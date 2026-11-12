"""
みんなのカラー (the gallery): custom colours players post from the game, for everyone to look at and keep. Part of the
relay (ci_relay.py hands it every request whose op starts with "gallery_"); the posts are kept in one JSON file and
each one's paint (the packed PNGs, "CIP1...") in a file of its own beside it.

  gallery_list   {token, page, sort:"new"|"saves", chara:-1|n, mine:bool}
                 -> {ok, total, page, pages, posts:[{id, title, poster, at, chara, parts, paint, saves, mine, saved}]}
  gallery_post   {token, name, title, chara, parts, paint}     paint: the paint's size in bytes (0: none)
                 -> {ok, id} (no paint: listed now) | {ok, id, upload} (listed once the paint has all come)
  gallery_piece  {token, upload, offset, data}   data: base64, PIECE bytes (the last one fewer), any order
                 -> {ok, next (the first piece still missing), done}
  gallery_paint  {id, offset}                    -> {ok, total, offset, data}      (a piece of a post's paint)
  gallery_save   {token, id}                     -> {ok, saves}   (counted once per player)
  gallery_delete {token, id}                     -> {ok}          (the poster's own only)
  Errors: {ok:false, error:"bad request"|"unknown post"|"too many"|"too fast"|"full"|"not yours"|"unknown upload"}

Standard library only (Python 3.8+).
"""
import base64
import hashlib
import json
import logging
import os
import secrets
import time
import unicodedata

PAGE = 8                       # posts in one page of the list
MAX_POSTS = 5000               # the whole gallery (the oldest go first when it is full)
PER_PLAYER = 30                # one player's posts at once
POST_GAP = 15.0                # seconds between one player's posts
PAINT_MAX = 640 * 1024         # a post's paint, packed (the game keeps a preset's pictures under 600 KB)
PIECE = 900                    # raw bytes in one piece of paint (under 1300 bytes as JSON: one UDP datagram)
UPLOAD_KEEP = 300.0            # an upload not finished this long is dropped
TITLE_MAX = 16
NAME_MAX = 16
PARTS_MAX = 64                 # hex digits: the game's 4 bytes per part, up to 8 parts

log = logging.getLogger("ci_relay.gallery")


def token_id(token):
    return hashlib.sha256(str(token).encode("utf-8")).hexdigest()[:32]


def clean_text(value, limit):
    """Printable text only (no control characters, no line breaks), trimmed and cut to the limit."""
    text = "".join(ch for ch in str(value or "") if unicodedata.category(ch)[0] != "C")
    return text.strip()[:limit]


def clean_parts(value):
    parts = str(value or "").lower()
    if len(parts) > PARTS_MAX or len(parts) % 2 or any(ch not in "0123456789abcdef" for ch in parts):
        return None
    return parts


def number(value, low, high, default):
    try:
        return max(low, min(high, int(value)))
    except (TypeError, ValueError):
        return default


class Gallery:
    def __init__(self, folder):
        """folder: where the posts are kept (None: in memory only, for tests)."""
        self.folder = folder
        self.posts = []               # newest last
        self.uploads = {}             # upload id -> {post, pid, bytes, total, at}
        self.last_post = {}           # pid -> when they last posted
        if folder:
            os.makedirs(os.path.join(folder, "paint"), exist_ok=True)
            index = os.path.join(folder, "gallery.json")
            if os.path.exists(index):
                try:
                    with open(index, "r", encoding="utf-8") as source:
                        loaded = json.load(source)
                    self.posts = [post for post in loaded.get("posts", []) if isinstance(post, dict) and post.get("id")]
                    log.info("gallery loaded: %d posts", len(self.posts))
                except (OSError, ValueError) as error:
                    log.error("gallery index unreadable (%s): starting empty, the old file is kept as .bad", error)
                    try:
                        os.replace(index, index + ".bad")
                    except OSError:
                        pass

    # ---- storage
    def save(self):
        if not self.folder:
            return
        index = os.path.join(self.folder, "gallery.json")
        temporary = index + ".tmp"
        with open(temporary, "w", encoding="utf-8") as target:
            json.dump({"posts": self.posts}, target, ensure_ascii=False)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, index)

    def paint_path(self, post_id):
        return os.path.join(self.folder, "paint", post_id + ".cip") if self.folder else None

    def write_paint(self, post, data):
        if self.folder:
            path = self.paint_path(post["id"])
            with open(path + ".tmp", "wb") as target:
                target.write(data)
            os.replace(path + ".tmp", path)
        else:
            post["_paint"] = bytes(data)

    def read_paint(self, post):
        if not self.folder:
            return post.get("_paint", b"")
        try:
            with open(self.paint_path(post["id"]), "rb") as source:
                return source.read()
        except OSError:
            return b""

    def forget_paint(self, post):
        if self.folder:
            try:
                os.remove(self.paint_path(post["id"]))
            except OSError:
                pass

    def find(self, post_id):
        return next((post for post in self.posts if post["id"] == post_id), None)

    # ---- requests
    def handle(self, request):
        op = request.get("op")
        self.sweep()
        if op == "gallery_list":
            return self.list(request)
        if op == "gallery_paint":
            return self.paint(request)
        if not request.get("token"):
            return {"ok": False, "error": "bad request"}
        pid = token_id(request["token"])
        if op == "gallery_post":
            return self.post(request, pid)
        if op == "gallery_piece":
            return self.piece(request, pid)
        if op == "gallery_save":
            return self.keep(request, pid)
        if op == "gallery_delete":
            return self.delete(request, pid)
        return {"ok": False, "error": "unknown op"}

    def describe(self, post, pid):
        return {"id": post["id"], "title": post["title"], "poster": post["poster"], "at": post["at"], "chara": post["chara"],
                "parts": post["parts"], "paint": post["paint"], "saves": len(post["savers"]),
                "mine": post["pid"] == pid, "saved": pid in post["savers"]}

    def list(self, request):
        pid = token_id(request["token"]) if request.get("token") else ""
        chara = number(request.get("chara"), -1, 63, -1)
        # (Kept oldest first: walked backwards, the newest come first; the popular order keeps that among equals.)
        shown = [post for post in reversed(self.posts) if post["listed"] and (chara < 0 or post["chara"] == chara)
                 and (not request.get("mine") or post["pid"] == pid)]
        if request.get("sort") == "saves":
            shown.sort(key=lambda post: len(post["savers"]), reverse=True)
        pages = max(1, (len(shown) + PAGE - 1) // PAGE)
        page = number(request.get("page"), 0, pages - 1, 0)
        return {"ok": True, "total": len(shown), "page": page, "pages": pages,
                "posts": [self.describe(post, pid) for post in shown[page * PAGE:(page + 1) * PAGE]]}

    def post(self, request, pid):
        title = clean_text(request.get("title"), TITLE_MAX)
        poster = clean_text(request.get("name"), NAME_MAX) or "ななし"
        parts = clean_parts(request.get("parts"))
        chara = number(request.get("chara"), -1, 63, -1)
        paint = number(request.get("paint", 0), -1, PAINT_MAX + 1, -1)
        if not title or parts is None or chara < 0 or paint < 0:
            return {"ok": False, "error": "bad request"}
        if paint > PAINT_MAX:
            return {"ok": False, "error": "too big"}
        now = time.time()
        if now - self.last_post.get(pid, 0.0) < POST_GAP:
            return {"ok": False, "error": "too fast"}
        if sum(1 for post in self.posts if post["pid"] == pid) >= PER_PLAYER:
            return {"ok": False, "error": "too many"}
        while len(self.posts) >= MAX_POSTS:
            oldest = self.posts.pop(0)
            self.forget_paint(oldest)
        self.last_post[pid] = now
        post = {"id": secrets.token_hex(5), "pid": pid, "title": title, "poster": poster, "at": int(now), "chara": chara,
                "parts": parts, "paint": paint, "savers": [], "listed": paint == 0}
        self.posts.append(post)
        if paint == 0:
            self.save()
            log.info("gallery: %s posted \"%s\" (%s)", poster, title, post["id"])
            return {"ok": True, "id": post["id"]}
        upload = secrets.token_hex(8)
        self.uploads[upload] = {"post": post["id"], "pid": pid, "pieces": {}, "total": paint, "at": time.monotonic()}
        return {"ok": True, "id": post["id"], "upload": upload}

    def piece(self, request, pid):
        upload = self.uploads.get(str(request.get("upload", "")))
        if upload is None or upload["pid"] != pid:
            return {"ok": False, "error": "unknown upload"}
        offset = number(request.get("offset"), 0, PAINT_MAX, -1)
        try:
            data = base64.b64decode(str(request.get("data", "")), validate=True)
        except ValueError:
            return {"ok": False, "error": "bad request"}
        pieces = upload["pieces"]
        total = upload["total"]
        # Pieces may come in any order (several are sent at once); each where it belongs, a whole piece but the last.
        if 0 <= offset < total and offset % PIECE == 0 and data and (len(data) == PIECE or offset + len(data) == total):
            pieces[offset] = data
            upload["at"] = time.monotonic()
        missing = next((at for at in range(0, total, PIECE) if at not in pieces), None)
        if missing is not None:
            return {"ok": True, "next": missing, "done": False}
        received = b"".join(pieces[at] for at in range(0, total, PIECE))
        del self.uploads[str(request["upload"])]
        post = self.find(upload["post"])
        if post is None:
            return {"ok": False, "error": "unknown post"}
        if bytes(received[:4]) != b"CIP1":
            self.posts.remove(post)
            return {"ok": False, "error": "bad request"}
        self.write_paint(post, bytes(received))
        post["listed"] = True
        self.save()
        log.info("gallery: %s posted \"%s\" (%s, paint %d bytes)", post["poster"], post["title"], post["id"], post["paint"])
        return {"ok": True, "next": len(received), "done": True, "id": post["id"]}

    def paint(self, request):
        post = self.find(str(request.get("id", "")))
        if post is None or not post["listed"]:
            return {"ok": False, "error": "unknown post"}
        data = self.read_paint(post)
        offset = number(request.get("offset"), 0, len(data), 0)
        return {"ok": True, "total": len(data), "offset": offset,
                "data": base64.b64encode(data[offset:offset + PIECE]).decode("ascii")}

    def keep(self, request, pid):
        post = self.find(str(request.get("id", "")))
        if post is None or not post["listed"]:
            return {"ok": False, "error": "unknown post"}
        if pid not in post["savers"] and post["pid"] != pid:
            post["savers"].append(pid)
            self.save()
        return {"ok": True, "saves": len(post["savers"])}

    def delete(self, request, pid):
        post = self.find(str(request.get("id", "")))
        if post is None:
            return {"ok": False, "error": "unknown post"}
        if post["pid"] != pid:
            return {"ok": False, "error": "not yours"}
        self.posts.remove(post)
        self.forget_paint(post)
        self.save()
        log.info("gallery: \"%s\" (%s) deleted by its poster", post["title"], post["id"])
        return {"ok": True}

    def sweep(self):
        now = time.monotonic()
        for upload_id, upload in list(self.uploads.items()):
            if now - upload["at"] > UPLOAD_KEEP:
                del self.uploads[upload_id]
                post = self.find(upload["post"])
                if post is not None and not post["listed"]:
                    self.posts.remove(post)
