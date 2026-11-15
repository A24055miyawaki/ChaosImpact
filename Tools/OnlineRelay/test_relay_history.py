"""The 対戦履歴 (the last 50 games of each mode, in detail, a page of 10 at a time) and the ランキング (rated players
only, best first, ties sharing a place, 50 a page, the top 100) on the relay.
    python -m unittest test_relay_history"""
import json
import os
import socket
import tempfile
import unittest

import ci_relay
from ci_relay import Relay


class HistoryTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        ports = []
        for _ in range(3):
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(("127.0.0.1", 0))
                ports.append(probe.getsockname()[1])
        self.folder = tempfile.mkdtemp()
        self.relay = Relay("127.0.0.1", ports, os.path.join(self.folder, "ranks.json"))

    async def asyncTearDown(self):
        for room in list(self.relay.rooms.values()):
            self.relay.remove(room, "test cleanup")

    async def ask(self, **request):
        return await self.relay.handle(request, ("127.0.0.1", 1))

    async def join(self, name, mode="duel", chara=1):
        reply = await self.ask(op="mm_join", ver="test", mode=mode, token="token-" + name, name=name, players=1, rtt=20,
                               profile={"chara": chara, "color": 2, "device": "pad"})
        self.assertTrue(reply["ok"], reply)
        return reply["ticket"]

    async def duel(self, a="A", b="B"):
        ta, tb = await self.join(a, chara=3), await self.join(b, chara=5)
        await self.relay.matchmake()
        await self.ask(op="mm_accept", ticket=ta, accept=True)
        await self.ask(op="mm_accept", ticket=tb, accept=True)
        await self.relay.matchmake()
        return (await self.ask(op="mm_poll", ticket=ta))["match"]

    async def report(self, match, who, game, **result):
        return await self.ask(op="record_report", match=match, token="token-" + who, game=game, **result)

    async def play_duel(self, match, game, winner, loser, stage=2):
        chara = {"A": 3, "B": 5}
        await self.report(match, winner, game, result="win", detail={"stage": stage, "chara": chara.get(winner, 1), "color": 1, "score": [2, 1]})
        await self.report(match, loser, game, result="lose", detail={"stage": stage, "chara": chara.get(loser, 1), "color": 0, "score": [1, 2]})

    async def test_duel_history_in_detail(self):
        match = await self.duel()
        # A wins all but the first.
        for game in range(7):
            await self.play_duel(match, game, "A" if game else "B", "B" if game else "A", stage=game % 6)
        page = await self.ask(op="record_history", token="token-A", mode="duel", page=0)
        self.assertTrue(page["ok"])
        self.assertEqual(page["total"], 7)
        self.assertEqual(page["pages"], 1)
        newest = page["entries"][0]
        self.assertEqual(newest["g"], 6, "newest first")
        self.assertEqual(newest["res"], "W")
        self.assertEqual(newest["op"]["n"], "B")
        self.assertEqual(newest["op"]["ch"], 5, "the opponent's character as matched")
        self.assertEqual(newest["sc"], [2, 1])
        self.assertEqual(newest["st"], 0)
        self.assertEqual(newest["ch"], 3)
        self.assertIn("d", newest, "the rating change is filled in once settled")
        self.assertTrue(newest["ms"], "measured by the 7th game")
        self.assertIn("r", newest)
        oldest = page["entries"][-1]
        self.assertEqual(oldest["res"], "L")
        self.assertFalse(oldest["ms"], "still measuring at the first game")
        self.assertGreater(oldest["at"], 1_600_000_000)
        # The 8-player history apart.
        other = await self.ask(op="record_history", token="token-A", mode="ffa", page=0)
        self.assertEqual(other["total"], 0)
        self.assertEqual(other["entries"], [])

    async def test_fifty_kept_ten_a_page(self):
        match = await self.duel()
        for game in range(57):
            await self.play_duel(match, game, "A", "B")
        first = await self.ask(op="record_history", token="token-A", mode="duel", page=0)
        self.assertEqual(first["total"], 50)
        self.assertEqual(first["pages"], 5)
        self.assertEqual(len(first["entries"]), 10)
        self.assertEqual(first["entries"][0]["g"], 56)
        last = await self.ask(op="record_history", token="token-A", mode="duel", page=4)
        self.assertEqual(last["entries"][-1]["g"], 7, "the oldest kept")
        beyond = await self.ask(op="record_history", token="token-A", mode="duel", page=99)
        self.assertEqual(beyond["page"], 4)
        # A page stays well within what a client reads at once (16 KB).
        self.assertLess(len(json.dumps(first, ensure_ascii=False).encode("utf-8")), 6000)

    async def test_eight_player_history(self):
        names = "PQRSTU"
        tickets = [await self.join(name, mode="random") for name in names]
        self.relay.gather_since["test"] = -1000.0
        await self.relay.matchmake()
        match = (await self.ask(op="mm_poll", ticket=tickets[0]))["match"]
        players = [["あいうえおかきくけこさしすせそたち", 2, 1, 1, 1200, 0]] + [[f"CPU{n}", n, 0, n + 1, 900 - n * 50, 1] for n in range(1, 8)]
        for place, name in enumerate(names, start=1):
            await self.report(match, name, 0, place=place, of=8,
                              detail={"stage": 4, "chara": 2, "color": 1, "points": 1500 - place * 100, "kos": 7 - place, "players": players + [["extra", 0, 0, 1, 0, 0]]})
        page = await self.ask(op="record_history", token="token-P", mode="ffa", page=0)
        entry = page["entries"][0]
        self.assertEqual((entry["pl"], entry["of"]), (1, 8))
        self.assertEqual(entry["pt"], 1400)
        self.assertEqual(entry["ko"], 6)
        self.assertEqual(len(entry["ps"]), 8, "eight players at most")
        self.assertEqual(len(entry["ps"][0][0]), 16, "names cut to 16")
        self.assertEqual(entry["ps"][1][5], 1, "CPUs marked")
        self.assertIn("d", entry)

    async def test_ranking(self):
        match = await self.duel()
        for game in range(6):
            await self.play_duel(match, game, "A", "B")
        # C plays four games only: still measuring, not ranked.
        other = await self.duel("C", "D")
        for game in range(4):
            await self.play_duel(other, game, "C", "D")
        ranking = await self.ask(op="ranking", token="token-B", name="B", mode="duel", page=0)
        self.assertTrue(ranking["ok"])
        self.assertEqual(ranking["total"], 2, "the measured players only")
        self.assertEqual([row["n"] for row in ranking["entries"]], ["A", "B"])
        self.assertEqual([row["k"] for row in ranking["entries"]], [1, 2])
        self.assertTrue(ranking["entries"][1]["me"])
        self.assertEqual(ranking["entries"][0]["w"], 6)
        self.assertEqual(ranking["entries"][0]["c"], 3, "the character last played")
        self.assertEqual(ranking["me"]["rank"], 2)
        self.assertTrue(ranking["me"]["measured"])
        measuring = await self.ask(op="ranking", token="token-C", name="C", mode="duel", page=0)
        self.assertEqual(measuring["me"]["rank"], 0)
        self.assertFalse(measuring["me"]["measured"])
        self.assertEqual(measuring["me"]["left"], 1)
        empty = await self.ask(op="ranking", token="token-A", name="A", mode="ffa", page=0)
        self.assertEqual(empty["total"], 0)

    async def test_ranking_ties_and_pages(self):
        book = self.relay.ranks
        for index in range(130):
            pid = ci_relay.token_id(f"token-R{index}")
            book.player(pid, f"R{index}")
            rating = book.rating(pid, "ffa")
            rating["rate"] = float(2000 - (index // 2) * 10)
            rating["games"] = 10
        first = await self.ask(op="ranking", token="token-R5", name="R5", mode="ffa", page=0)
        self.assertEqual(first["total"], 130)
        self.assertEqual(first["pages"], 2, "the top 100, 50 a page")
        self.assertEqual(len(first["entries"]), 50)
        self.assertEqual(first["entries"][0]["k"], first["entries"][1]["k"], "the same rating, the same place")
        self.assertEqual(first["entries"][2]["k"], 3)
        second = await self.ask(op="ranking", token="token-R5", name="R5", mode="ffa", page=1)
        self.assertEqual(len(second["entries"]), 50)
        self.assertEqual(first["me"]["rank"], 5)
        far = await self.ask(op="ranking", token="token-R129", name="R129", mode="ffa", page=0)
        self.assertEqual(far["me"]["rank"], 129, "outside the top 100: still told their place")
        self.assertLess(len(json.dumps(first, ensure_ascii=False).encode("utf-8")), 14000)

    async def test_bad_requests(self):
        self.assertFalse((await self.ask(op="ranking", token="t", mode="rank"))["ok"])
        self.assertFalse((await self.ask(op="record_history", mode="duel"))["ok"])
        page = await self.ask(op="record_history", token="t", mode="duel", page="x")
        self.assertTrue(page["ok"])

    async def test_kept_across_a_restart(self):
        match = await self.duel()
        await self.play_duel(match, 0, "A", "B")
        again = Relay("127.0.0.1", [], os.path.join(self.folder, "ranks.json"))
        page = await again.handle({"op": "record_history", "token": "token-A", "mode": "duel", "page": 0}, ("127.0.0.1", 1))
        self.assertEqual(page["total"], 1)
        self.assertIn("d", page["entries"][0])


if __name__ == "__main__":
    unittest.main()
