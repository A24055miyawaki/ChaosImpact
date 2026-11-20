"""ランダムマッチ ratings on the relay (1v1 and 8 players apart): 1500 to start, 計測中 for the first 5 games, the floor at
500, the same opponent counting every time, both 1v1 reports needed (or one, once the other has gone quiet), the
8-player places paired up, and the win streaks in the records.
    python -m unittest test_relay_rating"""
import os
import socket
import tempfile
import unittest

import ci_relay
from ci_relay import Relay


class RatingTests(unittest.IsolatedAsyncioTestCase):
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

    async def join(self, name, mode="duel", players=1, rtt=20):
        reply = await self.ask(op="mm_join", ver="test", mode=mode, token="token-" + name, name=name, players=players, rtt=rtt,
                               profile={"chara": 1, "color": 0, "device": "pad"})
        self.assertTrue(reply["ok"], reply)
        return reply["ticket"]

    async def duel(self, a="A", b="B"):
        ta, tb = await self.join(a), await self.join(b)
        await self.relay.matchmake()
        await self.ask(op="mm_accept", ticket=ta, accept=True)
        await self.ask(op="mm_accept", ticket=tb, accept=True)
        await self.relay.matchmake()
        return (await self.ask(op="mm_poll", ticket=ta))["match"]

    async def report(self, match, who, game, **result):
        return await self.ask(op="record_report", match=match, token="token-" + who, game=game, **result)

    async def play_duel(self, match, game, winner, loser):
        first = await self.report(match, winner, game, result="win")
        second = await self.report(match, loser, game, result="lose")
        return first, second

    async def test_measuring_then_shown(self):
        match = await self.duel()
        info = await self.ask(op="record_info", token="token-A", name="A")
        self.assertFalse(info["duel"]["measured"])
        self.assertEqual(info["duel"]["measuring_left"], 5)
        self.assertEqual(info["duel"]["rate"], 0, "not shown while measuring")
        for game in range(4):
            await self.play_duel(match, game, "A", "B")
        info = await self.ask(op="record_info", token="token-A", name="A")
        self.assertEqual(info["duel"]["measuring_left"], 1)
        self.assertFalse(info["duel"]["measured"])
        last, _ = await self.play_duel(match, 4, "A", "B")
        again = await self.report(match, "A", 4, result="win")
        self.assertEqual(again["rating"]["state"], "done")
        self.assertTrue(again["rating"]["measured"])
        self.assertTrue(again["rating"]["just_measured"])
        info = await self.ask(op="record_info", token="token-A", name="A")
        self.assertTrue(info["duel"]["measured"])
        self.assertGreater(info["duel"]["rate"], 1500)
        self.assertEqual(info["duel"]["best"], info["duel"]["rate"])
        self.assertEqual(info["duel"]["streak"], 5)
        self.assertEqual(info["duel"]["best_streak"], 5)
        self.assertEqual(len(info["duel"]["history"]), 5)
        # 8-player rating untouched.
        self.assertEqual(info["ffa"]["rated_games"], 0)
        del last

    async def test_first_game_change_and_pending(self):
        match = await self.duel()
        first = await self.report(match, "A", 0, result="win")
        self.assertEqual(first["rating"]["state"], "pending", "waits for the other report")
        second = await self.report(match, "B", 0, result="lose")
        self.assertEqual(second["rating"]["state"], "done")
        self.assertEqual(second["rating"]["before"], 1500)
        self.assertEqual(second["rating"]["delta"], -60, "equal ratings, K 120 while measuring: half of it")
        winner = await self.report(match, "A", 0, result="win")
        self.assertEqual(winner["rating"]["delta"], 60)

    async def test_same_opponent_always_counts(self):
        match = await self.duel()
        deltas = []
        for game in range(14):
            first, second = await self.play_duel(match, game, "A", "B")
            done = await self.report(match, "A", game, result="win")
            deltas.append(done["rating"]["delta"])
        self.assertTrue(all(delta > 0 for delta in deltas), deltas)
        self.assertGreater(deltas[-1], 0, "no daily cap and no same-pair cut")

    async def test_win_streak_bonus(self):
        """1v1: the 2nd win in a row gains 1.1 times, the 3rd 1.2, the 4th 1.3, the 5th and on twice; a loss is not changed."""
        match = await self.duel()
        for game in range(6):
            await self.play_duel(match, game, "A", "B")
        a, b = ci_relay.token_id("token-A"), ci_relay.token_id("token-B")
        gains = []
        for game in range(6, 12):
            # Equal ratings each time (so only the bonus changes the gain): K 60 after measuring, half of it.
            for pid in (a, b):
                self.relay.ranks.rating(pid, "duel")["rate"] = 1500.0
            self.relay.ranks.records(a)[0]["streak"] = game - 6
            await self.play_duel(match, game, "A", "B")
            gains.append((await self.report(match, "A", game, result="win"))["rating"]["delta"])
        self.assertEqual(gains, [30, 33, 36, 39, 60, 60])
        # The rating told back says the streak and its bonus (the results show it).
        told = await self.report(match, "A", 11, result="win")
        self.assertEqual((told["rating"]["streak"], told["rating"]["streak_bonus"]), (6, 2.0))
        first = await self.report(match, "A", 6, result="win")
        self.assertNotIn("streak_bonus", first["rating"], "the first win: no bonus told")
        # The loser's change is the plain one.
        for pid in (a, b):
            self.relay.ranks.rating(pid, "duel")["rate"] = 1500.0
        await self.play_duel(match, 12, "A", "B")
        lost = await self.report(match, "B", 12, result="lose")
        self.assertEqual(lost["rating"]["delta"], -30)
        # A loss ends the streak: the next win is plain again.
        for pid in (a, b):
            self.relay.ranks.rating(pid, "duel")["rate"] = 1500.0
        await self.play_duel(match, 13, "B", "A")
        for pid in (a, b):
            self.relay.ranks.rating(pid, "duel")["rate"] = 1500.0
        await self.play_duel(match, 14, "A", "B")
        self.assertEqual((await self.report(match, "A", 14, result="win"))["rating"]["delta"], 30)

    async def test_rounds_margin(self):
        """1v1: a 2-0 moves both ratings by 1.25 times, a 2-1 by 0.75; rounds not said (an older game) or not agreeing: as before."""
        match = await self.duel()
        for game in range(6):
            await self.play_duel(match, game, "A", "B")
        a, b = ci_relay.token_id("token-A"), ci_relay.token_id("token-B")

        async def game_with(game, a_says, b_says):
            for pid in (a, b):
                self.relay.ranks.rating(pid, "duel")["rate"] = 1500.0
                self.relay.ranks.records(pid)[0]["streak"] = 0
            await self.report(match, "A", game, result="win", **({"detail": {"score": a_says}} if a_says else {}))
            await self.report(match, "B", game, result="lose", **({"detail": {"score": b_says}} if b_says else {}))
            return ((await self.report(match, "A", game, result="win"))["rating"]["delta"],
                    (await self.report(match, "B", game, result="lose"))["rating"]["delta"])
        self.assertEqual(await game_with(6, [2, 0], [0, 2]), (38, -38), "a straight win: 30 x 1.25 (37.5 shown as 38)")
        self.assertEqual(await game_with(7, [2, 1], [1, 2]), (22, -22), "a close win: 30 x 0.75 (22.5 shown as 22)")
        self.assertEqual(await game_with(8, None, None), (30, -30), "no rounds said: as before")
        self.assertEqual(await game_with(9, [2, 0], None), (38, -38), "one side's rounds stand")
        self.assertEqual(await game_with(10, [2, 0], [1, 2]), (30, -30), "not agreeing: as before")
        self.assertEqual(await game_with(11, [0, 2], None), (30, -30), "rounds against the result: as before")

    async def test_upset(self):
        """1v1: even ratings ±30; the further below the winner was, the more: about +46 at 100, +78 at 200, +150 at 400 (the
        favourite losing the same)."""
        match = await self.duel()
        for game in range(6):
            await self.play_duel(match, game, "A", "B")
        a, b = ci_relay.token_id("token-A"), ci_relay.token_id("token-B")
        seen = []
        for game, gap in enumerate((0, 100, 200, 400, -400), start=6):
            self.relay.ranks.rating(a, "duel")["rate"] = 1500.0
            self.relay.ranks.rating(b, "duel")["rate"] = 1500.0 + gap
            for pid in (a, b):
                self.relay.ranks.records(pid)[0]["streak"] = 0
            await self.play_duel(match, game, "A", "B")
            seen.append(((await self.report(match, "A", game, result="win"))["rating"]["delta"],
                         (await self.report(match, "B", game, result="lose"))["rating"]["delta"]))
        self.assertEqual(seen[0], (30, -30), "even: as before")
        self.assertEqual(seen[1], (46, -46))
        self.assertEqual(seen[2], (78, -78))
        self.assertEqual(seen[3], (120, -120), "a big upset (about 150, held to 120)")
        self.assertEqual(seen[4], (5, -5), "the favourite winning: as before (no bonus)")

    async def test_caps(self):
        """1v1: the result and the upset move at most 120; with a 2-0 (x1.25) and the 5th win in a row (x2): at most 300."""
        match = await self.duel()
        for game in range(6):
            await self.play_duel(match, game, "A", "B")
        a, b = ci_relay.token_id("token-A"), ci_relay.token_id("token-B")

        async def upset(game, streak, rounds):
            self.relay.ranks.rating(a, "duel")["rate"] = 1000.0
            self.relay.ranks.rating(b, "duel")["rate"] = 2000.0
            self.relay.ranks.records(a)[0]["streak"] = streak - 1
            self.relay.ranks.records(b)[0]["streak"] = 0
            detail = {"detail": {"score": rounds}} if rounds else {}
            await self.report(match, "A", game, result="win", **detail)
            await self.report(match, "B", game, result="lose")
            return ((await self.report(match, "A", game, result="win"))["rating"]["delta"],
                    (await self.report(match, "B", game, result="lose"))["rating"]["delta"])
        self.assertEqual(await upset(6, 1, None), (120, -120), "1000 below: held to 120")
        self.assertEqual(await upset(7, 1, [2, 0]), (150, -150), "a 2-0: 120 x 1.25")
        self.assertEqual(await upset(8, 5, [2, 0]), (300, -150), "5th in a row: 120 x 1.25 x 2 = 300 (the loser: no streak)")
        self.assertEqual(await upset(9, 5, [2, 1]), (180, -90), "a 2-1 in a streak: 120 x 0.75 x 2")

    async def test_floor(self):
        match = await self.duel()
        for game in range(6):
            await self.play_duel(match, game, "A", "B")
        # B close to the floor: a loss stops at 500 and says so.
        self.relay.ranks.rating(ci_relay.token_id("token-B"), "duel")["rate"] = 505.0
        self.relay.ranks.rating(ci_relay.token_id("token-A"), "duel")["rate"] = 505.0
        await self.play_duel(match, 6, "A", "B")
        last = await self.report(match, "B", 6, result="lose")
        self.assertEqual(last["rating"]["after"], 500)
        self.assertTrue(last["rating"]["floor"])
        await self.play_duel(match, 7, "A", "B")
        lower = await self.report(match, "B", 7, result="lose")
        self.assertEqual((lower["rating"]["after"], lower["rating"]["delta"]), (500, 0))
        info = await self.ask(op="record_info", token="token-B", name="B")
        self.assertEqual(info["duel"]["rate"], 500)
        self.assertEqual(info["duel"]["streak"], 0)

    async def test_disagreeing_reports_do_not_count(self):
        match = await self.duel()
        await self.report(match, "A", 0, result="win")
        both = await self.report(match, "B", 0, result="win")
        self.assertEqual(both["rating"]["state"], "void")
        info = await self.ask(op="record_info", token="token-A", name="A")
        self.assertEqual(info["duel"]["rated_games"], 0)

    async def test_one_report_stands_after_a_while(self):
        match = await self.duel()
        await self.report(match, "A", 0, result="win")
        rated = self.relay.matches[match].games[0]
        rated["reports"] = {pid: (value[0], value[1] - ci_relay.REPORT_WAIT - 1) for pid, value in rated["reports"].items()}
        # The room is gone in the test, so the sweep's own pass is run by hand.
        self.relay.settle_game(self.relay.matches[match], 0)
        done = await self.report(match, "A", 0, result="win")
        self.assertEqual(done["rating"]["state"], "done")
        self.assertGreater(done["rating"]["delta"], 0)
        info = await self.ask(op="record_info", token="token-B", name="B")
        self.assertEqual(info["duel"]["rated_games"], 1, "the one who never said lost")

    async def test_eight_players(self):
        tickets = [await self.join(name, mode="random") for name in "PQRSTU"]
        self.relay.gather_since["test"] = -1000.0
        await self.relay.matchmake()
        match = (await self.ask(op="mm_poll", ticket=tickets[0]))["match"]
        names = "PQRSTU"
        for place, name in enumerate(names[:5], start=1):
            reply = await self.report(match, name, 0, place=place, of=6)
            self.assertEqual(reply["rating"]["state"], "pending")
        last = await self.report(match, "U", 0, place=6, of=6)
        self.assertEqual(last["rating"]["state"], "done")
        changes = {}
        for name in names:
            changes[name] = (await self.report(match, name, 0, place=1, of=6))["rating"]["delta"]
        self.assertGreater(changes["P"], 0)
        self.assertLess(changes["U"], 0)
        self.assertEqual(changes["P"], -changes["U"])
        self.assertTrue(changes["P"] > changes["Q"] > changes["R"])
        self.assertAlmostEqual(sum(changes.values()), 0, delta=3)
        info = await self.ask(op="record_info", token="token-P", name="P")
        self.assertEqual(info["ffa"]["streak"], 1)
        self.assertEqual(info["duel"]["rated_games"], 0, "the 1v1 rating apart")
        self.assertEqual(info["ffa"]["rated_games"], 1)

    async def test_eight_players_someone_left(self):
        tickets = [await self.join(name, mode="random") for name in "VWX"]
        self.relay.gather_since["test"] = -1000.0
        await self.relay.matchmake()
        match = (await self.ask(op="mm_poll", ticket=tickets[0]))["match"]
        await self.report(match, "V", 0, place=2, of=3)
        await self.report(match, "W", 0, place=1, of=3)
        rated = self.relay.matches[match].games[0]
        rated["reports"] = {pid: (value[0], value[1] - ci_relay.FFA_REPORT_WAIT - 1, value[2]) for pid, value in rated["reports"].items()}
        self.relay.settle_game(self.relay.matches[match], 0)
        info = await self.ask(op="record_info", token="token-X", name="X")
        self.assertEqual(info["ffa"]["rated_games"], 1, "who never said is last")
        self.assertEqual(info["ffa"]["deltas"], [], "still measuring: nothing shown")

    async def test_kept_across_a_restart(self):
        match = await self.duel()
        await self.play_duel(match, 0, "A", "B")
        again = Relay("127.0.0.1", [], os.path.join(self.folder, "ranks.json"))
        info = await again.handle({"op": "record_info", "token": "token-A", "name": "A"}, ("127.0.0.1", 1))
        self.assertEqual(info["duel"]["rated_games"], 1)

    async def test_offer_shows_the_opponents_rating(self):
        match = await self.duel()
        for game in range(5):
            await self.play_duel(match, game, "A", "B")
        ta, tc = await self.join("A"), await self.join("C")
        await self.relay.matchmake()
        offer = await self.ask(op="mm_poll", ticket=tc)
        self.assertEqual(offer["state"], "proposed")
        self.assertTrue(offer["opponent"]["record"]["measured"])
        self.assertEqual(offer["opponent"]["record"]["streak"], 5)
        self.assertGreater(offer["opponent"]["record"]["rate"], 1500)
        del ta


if __name__ == "__main__":
    unittest.main()
