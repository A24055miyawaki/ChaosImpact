#!/usr/bin/env python3
"""
Chaos Impact online relay: the room list for 合言葉 (password) rooms, the queues of ランダムマッチ and ランクマッチ, and
a UDP relay that carries the game's traffic between the players' machines over the internet. Nothing of the game
itself runs here (no Unreal Engine): a room's host is still one player's PC (a listen server). The relay tells each
player where the other can be reached so they can try to talk directly (through their routers, like other online games
do); where that does not work, every packet passes through this relay, so no one has to open ports on their router.

  Directory (one UDP port, JSON requests and replies):
    create {ver, password, name, host}            -> {ok, room, key, port}
    update {room, key, members, spectators, open, name}  -> {ok}
    search {ver, password}                        -> {ok, rooms:[{room, name, host, members, spectators, open, port}]}
    close  {room, key}                            -> {ok}

    Matchmaking (mode "random": up to 8 players, free-for-all; mode "duel": 1 v 1 for fun; mode "rank": 1 v 1 ranked):
    mm_join  {ver, mode, token, name, players, rtt, profile:{chara, color, device}}  -> {ok, ticket}
    mm_poll  {ticket, rtt}                        -> {ok, state:"waiting", waiting, elapsed, starts_in, notice}
                                                     {ok, state:"proposed", match, left, accepted, opponent_accepted,
                                                      ping, opponent:{name, chara, color, device, record}}   (duel)
                                                     {ok, state:"matched", match, mode, role:"host"|"guest", port,
                                                      key (host only), host_ready, expect, names:[...], opponent}
                                                     {ok, state:"expired"}   (a duel this player did not accept in time)
                                                     {ok, state:"gone"}   (expired or the match fell through)
    mm_accept {ticket, accept}                    -> {ok}   (duel: both accept the opponent shown, or neither plays)
    mm_leave {ticket}                             -> {ok}
    A duel found is only "proposed": its room is made once both players accept within DUEL_ACCEPT seconds of both having
    seen it. One who declines (or lets the time run out) stops searching; the other searches on ("notice": "declined"),
    not offered the same opponent again for a while.

    Records (1 v 1 and 8-player matches, in the same JSON file):
    record_info   {token, name}                   -> {ok, name, duel:{wins, losses, draws, recent}, ffa:{games, wins,
                                                      top3, average, recent:[[place, of], ...]}}
    record_report {match, token, game, result:"win"|"lose"|"draw"}   (duel)
                  {match, token, game, place, of}                     (random)   -> {ok, ...record_info}

    Rank (仮, the preseason: a JSON file on this VM, see --data):
    rank_info   {token, name}                     -> {ok, mmr, games, wins, losses, draws, best, placement, recent}
    rank_report {match, token, result:"win"|"lose"|"draw"}
                                                  -> {ok, state:"pending"} | {ok, state:"done", result, before, after,
                                                      delta, ...rank_info} | {ok, state:"void"}
  Every request carries "rid"; the reply echoes it.

    みんなのカラー (custom colours players post; ci_gallery.py, kept beside the ranks in gallery/):
    gallery_list, gallery_post, gallery_piece, gallery_paint, gallery_save, gallery_delete   (see ci_gallery.py)

  Relay (one UDP port per room, from the pool):
    host -> relay  b"CIH1" + key                  register / keep alive (every 2 s); relay answers b"CIHK"
    host -> relay  b"CIHD" + client id (4 bytes) + packet   -> the packet to that client
    client -> relay  any other packet             -> b"CIRD" + client id + packet to the host
    relay -> host  b"CIRX" + client id            that client went quiet and was dropped

  Direct connection (both sides over UDP; never forwarded to the game):
    host -> relay    b"CI2PPRIV" + "ip:port,..."  the host's addresses on its own network (with each CIH1)
    client -> relay  b"CI2PPRIV" + "ip:port,..."  the same from a client (every second; also keeps the client alive)
    relay -> client  b"CI2PHOST" + client id + token (8 bytes) + "pub=ip:port;priv=ip:port,..."   the host's addresses
    relay -> host    b"CI2PPEER" + client id + token + "pub=ip:port;priv=..."                    that client's addresses
  The two then probe each other (b"CI2PPROB"/b"CI2PRPLY" + client id + token) and send the game's packets directly
  while the probes are answered; the token (random, per client) keeps others from posing as either of them.

Run:  python3 ci_relay.py --bind 0.0.0.0 --port 7700 --room-ports 7701-7760 --data /var/lib/ci-relay/ranks.json
Python 3.8+ standard library only.
"""
import argparse
import asyncio
import hashlib
import json
import logging
import os
import secrets
import socket
import struct
import time

from ci_gallery import Gallery

ROOM_TIMEOUT = 20.0       # no update from the host for this long: the room is gone
CLIENT_TIMEOUT = 60.0     # a client silent this long is dropped
MAX_ROOMS_PER_PASSWORD = 8
TOURNEY_PASSWORD = "#TOURNEY"    # public tournaments: every one is listed under this
MAX_TOURNEYS = 40


def clean_tourney(data):
    """A public tournament's notice: its name, start (unix seconds), how it runs, its places and its state."""
    if not isinstance(data, dict):
        return None

    def number(key, low, high, default):
        try:
            return max(low, min(high, int(data.get(key, default))))
        except (TypeError, ValueError):
            return default
    state = str(data.get("state", "open"))
    return {"title": str(data.get("title", ""))[:24], "starts": number("starts", 0, 2 ** 40, 0), "mode": number("mode", 0, 1, 0),
            "cpus": number("cpus", 0, 31, 0), "max": number("max", 2, 8, 8), "level": number("level", -1, 3, -1),
            "state": state if state in ("open", "running", "done") else "open"}
MAX_CLIENTS_PER_ROOM = 16
MAX_PACKET = 2048
MUX_FORGET = 120.0        # a player's one-port (mux) address unheard this long is forgotten

TICKET_TIMEOUT = 15.0     # a queued player who stops polling this long leaves the queue (a level load can take a while)
MATCH_KEEP = 20 * 60.0    # a match is remembered this long (for late polls and the rank reports)
HOST_WAIT = 25.0          # the chosen host must reach the relay within this long, or the match falls through
RANDOM_MAX_PLAYERS = 8
RANDOM_GATHER = 20.0      # once two players wait, more may join for this long before the match is made
DUEL_ACCEPT = 15.0        # a 1 v 1 found waits this long for both players to accept it (from when both have seen it)
DUEL_SEEN_WAIT = 20.0     # ...and this long for both to see it at all (a player loading or gone is not waited for)
DUEL_AVOID = 120.0        # an opponent who declined is not offered again for this long
RANK_START = 1500
RANK_PLACEMENT_GAMES = 10
RANK_WINDOWS = ((15.0, 150), (45.0, 300))   # (seconds waited, MMR difference allowed); after the last: any
REPORT_WAIT = 60.0        # one side's report stands alone this long after it came (the other side left)
PAIR_DAY = 24 * 3600.0    # the same two players: half the change from their 6th game in a day, none from the 11th
# The ランダムマッチ ratings (1v1 and 8 players apart): shown once the measuring games are played (計測中 until then).
RATE_START = 1500
RATE_FLOOR = 500
RATE_MEASURING = 5        # games played before the rating shows
DUEL_K = (40.0, 20.0)     # (while measuring, after)
FFA_K = (48.0, 32.0)
RATE_HISTORY = 20         # the last ratings kept (the 戦績 graph)
FFA_REPORT_WAIT = 30.0    # an 8-player game is settled this long after its first report (who never said is last)

log = logging.getLogger("ci_relay")


def token_id(token):
    return hashlib.sha256(str(token).encode("utf-8")).hexdigest()[:32]


def clean_profile(profile):
    """What a player shows of themselves to a 1 v 1 opponent: character, colour and pad or keyboard."""
    profile = profile if isinstance(profile, dict) else {}

    def number(key):
        try:
            return max(0, min(63, int(profile.get(key, 0))))
        except (TypeError, ValueError):
            return 0
    return {"chara": number("chara"), "color": number("color"), "device": "key" if profile.get("device") == "key" else "pad"}


class Room:
    def __init__(self, relay, room_id, key, port, info, listed=True):
        self.relay = relay
        self.id = room_id
        self.key = key
        self.port = port
        self.password = info["password"]
        self.version = info["ver"]
        self.name = info.get("name", "")[:24]
        self.host_name = info.get("host", "")[:24]
        self.listed = listed
        self.tourney = clean_tourney(info.get("tourney")) if info.get("password") == TOURNEY_PASSWORD else None
        self.members = 1
        self.spectators = 0
        self.open = True
        self.updated = time.monotonic()
        self.transport = None
        self.host_addr = None
        self.host_seen = 0.0
        self.clients = {}       # addr -> [client id, last seen, token for the direct connection]
        self.host_private = ""  # the host's own network addresses (CI2PPRIV), for a client on the same network
        self.addresses = {}     # client id -> addr
        self.next_client = 1
        self.packets = 0
        self.bytes = 0

    def send(self, data, addr):
        """UDP and WSS endpoints may share a room without changing its packet protocol."""
        peer = self.relay.ws_peers.get(addr)
        if peer is not None:
            peer.send(data)
        elif addr in self.relay.mux_addrs and self.relay.mux_transport:
            self.relay.mux_transport.sendto(data, addr)
        elif addr[0] != "wss" and self.transport:
            self.transport.sendto(data, addr)

    def listing(self):
        listing = {"room": self.id, "name": self.name, "host": self.host_name, "members": self.members,
                   "spectators": self.spectators, "open": self.open, "port": self.port}
        if self.tourney:
            listing["tourney"] = self.tourney
        return listing

    # ---- relay traffic
    def datagram(self, data, addr):
        now = time.monotonic()
        self.packets += 1
        self.bytes += len(data)
        if data[:4] == b"CIH1" and data[4:].decode("ascii", "ignore") == self.key:
            if self.host_addr != addr:
                log.info("room %s: host at %s:%d", self.id, addr[0], addr[1])
            self.host_addr = addr
            self.host_seen = now
            self.send(b"CIHK", addr)
            return
        if addr == self.host_addr:
            self.host_seen = now
            if data[:4] == b"CIHD" and len(data) > 8:
                (client_id,) = struct.unpack(">I", data[4:8])
                target = self.addresses.get(client_id)
                if target:
                    self.send(data[8:], target)
            elif data[:8] == b"CI2PPRIV":
                self.host_private = clean_addresses(data[8:])
            return
        if self.host_addr is None:
            return
        entry = self.clients.get(addr)
        if entry is None:
            if len(self.clients) >= MAX_CLIENTS_PER_ROOM:
                return
            entry = [self.next_client, now, secrets.token_bytes(8)]
            self.next_client += 1
            self.clients[addr] = entry
            self.addresses[entry[0]] = addr
            log.info("room %s: client %d from %s:%d", self.id, entry[0], addr[0], addr[1])
        entry[1] = now
        if data[:8] == b"CI2PPRIV":
            self.introduce(entry, addr, clean_addresses(data[8:]))
            return
        self.send(b"CIRD" + struct.pack(">I", entry[0]) + data, self.host_addr)

    def introduce(self, entry, addr, private):
        """A client asked how to reach the host directly: each learns the other's addresses (repeated every second, so a
        lost datagram only delays it). Not over WSS: a WebSocket end has no UDP address to reach."""
        if addr[0] == "wss" or self.host_addr[0] == "wss":
            return
        head = struct.pack(">I", entry[0]) + entry[2]
        host = "pub=%s:%d;priv=%s" % (self.host_addr[0], self.host_addr[1], self.host_private)
        client = "pub=%s:%d;priv=%s" % (addr[0], addr[1], private)
        self.send(b"CI2PHOST" + head + host.encode("ascii"), addr)
        self.send(b"CI2PPEER" + head + client.encode("ascii"), self.host_addr)

    def sweep(self, now):
        for addr, (client_id, seen, _token) in list(self.clients.items()):
            if now - seen > CLIENT_TIMEOUT:
                del self.clients[addr]
                self.addresses.pop(client_id, None)
                if self.host_addr:
                    self.send(b"CIRX" + struct.pack(">I", client_id), self.host_addr)
                log.info("room %s: client %d dropped (quiet)", self.id, client_id)

    def close(self):
        for addr in [self.host_addr, *self.clients]:
            peer = self.relay.ws_peers.get(addr)
            if peer:
                peer.stop(1001, "room closed")
        if self.transport:
            self.transport.close()
            self.transport = None


def clean_addresses(data):
    """A player's "ip:port,..." (its own network's addresses), kept to IPv4 numbers and at most four of them."""
    found = []
    for part in data[:200].decode("ascii", "ignore").split(","):
        ip, _, port = part.strip().rpartition(":")
        try:
            socket.inet_aton(ip)
            if ip.count(".") == 3 and 0 < int(port) < 65536:
                found.append("%s:%d" % (ip, int(port)))
        except (OSError, ValueError):
            pass
    return ",".join(found[:4])


def ignore_unreachable(transport):
    """Windows: a datagram sent to a port that has closed (a player's game moving to the room's next level) comes back as
    an error on the socket, which stops asyncio reading it there; ask Windows not to report it. (Linux never does.)"""
    sock = transport.get_extra_info("socket")
    if sock is not None and hasattr(socket, "SIO_UDP_CONNRESET"):
        try:
            sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        except OSError:
            pass


class RoomProtocol(asyncio.DatagramProtocol):
    def __init__(self, room):
        self.room = room

    def connection_made(self, transport):
        ignore_unreachable(transport)
        self.room.transport = transport

    def error_received(self, exc):
        log.info("room %s: socket error %s (ignored)", self.room.id, exc)

    def datagram_received(self, data, addr):
        limit = MAX_PACKET + 8 if addr == self.room.host_addr and data[:4] == b"CIHD" else MAX_PACKET
        if len(data) <= limit:
            self.room.datagram(data, addr)


class DirectoryProtocol(asyncio.DatagramProtocol):
    def __init__(self, relay):
        self.relay = relay
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        try:
            request = json.loads(data.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return
        if not isinstance(request, dict):
            return
        asyncio.ensure_future(self.answer(request, addr))

    async def answer(self, request, addr):
        try:
            reply = await self.relay.handle(request, addr)
        except Exception:  # never let one bad request stop the relay
            log.exception("request failed")
            reply = {"ok": False, "error": "server"}
        reply["rid"] = request.get("rid", 0)
        if self.transport:
            self.transport.sendto(json.dumps(reply, ensure_ascii=False).encode("utf-8"), addr)


class MuxProtocol(DirectoryProtocol):
    """Everything on one UDP port, for a network that lets only well-known ports out (UDP 443 redirected here): a JSON
    request is the directory's, b"CIMX" + a room's port (2 bytes) + a datagram is that room's. Every answer goes back
    from this port, so the player's one way out is enough. The rooms and their packets are the same as on their own
    ports."""

    def connection_made(self, transport):
        ignore_unreachable(transport)
        self.transport = transport
        self.relay.mux_transport = transport

    def error_received(self, exc):
        log.info("mux: socket error %s (ignored)", exc)

    def datagram_received(self, data, addr):
        if data[:1] == b"{":
            super().datagram_received(data, addr)
            return
        if data[:4] != b"CIMX" or len(data) < 7:
            return
        (port,) = struct.unpack(">H", data[4:6])
        room = next((room for room in self.relay.rooms.values() if room.port == port), None)
        if room is None:
            return
        payload = data[6:]
        limit = MAX_PACKET + 8 if addr == room.host_addr and payload[:4] == b"CIHD" else MAX_PACKET
        if len(payload) > limit:
            return
        if addr not in self.relay.mux_addrs:
            log.info("room %s: %s:%d on the one-port entry", room.id, addr[0], addr[1])
        self.relay.mux_addrs[addr] = time.monotonic()
        room.datagram(payload, addr)


# ------------------------------------------------------------------------------------------------ Rank (preseason)

class RankBook:
    """The preseason's ranks: one JSON file, rewritten whole (atomically) after every settled match."""

    def __init__(self, path):
        self.path = path
        self.data = {"players": {}, "pairs": {}, "history": []}
        if path and os.path.exists(path):
            try:
                with open(path, "r", encoding="utf-8") as source:
                    loaded = json.load(source)
                if isinstance(loaded, dict):
                    self.data.update(loaded)
                log.info("ranks loaded: %d players", len(self.data["players"]))
            except (OSError, ValueError) as error:
                log.error("ranks file unreadable (%s): starting empty, the old file is kept as .bad", error)
                try:
                    os.replace(path, path + ".bad")
                except OSError:
                    pass

    def save(self):
        if not self.path:
            return
        folder = os.path.dirname(self.path)
        if folder:
            os.makedirs(folder, exist_ok=True)
        temporary = self.path + ".tmp"
        with open(temporary, "w", encoding="utf-8") as target:
            json.dump(self.data, target, ensure_ascii=False)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, self.path)

    def player(self, pid, name=None):
        entry = self.data["players"].get(pid)
        if entry is None:
            entry = {"name": "", "mmr": RANK_START, "games": 0, "wins": 0, "losses": 0, "draws": 0,
                     "best": RANK_START, "recent": []}
            self.data["players"][pid] = entry
        if name:
            entry["name"] = str(name)[:16]
        return entry

    # ---- the ランダムマッチ ratings (1v1 "duel", 8 players "ffa")
    def rating(self, pid, mode):
        entry = self.player(pid)
        rating = entry.setdefault(mode + "_rating", {})
        rating.setdefault("rate", float(RATE_START))
        rating.setdefault("games", 0)
        rating.setdefault("best", float(RATE_START))
        rating.setdefault("history", [])
        rating.setdefault("deltas", [])
        return rating

    def describe_rating(self, pid, mode):
        rating = self.rating(pid, mode)
        measured = rating["games"] >= RATE_MEASURING
        return {"rate": int(round(rating["rate"])) if measured else 0, "measured": measured, "rated_games": rating["games"],
                "measuring_left": max(0, RATE_MEASURING - rating["games"]),
                "best": int(round(rating["best"])) if measured else 0,
                "history": [int(v) for v in rating["history"][-RATE_HISTORY:]] if measured else [],
                "deltas": [int(v) for v in rating["deltas"][-10:]] if measured else []}

    @staticmethod
    def expected(rate, other):
        return 1.0 / (1.0 + 10 ** ((other - rate) / 400.0))

    def apply_rating(self, pid, mode, delta):
        rating = self.rating(pid, mode)
        before = rating["rate"]
        after = max(float(RATE_FLOOR), before + delta)
        rating["rate"] = after
        rating["games"] += 1
        if rating["games"] == RATE_MEASURING:
            rating["best"] = after
        elif rating["games"] > RATE_MEASURING:
            rating["best"] = max(rating["best"], after)
        rating["history"] = (rating["history"] + [int(round(after))])[-RATE_HISTORY:]
        rating["deltas"] = (rating["deltas"] + [int(round(after)) - int(round(before))])[-10:]
        return before, after

    def rated_change(self, pid, mode, before, after):
        rating = self.rating(pid, mode)
        measured = rating["games"] >= RATE_MEASURING
        delta = int(round(after)) - int(round(before))
        return {"state": "done", "measured": measured, "measuring_left": max(0, RATE_MEASURING - rating["games"]),
                "just_measured": rating["games"] == RATE_MEASURING,
                "before": int(round(before)), "after": int(round(after)), "delta": delta,
                "floor": after <= RATE_FLOOR + 0.001 and delta <= 0}

    def settle_duel(self, a, b, score_a):
        """A rated 1v1: score_a 1 a won, 0 b won, 0.5 drawn. Returns {pid: (before, after)}."""
        ra, rb = self.rating(a, "duel"), self.rating(b, "duel")
        expected_a = self.expected(ra["rate"], rb["rate"])
        ka = DUEL_K[0] if ra["games"] < RATE_MEASURING else DUEL_K[1]
        kb = DUEL_K[0] if rb["games"] < RATE_MEASURING else DUEL_K[1]
        delta_a, delta_b = ka * (score_a - expected_a), kb * ((1.0 - score_a) - (1.0 - expected_a))
        changes = {a: self.apply_rating(a, "duel", delta_a), b: self.apply_rating(b, "duel", delta_b)}
        self.save()
        return changes

    def settle_ffa(self, places):
        """A rated 8-player game: {pid: place} (people only). Each pair is a 1v1 (the better place wins), the sum shared
        out over the others. Returns {pid: (before, after)}."""
        pids = list(places)
        if len(pids) < 2:
            return {}
        rates = {pid: self.rating(pid, "ffa")["rate"] for pid in pids}
        deltas = {}
        for pid in pids:
            total = 0.0
            for other in pids:
                if other == pid:
                    continue
                score = 1.0 if places[pid] < places[other] else 0.5 if places[pid] == places[other] else 0.0
                total += score - self.expected(rates[pid], rates[other])
            k = FFA_K[0] if self.rating(pid, "ffa")["games"] < RATE_MEASURING else FFA_K[1]
            deltas[pid] = k * total / (len(pids) - 1)
        changes = {pid: self.apply_rating(pid, "ffa", delta) for pid, delta in deltas.items()}
        self.save()
        return changes

    def describe(self, pid):
        entry = self.data["players"].get(pid) or self.player(pid)
        return {"mmr": int(round(entry["mmr"])), "games": entry["games"], "wins": entry["wins"],
                "losses": entry["losses"], "draws": entry["draws"], "best": int(round(entry["best"])),
                "placement": max(0, RANK_PLACEMENT_GAMES - entry["games"]), "recent": entry["recent"][-10:]}

    def mmr(self, pid):
        entry = self.data["players"].get(pid)
        return entry["mmr"] if entry else RANK_START

    # ---- records of the 1 v 1 and 8-player matches (no rating: just what happened)
    def records(self, pid):
        entry = self.player(pid)
        duel = entry.setdefault("duel", {})
        for key in ("wins", "losses", "draws"):
            duel.setdefault(key, 0)
        duel.setdefault("recent", [])
        duel.setdefault("streak", 0)
        duel.setdefault("best_streak", 0)
        ffa = entry.setdefault("ffa", {})
        for key in ("games", "wins", "top3", "places", "streak", "best_streak"):
            ffa.setdefault(key, 0)
        ffa.setdefault("recent", [])
        return duel, ffa

    def describe_duel(self, pid):
        duel, _ = self.records(pid)
        return dict({"wins": duel["wins"], "losses": duel["losses"], "draws": duel["draws"], "recent": duel["recent"][-10:],
                     "streak": duel["streak"], "best_streak": duel["best_streak"]}, **self.describe_rating(pid, "duel"))

    def describe_records(self, pid):
        _, ffa = self.records(pid)
        average = round(ffa["places"] / ffa["games"], 1) if ffa["games"] else 0
        return {"name": self.player(pid)["name"], "duel": self.describe_duel(pid),
                "ffa": dict({"games": ffa["games"], "wins": ffa["wins"], "top3": ffa["top3"], "average": average,
                             "recent": ffa["recent"][-10:], "streak": ffa["streak"], "best_streak": ffa["best_streak"]},
                            **self.describe_rating(pid, "ffa"))}

    def add_duel(self, pid, result):
        duel, _ = self.records(pid)
        duel[{"win": "wins", "lose": "losses", "draw": "draws"}[result]] += 1
        duel["recent"] = (duel["recent"] + [{"win": "W", "lose": "L", "draw": "D"}[result]])[-10:]
        duel["streak"] = duel["streak"] + 1 if result == "win" else 0
        duel["best_streak"] = max(duel["best_streak"], duel["streak"])
        self.save()

    def add_ffa(self, pid, place, of):
        _, ffa = self.records(pid)
        ffa["games"] += 1
        ffa["wins"] += 1 if place == 1 else 0
        ffa["top3"] += 1 if place <= 3 else 0
        ffa["places"] += place
        ffa["recent"] = (ffa["recent"] + [[place, of]])[-10:]
        ffa["streak"] = ffa["streak"] + 1 if place == 1 else 0
        ffa["best_streak"] = max(ffa["best_streak"], ffa["streak"])
        self.save()

    def settle(self, a, b, score_a, match_id):
        """score_a: 1 a won, 0 b won, 0.5 draw. Returns {pid: (before, after)}."""
        now = time.time()
        pair = "|".join(sorted((a, b)))
        played = [t for t in self.data["pairs"].get(pair, []) if now - t < PAIR_DAY]
        factor = 1.0 if len(played) < 5 else 0.5 if len(played) < 10 else 0.0
        played.append(now)
        self.data["pairs"][pair] = played
        changes = {}
        pa, pb = self.player(a), self.player(b)
        expected_a = 1.0 / (1.0 + 10 ** ((pb["mmr"] - pa["mmr"]) / 400.0))
        for entry, pid, score, expected in ((pa, a, score_a, expected_a), (pb, b, 1.0 - score_a, 1.0 - expected_a)):
            k = 48 if entry["games"] < RANK_PLACEMENT_GAMES else 24
            before = entry["mmr"]
            entry["mmr"] = before + k * factor * (score - expected)
            entry["games"] += 1
            if score == 1.0:
                entry["wins"] += 1
            elif score == 0.0:
                entry["losses"] += 1
            else:
                entry["draws"] += 1
            entry["best"] = max(entry["best"], entry["mmr"])
            entry["recent"] = (entry["recent"] + ["W" if score == 1.0 else "L" if score == 0.0 else "D"])[-10:]
            changes[pid] = (before, entry["mmr"])
        self.data["history"] = (self.data["history"] + [{"match": match_id, "at": int(now), "a": a, "b": b,
                                                         "score_a": score_a, "factor": factor}])[-500:]
        self.save()
        return changes


# ------------------------------------------------------------------------------------------------ Matchmaking

class Ticket:
    def __init__(self, ticket_id, request, pid):
        now = time.monotonic()
        self.id = ticket_id
        self.mode = request["mode"]
        self.version = request["ver"]
        self.pid = pid
        self.name = str(request.get("name", ""))[:16]
        self.players = max(1, min(2, int(request.get("players", 1)))) if self.mode == "random" else 1
        self.rtt = int(request.get("rtt", 999) or 999)
        self.mmr = RANK_START
        self.joined = now
        self.seen = now
        self.match = None
        self.profile = clean_profile(request.get("profile"))
        self.notice = ""          # said once on the next poll: "declined" (the opponent found said no)
        self.avoid = {}           # pid -> until when that opponent is not offered again
        self.expired = False      # a duel offered was not accepted: the search ends on the next poll


class Match:
    def __init__(self, match_id, mode, room, tickets):
        self.id = match_id
        self.mode = mode
        self.room = room
        self.tickets = tickets            # host first
        self.created = time.monotonic()
        self.reports = {}                 # pid -> (result, at)
        self.outcome = None               # "done" | "void"
        self.changes = {}
        self.accepts = set()              # duel: the pids that accepted (the room is made when both have)
        self.seen = set()                 # duel: the pids that have been shown the opponent
        self.deadline = None              # duel: when the answers are due (set once both have seen it)
        self.recorded = set()             # (pid, game) already in the records
        self.games = {}                   # ランダムマッチ game -> {"reports": {pid: (value, at[, of])}, "changes", "outcome"}

    def host(self):
        return self.tickets[0]


class Relay:
    def __init__(self, bind, ports, data_path):
        self.bind = bind
        self.free_ports = list(ports)
        self.rooms = {}
        self.tickets = {}
        self.matches = {}
        self.gather_since = {}            # version -> when two random players first waited together
        self.ranks = RankBook(data_path)
        self.gallery = Gallery(os.path.join(os.path.dirname(os.path.abspath(data_path)), "gallery") if data_path else None)
        self.matchmaking = False
        self.ws_peers = {}
        self.mux_transport = None
        self.mux_addrs = {}               # (ip, port) heard on the one-port entry -> when last heard

    async def handle(self, request, addr):
        op = request.get("op")
        if op == "ping":
            return {"ok": True, "protocol": 1, "transports": ["udp", "wss"] + (["mux"] if self.mux_transport else [])}
        if op == "create":
            return await self.create(request, addr)
        if isinstance(op, str) and op.startswith("gallery_"):
            return self.gallery.handle(request)
        if op == "search":
            password = str(request.get("password", ""))
            version = str(request.get("ver", ""))
            rooms = [room.listing() for room in self.rooms.values()
                     if room.listed and room.password == password and room.version == version and room.host_addr is not None]
            return {"ok": True, "rooms": rooms}
        if op == "mm_join":
            return self.queue_join(request)
        if op == "mm_poll":
            return self.queue_poll(request)
        if op == "mm_accept":
            return self.queue_accept(request)
        if op == "record_info":
            if not request.get("token"):
                return {"ok": False, "error": "bad request"}
            pid = token_id(request["token"])
            self.ranks.player(pid, request.get("name"))
            return dict(ok=True, **self.ranks.describe_records(pid))
        if op == "record_report":
            return self.record_report(request)
        if op == "mm_leave":
            ticket = self.tickets.pop(str(request.get("ticket", "")), None)
            if ticket and ticket.match is None:
                log.info("queue %s: %s left", ticket.mode, ticket.name)
            return {"ok": True}
        if op == "rank_info":
            if not request.get("token"):
                return {"ok": False, "error": "bad request"}
            pid = token_id(request["token"])
            self.ranks.player(pid, request.get("name"))
            return dict(ok=True, **self.ranks.describe(pid))
        if op == "rank_report":
            return self.rank_report(request)
        room = self.rooms.get(str(request.get("room", "")))
        if room is None or request.get("key") != room.key:
            return {"ok": False, "error": "unknown room"}
        if op == "update":
            room.members = int(request.get("members", room.members))
            room.spectators = int(request.get("spectators", room.spectators))
            room.open = bool(request.get("open", room.open))
            room.name = str(request.get("name", room.name))[:24]
            if room.tourney and isinstance(request.get("tourney_state"), str) and request["tourney_state"] in ("open", "running", "done"):
                room.tourney["state"] = request["tourney_state"]
            room.updated = time.monotonic()
            return {"ok": True}
        if op == "close":
            self.remove(room, "closed by its host")
            return {"ok": True}
        return {"ok": False, "error": "unknown op"}

    async def create(self, request, addr):
        if not isinstance(request.get("password"), str) or not isinstance(request.get("ver"), str):
            return {"ok": False, "error": "bad request"}
        same = sum(1 for room in self.rooms.values() if room.password == request["password"])
        limit = MAX_TOURNEYS if request["password"] == TOURNEY_PASSWORD else MAX_ROOMS_PER_PASSWORD
        if same >= limit or not self.free_ports:
            return {"ok": False, "error": "full"}
        room = await self.open_room(request, listed=True)
        if room is None:
            return {"ok": False, "error": "port"}
        log.info("room %s created on port %d by %s:%d (\"%s\")", room.id, room.port, addr[0], addr[1], room.name)
        return {"ok": True, "room": room.id, "key": room.key, "port": room.port}

    async def open_room(self, info, listed):
        if not self.free_ports:
            return None
        port = self.free_ports.pop(0)
        room = Room(self, secrets.token_hex(4), secrets.token_hex(16), port, info, listed)
        loop = asyncio.get_running_loop()
        try:
            await loop.create_datagram_endpoint(lambda: RoomProtocol(room), local_addr=(self.bind, port))
        except OSError as error:
            log.error("room port %d unavailable: %s", port, error)
            return None
        self.rooms[room.id] = room
        return room

    def remove(self, room, why):
        room.close()
        if self.rooms.pop(room.id, None) is not None:
            self.free_ports.append(room.port)
        log.info("room %s removed (%s): %d packets, %.1f MB", room.id, why, room.packets, room.bytes / 1e6)

    # ---- queues
    def queue_join(self, request):
        mode = request.get("mode")
        if mode not in ("random", "duel", "rank") or not isinstance(request.get("ver"), str) or not request.get("token"):
            return {"ok": False, "error": "bad request"}
        if mode in ("duel", "rank") and int(request.get("players", 1)) != 1:
            return {"ok": False, "error": "solo only"}
        pid = token_id(request["token"])
        # One place per player: joining again (a retried request, or back from a match) replaces the old ticket.
        for old in [t for t in self.tickets.values() if t.pid == pid and t.match is None]:
            del self.tickets[old.id]
        ticket = Ticket(secrets.token_hex(8), request, pid)
        self.ranks.player(pid, ticket.name)
        if mode == "rank":
            self.ranks.player(pid, ticket.name)
            ticket.mmr = self.ranks.mmr(pid)
        self.tickets[ticket.id] = ticket
        log.info("queue %s: %s joined (%d player(s), mmr %d)", mode, ticket.name, ticket.players, ticket.mmr)
        return {"ok": True, "ticket": ticket.id}

    def queue_poll(self, request):
        ticket = self.tickets.get(str(request.get("ticket", "")))
        if ticket is None:
            return {"ok": True, "state": "gone"}
        now = time.monotonic()
        ticket.seen = now
        if request.get("rtt"):
            ticket.rtt = int(request["rtt"])
        if ticket.expired:
            self.tickets.pop(ticket.id, None)
            return {"ok": True, "state": "expired"}
        match = self.matches.get(ticket.match) if ticket.match else None
        if match is None:
            waiting = [t for t in self.tickets.values()
                       if t.match is None and not t.expired and t.mode == ticket.mode and t.version == ticket.version]
            reply = {"ok": True, "state": "waiting", "waiting": sum(t.players for t in waiting),
                     "elapsed": round(now - ticket.joined, 1), "starts_in": -1, "notice": ticket.notice}
            ticket.notice = ""
            since = self.gather_since.get(ticket.version)
            if ticket.mode == "random" and since is not None:
                reply["starts_in"] = round(max(0.0, RANDOM_GATHER - (now - since)), 1)
            return reply
        if match.room is None:
            # A duel found: who it is, how far away, and whether each side has accepted. The time to answer runs from
            # when both have been shown it.
            other = match.tickets[1] if ticket is match.tickets[0] else match.tickets[0]
            match.seen.add(ticket.pid)
            if match.deadline is None and len(match.seen) == 2:
                match.deadline = now + DUEL_ACCEPT
            left = DUEL_ACCEPT if match.deadline is None else max(0.0, match.deadline - now)
            return {"ok": True, "state": "proposed", "match": match.id, "left": round(left, 1),
                    "accepted": ticket.pid in match.accepts, "opponent_accepted": other.pid in match.accepts,
                    # Through the relay: the most it takes (a direct line, found once they connect, is quicker).
                    "ping": min(999, ticket.rtt + other.rtt),
                    "opponent": dict(name=other.name, record=self.ranks.describe_duel(other.pid), **other.profile)}
        if match.room.id not in self.rooms:
            # The host never came, or left before anyone joined.
            self.tickets.pop(ticket.id, None)
            return {"ok": True, "state": "gone"}
        host = match.host()
        reply = {"ok": True, "state": "matched", "match": match.id, "mode": match.mode,
                 "role": "host" if ticket is host else "guest", "port": match.room.port,
                 "host_ready": match.room.host_addr is not None,
                 "expect": sum(t.players for t in match.tickets),
                 "names": [t.name for t in match.tickets]}
        if ticket is host:
            reply["key"] = match.room.key
            reply["room"] = match.room.id
        if match.mode == "duel":
            other = match.tickets[1] if ticket is host else host
            reply["opponent"] = dict(name=other.name, record=self.ranks.describe_duel(other.pid), **other.profile)
        if match.mode == "rank":
            other = match.tickets[1] if ticket is host else host
            reply["opponent"] = {"name": other.name, **self.ranks.describe(other.pid)}
        return reply

    def queue_accept(self, request):
        ticket = self.tickets.get(str(request.get("ticket", "")))
        match = self.matches.get(ticket.match) if ticket and ticket.match else None
        if match is None or match.mode != "duel" or match.room is not None:
            return {"ok": False, "error": "unknown match"}
        if request.get("accept") is True:
            match.accepts.add(ticket.pid)
            log.info("match %s: %s accepts", match.id, ticket.name)
        else:
            log.info("match %s: %s declines", match.id, ticket.name)
            self.end_proposal(match, [ticket])
        return {"ok": True}

    def end_proposal(self, match, refusers):
        """A duel found will not be played: who did not accept stops searching, the other searches on."""
        now = time.monotonic()
        for ticket in match.tickets:
            ticket.match = None
            if ticket in refusers:
                ticket.expired = True
            else:
                ticket.notice = "declined"
                for other in refusers:
                    ticket.avoid[other.pid] = now + DUEL_AVOID
        self.matches.pop(match.id, None)

    async def matchmake(self):
        now = time.monotonic()
        for ticket in [t for t in self.tickets.values() if now - t.seen > TICKET_TIMEOUT]:
            del self.tickets[ticket.id]
            if ticket.match is None:
                log.info("queue %s: %s dropped (quiet)", ticket.mode, ticket.name)
        # Duels found: made once both accept; dropped when one says no, lets the time run out or leaves.
        for match in [m for m in self.matches.values() if m.room is None]:
            gone = [t for t in match.tickets if t.id not in self.tickets]
            if gone:
                self.end_proposal(match, gone)
            elif all(t.pid in match.accepts for t in match.tickets):
                room = await self.open_room({"password": "\x00match", "ver": match.host().version, "name": "duel",
                                             "host": match.host().name}, listed=False)
                if room is None:
                    log.error("queue duel: no room port free for a match")
                    self.end_proposal(match, [])
                    continue
                match.room = room
                match.created = now
                log.info("match %s (duel) accepted: hosted by %s on port %d", match.id, match.host().name, room.port)
            elif match.deadline is not None and now > match.deadline:
                log.info("match %s (duel): not accepted in time", match.id)
                self.end_proposal(match, [t for t in match.tickets if t.pid not in match.accepts])
            elif match.deadline is None and now - match.created > DUEL_SEEN_WAIT:
                log.info("match %s (duel): not seen by both in time", match.id)
                self.end_proposal(match, [t for t in match.tickets if t.pid not in match.seen])
        waiting = [t for t in self.tickets.values() if t.match is None and not t.expired]
        for version in {t.version for t in waiting}:
            await self.match_random(version, sorted((t for t in waiting if t.version == version and t.mode == "random"),
                                                    key=lambda t: t.joined), now)
            self.match_duel(sorted((t for t in waiting if t.version == version and t.mode == "duel"),
                                   key=lambda t: t.joined), now)
            await self.match_rank(version, sorted((t for t in waiting if t.version == version and t.mode == "rank"),
                                                  key=lambda t: t.joined), now)
        for match in list(self.matches.values()):
            if match.room is None:
                continue
            if now - match.created > MATCH_KEEP:
                del self.matches[match.id]
            elif match.room.id in self.rooms and match.room.host_addr is None and now - match.created > HOST_WAIT:
                self.remove(match.room, "the matched host never came")
            else:
                for game, rated in list(match.games.items()):
                    if rated["outcome"] is None and rated["reports"]:
                        first = min(value[1] for value in rated["reports"].values())
                        if now - first > (REPORT_WAIT if match.mode == "duel" else FFA_REPORT_WAIT):
                            self.settle_game(match, game)
                if match.mode == "rank" and match.outcome is None and match.reports:
                    first = min(at for _, at in match.reports.values())
                    if now - first > REPORT_WAIT:
                        self.settle(match)

    async def match_random(self, version, waiting, now):
        while sum(t.players for t in waiting) >= 2:
            since = self.gather_since.setdefault(version, now)
            group, count = [], 0
            for ticket in waiting:
                if count + ticket.players <= RANDOM_MAX_PLAYERS:
                    group.append(ticket)
                    count += ticket.players
            if count < RANDOM_MAX_PLAYERS and now - since < RANDOM_GATHER:
                return
            if len(group) < 2:
                return
            await self.make_match("random", group)
            waiting = [t for t in waiting if t not in group]
            self.gather_since.pop(version, None)
        self.gather_since.pop(version, None)

    def match_duel(self, waiting, now):
        """1 v 1: the one who has waited longest with the nearest rating (any rating once they have waited a little;
        never someone who just declined the other)."""
        taken = set()
        for ticket in waiting:
            if ticket.id in taken:
                continue
            mine = self.ranks.rating(ticket.pid, "duel")["rate"]
            window = 150 if now - ticket.joined < 6.0 else 350 if now - ticket.joined < 12.0 else 1 << 30
            candidates = sorted((o for o in waiting if o is not ticket), key=lambda o: abs(self.ranks.rating(o.pid, "duel")["rate"] - mine))
            for other in candidates:
                if other.id in taken or other.pid == ticket.pid:
                    continue
                if ticket.avoid.get(other.pid, 0) > now or other.avoid.get(ticket.pid, 0) > now:
                    continue
                if abs(self.ranks.rating(other.pid, "duel")["rate"] - mine) > max(window, 150 if now - other.joined < 6.0 else 350
                                                                                 if now - other.joined < 12.0 else 1 << 30):
                    continue
                taken.update((ticket.id, other.id))
                # The host is the one with the quicker way to the relay.
                group = sorted([ticket, other], key=lambda t: (t.rtt, t.joined))
                match = Match(secrets.token_hex(6), "duel", None, group)
                self.matches[match.id] = match
                for member in group:
                    member.match = match.id
                log.info("match %s (duel) found: %s and %s, waiting for both to accept", match.id, group[0].name,
                         group[1].name)
                break

    async def match_rank(self, version, waiting, now):
        def allowed(ticket):
            waited = now - ticket.joined
            for seconds, window in RANK_WINDOWS:
                if waited < seconds:
                    return window
            return 1 << 30
        taken = set()
        for ticket in waiting:
            if ticket.id in taken:
                continue
            best = None
            for other in waiting:
                if other is ticket or other.id in taken or other.pid == ticket.pid:
                    continue
                gap = abs(other.mmr - ticket.mmr)
                if gap <= max(allowed(ticket), allowed(other)) and (best is None or gap < abs(best.mmr - ticket.mmr)):
                    best = other
            if best is not None:
                taken.update((ticket.id, best.id))
                await self.make_match("rank", [ticket, best])

    async def make_match(self, mode, group):
        # The host is the machine with the quickest way to the relay (everyone's packets pass through it).
        group = sorted(group, key=lambda t: (t.rtt, t.joined))
        room = await self.open_room({"password": "\x00match", "ver": group[0].version, "name": mode,
                                     "host": group[0].name}, listed=False)
        if room is None:
            log.error("queue %s: no room port free for a match", mode)
            return
        match = Match(secrets.token_hex(6), mode, room, group)
        self.matches[match.id] = match
        for ticket in group:
            ticket.match = match.id
        log.info("match %s (%s): %s, hosted by %s on port %d", match.id, mode,
                 ", ".join(f"{t.name}({t.players})" for t in group), group[0].name, room.port)

    # ---- records
    def record_report(self, request):
        match = self.matches.get(str(request.get("match", "")))
        if match is None or match.mode not in ("duel", "random") or not request.get("token"):
            return {"ok": False, "error": "unknown match"}
        pid = token_id(request["token"])
        if pid not in (t.pid for t in match.tickets):
            return {"ok": False, "error": "not in this match"}
        try:
            game = int(request.get("game", 0))
            key = (pid, game)
            if key not in match.recorded:
                if match.mode == "duel":
                    result = request.get("result")
                    if result not in ("win", "lose", "draw"):
                        return {"ok": False, "error": "bad request"}
                    self.ranks.add_duel(pid, result)
                else:
                    of = max(2, min(RANDOM_MAX_PLAYERS, int(request.get("of", RANDOM_MAX_PLAYERS))))
                    place = max(1, min(of, int(request.get("place", of))))
                    self.ranks.add_ffa(pid, place, of)
                match.recorded.add(key)
                log.info("match %s: %s recorded game %d", match.id, self.ranks.player(pid)["name"], game)
                rated = match.games.setdefault(game, {"reports": {}, "changes": None, "outcome": None})
                if match.mode == "duel":
                    rated["reports"][pid] = (request.get("result"), time.monotonic())
                    if len(rated["reports"]) == 2:
                        self.settle_game(match, game)
                else:
                    rated["reports"][pid] = (place, time.monotonic(), of)
                    if all(t.pid in rated["reports"] for t in match.tickets):
                        self.settle_game(match, game)
        except (TypeError, ValueError):
            return {"ok": False, "error": "bad request"}
        return dict(ok=True, rating=self.game_rating(match, game, pid), **self.ranks.describe_records(pid))

    def game_rating(self, match, game, pid):
        """Where this player's rating stands for that game: pending, done (before / after) or void (not counted)."""
        rated = match.games.get(game)
        if rated is None or rated["outcome"] is None:
            return {"state": "pending"}
        if rated["outcome"] == "void" or pid not in (rated["changes"] or {}):
            return {"state": "void"}
        before, after = rated["changes"][pid]
        return self.ranks.rated_change(pid, "duel" if match.mode == "duel" else "ffa", before, after)

    def settle_game(self, match, game):
        rated = match.games.get(game)
        if rated is None or rated["outcome"] is not None:
            return
        reports = rated["reports"]
        if match.mode == "duel":
            a, b = match.tickets[0].pid, match.tickets[1].pid
            ra, rb = (reports.get(a) or (None,))[0], (reports.get(b) or (None,))[0]
            flip = {"win": "lose", "lose": "win", "draw": "draw"}
            if ra and rb and flip.get(ra) != rb:
                rated["outcome"] = "void"
                log.warning("match %s game %d: the 1v1 reports disagree (%s / %s): not rated", match.id, game, ra, rb)
                return
            said = ra or flip.get(rb)
            if said not in flip:
                rated["outcome"] = "void"
                return
            rated["changes"] = self.ranks.settle_duel(a, b, 1.0 if said == "win" else 0.0 if said == "lose" else 0.5)
        else:
            # Who never said (left, or gone) is last.
            of = max([value[2] for value in reports.values()] + [len(match.tickets)])
            places = {t.pid: (reports[t.pid][0] if t.pid in reports else of) for t in match.tickets}
            rated["changes"] = self.ranks.settle_ffa(places)
        rated["outcome"] = "done"
        log.info("match %s game %d rated: %s", match.id, game, ", ".join(
            f"{self.ranks.player(pid)['name']} {before:.0f}->{after:.0f}" for pid, (before, after) in (rated["changes"] or {}).items()))

    # ---- rank results
    def rank_report(self, request):
        match = self.matches.get(str(request.get("match", "")))
        result = request.get("result")
        if match is None or match.mode != "rank" or result not in ("win", "lose", "draw") or not request.get("token"):
            return {"ok": False, "error": "unknown match"}
        pid = token_id(request["token"])
        if pid not in (t.pid for t in match.tickets):
            return {"ok": False, "error": "not in this match"}
        if match.outcome is None and pid not in match.reports:
            match.reports[pid] = (result, time.monotonic())
            log.info("match %s: %s reports %s", match.id, self.ranks.player(pid)["name"], result)
            if len(match.reports) == 2:
                self.settle(match)
        if match.outcome is None:
            return {"ok": True, "state": "pending"}
        if match.outcome == "void":
            return {"ok": True, "state": "void"}
        before, after = match.changes[pid]
        mine = match.reports.get(pid, (None,))[0]
        return dict(ok=True, state="done", result=mine or "", before=int(round(before)), after=int(round(after)),
                    delta=int(round(after)) - int(round(before)), **self.ranks.describe(pid))

    def settle(self, match):
        a, b = match.tickets[0].pid, match.tickets[1].pid
        ra = match.reports.get(a, (None,))[0]
        rb = match.reports.get(b, (None,))[0]
        flip = {"win": "lose", "lose": "win", "draw": "draw"}
        if ra and rb and flip[ra] != rb:
            match.outcome = "void"
            log.warning("match %s: the reports disagree (%s / %s): not counted", match.id, ra, rb)
            return
        said = ra or flip[rb]
        score_a = 1.0 if said == "win" else 0.0 if said == "lose" else 0.5
        # A side that never reported is counted as the other side says (it left the match).
        for pid, theirs in ((a, said), (b, flip[said])):
            match.reports.setdefault(pid, (theirs, time.monotonic()))
        match.changes = self.ranks.settle(a, b, score_a, match.id)
        match.outcome = "done"
        log.info("match %s settled: %s", match.id, ", ".join(
            f"{self.ranks.player(pid)['name']} {before:.0f}->{after:.0f}" for pid, (before, after) in match.changes.items()))

    async def sweep(self):
        last_rooms = 0.0
        while True:
            await asyncio.sleep(0.5)
            now = time.monotonic()
            try:
                await self.matchmake()
            except Exception:
                log.exception("matchmaking failed")
            if now - last_rooms < 2.0:
                continue
            last_rooms = now
            for addr, heard in list(self.mux_addrs.items()):
                if now - heard > MUX_FORGET:
                    del self.mux_addrs[addr]
            for room in list(self.rooms.values()):
                if now - room.updated > ROOM_TIMEOUT:
                    self.remove(room, "host went quiet")
                else:
                    room.sweep(now)


async def main():
    parser = argparse.ArgumentParser(description="Chaos Impact online relay")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=7700)
    parser.add_argument("--room-ports", default="7701-7760")
    parser.add_argument("--mux-port", type=int, default=0,
                        help="optional one-port UDP entry (directory and every room), e.g. UDP 443 redirected here (0: off)")
    parser.add_argument("--ws-bind", default="127.0.0.1")
    parser.add_argument("--ws-port", type=int, default=0,
                        help="optional WebSocket listener behind a TLS reverse proxy (0: UDP only)")
    parser.add_argument("--data", default=os.environ.get("STATE_DIRECTORY", ".").split(":")[0] + "/ranks.json",
                        help="where the preseason's ranks are kept (one JSON file)")
    args = parser.parse_args()
    first, last = (int(value) for value in args.room_ports.split("-"))
    relay = Relay(args.bind, range(first, last + 1), args.data)
    loop = asyncio.get_running_loop()
    await loop.create_datagram_endpoint(lambda: DirectoryProtocol(relay), local_addr=(args.bind, args.port))
    log.info("listening on %s:%d, room ports %d-%d, ranks in %s", args.bind, args.port, first, last, args.data)
    if args.mux_port:
        await loop.create_datagram_endpoint(lambda: MuxProtocol(relay), local_addr=(args.bind, args.mux_port))
        log.info("one-port entry on %s:%d", args.bind, args.mux_port)
    ws_server = None
    if args.ws_port:
        from ci_relay_ws import start_server
        ws_server = await start_server(relay, args.ws_bind, args.ws_port)
    try:
        await relay.sweep()
    finally:
        if ws_server:
            ws_server.close()
            await ws_server.wait_closed()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
