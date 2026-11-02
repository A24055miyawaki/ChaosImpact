"""ランダムマッチ 1v1 on the relay: a match found is only proposed until both accept, and the records it keeps.
    python -m unittest test_relay_duel"""
import socket
import unittest

from ci_relay import Relay


class DuelTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        ports = []
        for _ in range(3):
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(("127.0.0.1", 0))
                ports.append(probe.getsockname()[1])
        self.relay = Relay("127.0.0.1", ports, None)

    async def asyncTearDown(self):
        for room in list(self.relay.rooms.values()):
            self.relay.remove(room, "test cleanup")

    async def join(self, name, rtt, mode="duel", players=1):
        reply = await self.relay.handle({"op": "mm_join", "ver": "test", "mode": mode, "token": "token-" + name,
                                         "name": name, "players": players, "rtt": rtt,
                                         "profile": {"chara": 3, "color": 1, "device": "key"}}, ("127.0.0.1", 1))
        self.assertTrue(reply["ok"], reply)
        return reply["ticket"]

    async def poll(self, ticket):
        return await self.relay.handle({"op": "mm_poll", "ticket": ticket}, ("127.0.0.1", 1))

    async def accept(self, ticket, accept=True):
        return await self.relay.handle({"op": "mm_accept", "ticket": ticket, "accept": accept}, ("127.0.0.1", 1))

    async def test_pair_only_after_both_accept(self):
        a = await self.join("A", 30)
        b = await self.join("B", 20)
        await self.relay.matchmake()
        offer = await self.poll(a)
        self.assertEqual(offer["state"], "proposed")
        self.assertEqual(offer["opponent"]["name"], "B")
        self.assertEqual(offer["opponent"]["chara"], 3)
        self.assertEqual(offer["opponent"]["device"], "key")
        self.assertEqual(offer["ping"], 50)
        self.assertFalse(self.relay.rooms, "no room before both accept")
        await self.accept(a)
        await self.relay.matchmake()
        self.assertFalse(self.relay.rooms)
        self.assertTrue((await self.poll(b))["opponent_accepted"])
        await self.accept(b)
        await self.relay.matchmake()
        self.assertEqual(len(self.relay.rooms), 1)
        guest = await self.poll(a)
        host = await self.poll(b)
        self.assertEqual(guest["state"], "matched")
        self.assertEqual(guest["role"], "guest")
        self.assertEqual(host["role"], "host", "the quicker way to the relay hosts")
        self.assertIn("key", host)

    async def test_decline_ends_one_search_and_keeps_the_other(self):
        a = await self.join("A", 30)
        b = await self.join("B", 20)
        await self.relay.matchmake()
        await self.accept(a)
        await self.accept(b, False)
        self.assertEqual((await self.poll(b))["state"], "expired")
        waiting = await self.poll(a)
        self.assertEqual(waiting["state"], "waiting")
        self.assertEqual(waiting["notice"], "declined")
        self.assertEqual((await self.poll(a))["notice"], "", "the notice is said once")
        # B searches again at once: A is not offered B again for a while, but a third player is.
        b2 = await self.join("B", 20)
        await self.relay.matchmake()
        self.assertEqual((await self.poll(a))["state"], "waiting")
        c = await self.join("C", 40)
        await self.relay.matchmake()
        self.assertEqual((await self.poll(a))["opponent"]["name"], "C")
        self.assertEqual((await self.poll(b2))["state"], "waiting")
        self.assertEqual((await self.poll(c))["state"], "proposed")

    async def test_time_running_out_stops_who_did_not_accept(self):
        a = await self.join("A", 30)
        b = await self.join("B", 20)
        await self.relay.matchmake()
        self.assertEqual((await self.poll(a))["left"], 15.0, "the time runs once both have seen it")
        self.assertEqual((await self.poll(b))["state"], "proposed")
        self.assertTrue(all(match.deadline is not None for match in self.relay.matches.values()))
        await self.accept(a)
        for match in self.relay.matches.values():
            match.deadline = 0.0
        await self.relay.matchmake()
        self.assertEqual((await self.poll(b))["state"], "expired")
        self.assertEqual((await self.poll(a))["notice"], "declined")

    async def test_one_who_never_sees_it_is_not_waited_for(self):
        a = await self.join("A", 30)
        b = await self.join("B", 20)
        await self.relay.matchmake()
        await self.poll(a)
        for match in self.relay.matches.values():
            match.created -= 30.0
        await self.relay.matchmake()
        self.assertEqual((await self.poll(a))["notice"], "declined")
        self.assertEqual((await self.poll(b))["state"], "expired")

    async def test_leaving_during_the_offer(self):
        a = await self.join("A", 30)
        b = await self.join("B", 20)
        await self.relay.matchmake()
        await self.relay.handle({"op": "mm_leave", "ticket": b}, ("127.0.0.1", 1))
        await self.relay.matchmake()
        self.assertEqual((await self.poll(a))["notice"], "declined")

    async def test_duel_is_one_player_only(self):
        reply = await self.relay.handle({"op": "mm_join", "ver": "test", "mode": "duel", "token": "t", "name": "P",
                                         "players": 2}, ("127.0.0.1", 1))
        self.assertFalse(reply["ok"])

    async def test_records(self):
        a = await self.join("A", 30)
        b = await self.join("B", 20)
        await self.relay.matchmake()
        await self.accept(a)
        await self.accept(b)
        await self.relay.matchmake()
        match = (await self.poll(a))["match"]
        report = {"op": "record_report", "match": match, "token": "token-A", "game": 0, "result": "win"}
        done = await self.relay.handle(report, ("127.0.0.1", 1))
        self.assertEqual(done["duel"]["wins"], 1)
        again = await self.relay.handle(report, ("127.0.0.1", 1))
        self.assertEqual(again["duel"]["wins"], 1, "the same game counts once")
        rematch = await self.relay.handle(dict(report, game=1, result="lose"), ("127.0.0.1", 1))
        self.assertEqual((rematch["duel"]["wins"], rematch["duel"]["losses"]), (1, 1))
        self.assertEqual(rematch["duel"]["recent"], ["W", "L"])
        # The opponent sees it when they meet A next.
        c = await self.join("C", 10)
        a2 = await self.join("A", 30)
        await self.relay.matchmake()
        self.assertEqual((await self.poll(c))["opponent"]["record"]["wins"], 1)
        self.assertEqual((await self.poll(a2))["state"], "proposed")

        x = await self.join("X", 10, mode="random")
        y = await self.join("Y", 10, mode="random", players=2)
        self.relay.gather_since["test"] = -1000.0
        await self.relay.matchmake()
        random_match = (await self.poll(x))["match"]
        placed = await self.relay.handle({"op": "record_report", "match": random_match, "token": "token-X", "game": 0,
                                          "place": 2, "of": 8}, ("127.0.0.1", 1))
        self.assertEqual(placed["ffa"]["games"], 1)
        self.assertEqual(placed["ffa"]["top3"], 1)
        self.assertEqual(placed["ffa"]["average"], 2.0)
        self.assertEqual(placed["ffa"]["recent"], [[2, 8]])
        info = await self.relay.handle({"op": "record_info", "token": "token-X", "name": "X"}, ("127.0.0.1", 1))
        self.assertEqual(info["name"], "X")
        self.assertEqual(info["duel"]["wins"], 0)
        outsider = await self.relay.handle({"op": "record_report", "match": random_match, "token": "nobody", "game": 0,
                                            "place": 1, "of": 8}, ("127.0.0.1", 1))
        self.assertFalse(outsider["ok"])
        del y


if __name__ == "__main__":
    unittest.main()
