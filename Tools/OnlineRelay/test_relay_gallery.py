"""みんなのカラー on the relay: posting (with and without paint), the list's pages and orders, paint pieces, saves,
deleting, the limits, and the posts kept across a restart.
    python -m unittest test_relay_gallery"""
import base64
import os
import tempfile
import time
import unittest

import ci_gallery
from ci_relay import Relay

PAINT = b"CIP1" + bytes(range(256)) * 9   # 2308 bytes: three pieces


class GalleryTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.folder = tempfile.mkdtemp()
        self.relay = Relay("127.0.0.1", [], os.path.join(self.folder, "ranks.json"))
        ci_gallery.POST_GAP, self.gap = 0.0, ci_gallery.POST_GAP

    async def asyncTearDown(self):
        ci_gallery.POST_GAP = self.gap

    async def ask(self, **request):
        return await self.relay.handle(request, ("127.0.0.1", 1))

    async def post(self, who, title, chara=2, parts="ff000001", paint=b""):
        reply = await self.ask(op="gallery_post", token="t-" + who, name=who, title=title, chara=chara, parts=parts, paint=len(paint))
        self.assertTrue(reply["ok"], reply)
        if paint:
            offset = 0
            while True:
                piece = paint[offset:offset + ci_gallery.PIECE]
                answer = await self.ask(op="gallery_piece", token="t-" + who, upload=reply["upload"], offset=offset,
                                        data=base64.b64encode(piece).decode())
                self.assertTrue(answer["ok"], answer)
                if answer["done"]:
                    break
                offset = answer["next"]
        return reply["id"]

    async def test_post_list_and_details(self):
        first = await self.post("あお", "赤いスマイリー", chara=0)
        await self.post("みどり", "みどりのやつ", chara=3)
        listing = await self.ask(op="gallery_list", token="t-あお")
        self.assertEqual(listing["total"], 2)
        newest = listing["posts"][0]
        self.assertEqual(newest["title"], "みどりのやつ", "newest first")
        mine = next(post for post in listing["posts"] if post["id"] == first)
        self.assertTrue(mine["mine"])
        self.assertEqual(mine["poster"], "あお")
        self.assertEqual(mine["parts"], "ff000001")
        self.assertAlmostEqual(mine["at"], time.time(), delta=5)
        only = await self.ask(op="gallery_list", chara=3)
        self.assertEqual([post["chara"] for post in only["posts"]], [3])

    async def test_paint_up_and_down_in_pieces(self):
        post_id = await self.post("あお", "ペイント入り", paint=PAINT)
        listing = await self.ask(op="gallery_list")
        self.assertEqual(listing["posts"][0]["paint"], len(PAINT))
        got, offset = b"", 0
        while True:
            piece = await self.ask(op="gallery_paint", id=post_id, offset=offset)
            data = base64.b64decode(piece["data"])
            if not data:
                break
            got += data
            offset += len(data)
            if offset >= piece["total"]:
                break
        self.assertEqual(got, PAINT)

    async def test_pieces_in_any_order(self):
        reply = await self.ask(op="gallery_post", token="t-a", name="a", title="ばらばら", chara=1, parts="", paint=len(PAINT))
        offsets = list(range(0, len(PAINT), ci_gallery.PIECE))
        answers = []
        for offset in reversed(offsets):
            answers.append(await self.ask(op="gallery_piece", token="t-a", upload=reply["upload"], offset=offset,
                                          data=base64.b64encode(PAINT[offset:offset + ci_gallery.PIECE]).decode()))
        self.assertFalse(answers[0]["done"])
        self.assertEqual(answers[0]["next"], 0, "the first piece still missing")
        self.assertTrue(answers[-1]["done"])
        piece = await self.ask(op="gallery_paint", id=reply["id"], offset=0)
        self.assertEqual(piece["total"], len(PAINT))

    async def test_unfinished_paint_is_not_listed(self):
        reply = await self.ask(op="gallery_post", token="t-a", name="a", title="途中", chara=1, parts="", paint=len(PAINT))
        self.assertIn("upload", reply)
        self.assertEqual((await self.ask(op="gallery_list"))["total"], 0)
        # Out of turn: told where to go on from.
        answer = await self.ask(op="gallery_piece", token="t-a", upload=reply["upload"], offset=900, data="AAAA")
        self.assertEqual(answer["next"], 0)
        # Someone else cannot fill it.
        self.assertFalse((await self.ask(op="gallery_piece", token="t-b", upload=reply["upload"], offset=0, data="AAAA"))["ok"])

    async def test_paint_must_be_packed_pictures(self):
        reply = await self.ask(op="gallery_post", token="t-a", name="a", title="にせもの", chara=1, parts="", paint=8)
        answer = await self.ask(op="gallery_piece", token="t-a", upload=reply["upload"], offset=0, data=base64.b64encode(b"NOTPAINT").decode())
        self.assertFalse(answer["ok"])
        self.assertEqual((await self.ask(op="gallery_list"))["total"], 0)

    async def test_pages_and_popular(self):
        ids = [await self.post("p%d" % index, "作品%d" % index) for index in range(11)]
        first = await self.ask(op="gallery_list", page=0)
        second = await self.ask(op="gallery_list", page=1)
        self.assertEqual((first["pages"], len(first["posts"]), len(second["posts"])), (2, 8, 3))
        await self.ask(op="gallery_save", token="t-x", id=ids[2])
        await self.ask(op="gallery_save", token="t-y", id=ids[2])
        await self.ask(op="gallery_save", token="t-x", id=ids[2])   # once per player
        await self.ask(op="gallery_save", token="t-x", id=ids[5])
        popular = await self.ask(op="gallery_list", sort="saves", token="t-x")
        self.assertEqual([post["id"] for post in popular["posts"][:2]], [ids[2], ids[5]])
        self.assertEqual(popular["posts"][0]["saves"], 2)
        self.assertTrue(popular["posts"][0]["saved"])
        # The poster saving their own does not count.
        self.assertEqual((await self.ask(op="gallery_save", token="t-p2", id=ids[2]))["saves"], 2)

    async def test_mine_and_delete(self):
        mine = await self.post("あお", "わたしの")
        await self.post("ほか", "ほかの")
        listing = await self.ask(op="gallery_list", token="t-あお", mine=True)
        self.assertEqual([post["id"] for post in listing["posts"]], [mine])
        self.assertEqual((await self.ask(op="gallery_delete", token="t-ほか", id=mine))["error"], "not yours")
        self.assertTrue((await self.ask(op="gallery_delete", token="t-あお", id=mine))["ok"])
        self.assertEqual((await self.ask(op="gallery_list"))["total"], 1)

    async def test_limits(self):
        self.assertEqual((await self.ask(op="gallery_post", token="t-a", name="a", title="", chara=1, parts=""))["error"], "bad request")
        self.assertEqual((await self.ask(op="gallery_post", token="t-a", name="a", title="x", chara=1, parts="zz"))["error"], "bad request")
        self.assertEqual((await self.ask(op="gallery_post", token="t-a", name="a", title="x", chara=1, parts="",
                                         paint=ci_gallery.PAINT_MAX + 1))["error"], "too big")
        long = await self.post("a", "とても長い作品の名前をつけてみたらどうなるでしょうか")
        listing = await self.ask(op="gallery_list")
        self.assertEqual(len(listing["posts"][0]["title"]), ci_gallery.TITLE_MAX)
        self.assertTrue(long)
        ci_gallery.POST_GAP = 60.0
        self.assertEqual((await self.ask(op="gallery_post", token="t-a", name="a", title="すぐ", chara=1, parts=""))["error"], "too fast")
        ci_gallery.POST_GAP = 0.0
        for index in range(ci_gallery.PER_PLAYER - 1):
            await self.post("a", "まだ%d" % index)
        self.assertEqual((await self.ask(op="gallery_post", token="t-a", name="a", title="多すぎ", chara=1, parts=""))["error"], "too many")

    async def test_kept_across_a_restart(self):
        post_id = await self.post("あお", "のこる", paint=PAINT)
        again = Relay("127.0.0.1", [], os.path.join(self.folder, "ranks.json"))
        listing = await again.handle({"op": "gallery_list"}, ("127.0.0.1", 1))
        self.assertEqual(listing["posts"][0]["id"], post_id)
        piece = await again.handle({"op": "gallery_paint", "id": post_id, "offset": 0}, ("127.0.0.1", 1))
        self.assertEqual(piece["total"], len(PAINT))

    async def test_reply_fits_one_datagram(self):
        import json
        for index in range(8):
            await self.post("ながいなまえのひと%d" % index, "とても長い作品の名前ですよ%d" % index, parts="ff" * 32)
        listing = await self.ask(op="gallery_list", token="t-x", rid=123)
        self.assertLess(len(json.dumps(listing, ensure_ascii=False).encode("utf-8")), 4000)
        piece = await self.ask(op="gallery_paint", id="nothing", offset=0)
        self.assertFalse(piece["ok"])


if __name__ == "__main__":
    unittest.main()
