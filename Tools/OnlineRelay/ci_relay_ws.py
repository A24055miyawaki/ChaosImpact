"""WSS entry point. Bind to loopback behind nginx TLS; shares the existing UDP room/rank state.

One persistent /directory socket carries JSON requests; /room/<UDP room port> carries binary
datagrams. Framing, TLS, masking, ping/pong and size limits are handled by websockets.
"""
import asyncio
import itertools
import json
import logging
import socket
from http import HTTPStatus

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

log = logging.getLogger("ci_relay")
MAX_DIRECTORY_BYTES = 16384
MAX_ROOM_BYTES = 2056  # MAX_PACKET (2048) + host envelope (8)
MAX_CONNECTIONS = 128
MAX_QUEUE = 256


class Peer:
    def __init__(self, ws):
        self.ws = ws
        self.queue = asyncio.Queue(maxsize=MAX_QUEUE)
        self.closing = False

    def send(self, data):
        if self.closing:
            return
        try:
            self.queue.put_nowait(data)
        except asyncio.QueueFull:
            self.stop(1013, "slow receiver")

    def stop(self, code=1001, reason="closed"):
        if not self.closing:
            self.closing = True
            asyncio.create_task(self.ws.close(code, reason))

    async def write(self):
        while True:
            await self.ws.send(await self.queue.get())


async def start_server(relay, bind, port, ssl_context=None):
    sequence = itertools.count(1)
    active = set()

    def room_at(path):
        if not path.startswith("/room/") or not path[6:].isdigit():
            return None
        number = int(path[6:])
        return next((room for room in relay.rooms.values() if room.port == number), None)

    def check_request(connection, request):
        if request.path == "/health":
            return connection.respond(HTTPStatus.OK, "Chaos Impact relay OK\n")
        if len(active) >= MAX_CONNECTIONS:
            return connection.respond(HTTPStatus.SERVICE_UNAVAILABLE, "relay busy\n")
        if request.path != "/directory" and room_at(request.path) is None:
            return connection.respond(HTTPStatus.NOT_FOUND, "unknown room\n")
        return None

    async def handler(ws):
        # asyncio normally enables this already; make low-latency small-packet sends explicit.
        transport_socket = ws.transport.get_extra_info("socket")
        if transport_socket:
            transport_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        if len(active) >= MAX_CONNECTIONS:
            await ws.close(1013, "relay busy")
            return
        active.add(ws)
        room = None
        peer = None
        writer = None
        addr = ("wss", next(sequence))
        try:
            if ws.request.path == "/directory":
                async for raw in ws:
                    if not isinstance(raw, str):
                        await ws.close(1003, "directory expects text")
                        return
                    try:
                        request = json.loads(raw)
                        if not isinstance(request, dict):
                            raise ValueError("not an object")
                        reply = await relay.handle(request, addr)
                        reply["rid"] = request.get("rid", 0)
                    except (ValueError, TypeError, KeyError):
                        await ws.close(1007, "invalid request")
                        return
                    except Exception:
                        log.exception("WSS directory request failed")
                        reply = {"ok": False, "error": "server", "rid": request.get("rid", 0)}
                    await ws.send(json.dumps(reply, ensure_ascii=False))
            else:
                room = room_at(ws.request.path)
                if room is None:
                    await ws.close(1001, "room closed")
                    return
                peer = Peer(ws)
                relay.ws_peers[addr] = peer
                writer = asyncio.create_task(peer.write())
                log.info("room %s: WSS transport connected (%d)", room.id, addr[1])
                async for raw in ws:
                    if not isinstance(raw, bytes) or not 0 < len(raw) <= MAX_ROOM_BYTES:
                        await ws.close(1009, "invalid game datagram")
                        return
                    # The larger host envelope is accepted only from the authenticated host.
                    if len(raw) > 2048 and addr != room.host_addr:
                        await ws.close(1009, "game datagram too large")
                        return
                    if room.transport is None:
                        await ws.close(1001, "room closed")
                        return
                    room.datagram(raw, addr)
        except ConnectionClosed:
            pass
        finally:
            active.discard(ws)
            relay.ws_peers.pop(addr, None)
            if writer:
                writer.cancel()
                await asyncio.gather(writer, return_exceptions=True)
            if room and addr in room.clients:
                client_id, _ = room.clients.pop(addr)
                room.addresses.pop(client_id, None)
                if room.host_addr:
                    room.send(b"CIRX" + client_id.to_bytes(4, "big"), room.host_addr)
            if room and room.host_addr == addr:
                room.host_addr = None
                # The host's directory heartbeat expires the room as in the UDP path.
                for client_addr in list(room.clients):
                    other = relay.ws_peers.get(client_addr)
                    if other:
                        other.stop(1001, "host disconnected")

    server = await serve(handler, bind, port, ssl=ssl_context,
                         process_request=check_request, compression=None,
                         max_size=MAX_DIRECTORY_BYTES, max_queue=16,
                         write_limit=65536, ping_interval=20, ping_timeout=20,
                         close_timeout=3, open_timeout=10, server_header=None)
    log.info("WebSocket entry on %s:%d (TLS terminates at the reverse proxy)", bind, server.sockets[0].getsockname()[1])
    return server
