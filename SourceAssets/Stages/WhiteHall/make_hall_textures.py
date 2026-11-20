"""
ホワイトボールハウスの大広間 (2026-11-19): the hall's textures, drawn here (no letters anywhere).
  python make_hall_textures.py      -> T_Hall_*.png beside this file
"""
import math
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
rng = np.random.default_rng(3030)


def save(name, rgb):
    a = np.clip(rgb, 0, 1)
    Image.fromarray((a * 255).astype(np.uint8)).save(os.path.join(HERE, name + ".png"))
    print(name, a.shape)


def fbm(h, w, octaves=6, seed=0, tile=True):
    """Tileable fractal noise in 0..1."""
    r = np.random.default_rng(seed)
    out = np.zeros((h, w))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        n = 4 * 2 ** o
        grid = r.random((n, n))
        img = Image.fromarray((grid * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC) if not tile else None
        if tile:
            big = np.tile(grid, (3, 3))
            im = Image.fromarray((big * 255).astype(np.uint8)).resize((w * 3, h * 3), Image.BICUBIC)
            arr = np.asarray(im, dtype=np.float64)[h:2 * h, w:2 * w] / 255.0
        else:
            arr = np.asarray(img, dtype=np.float64) / 255.0
        out += arr * amp
        total += amp
        amp *= 0.5
    return out / total


def marble(h, w, base, vein, seed, strength=1.0, scale=6.0):
    n = fbm(h, w, 7, seed)
    m = fbm(h, w, 5, seed + 3)
    y, x = np.mgrid[0:h, 0:w]
    # Two families of veins, warped hard by noise (irregular, branching), the second fainter and finer.
    t1 = (x / w * 1.0 + y / h * 0.35) * scale * 0.5 * math.pi * 2 + n * 16.0
    t2 = (x / w * 0.3 - y / h * 1.0) * scale * 0.8 * math.pi * 2 + m * 22.0
    v1 = np.exp(-np.abs(np.sin(t1)) * 18.0)
    v2 = np.exp(-np.abs(np.sin(t2)) * 28.0) * 0.45
    fade = np.clip(fbm(h, w, 3, seed + 7) * 1.8 - 0.4, 0, 1)
    veins = np.clip((v1 * fade + v2 * (1 - fade * 0.5)), 0, 1)
    fine = fbm(h, w, 7, seed + 9)
    cloud = (fbm(h, w, 4, seed + 4) - 0.5) * 0.12
    col = np.ones((h, w, 3)) * np.array(base)
    col += cloud[..., None]
    col = col * (1 - veins[..., None] * 0.55 * strength) + np.array(vein) * veins[..., None] * 0.55 * strength
    col += (fine[..., None] - 0.5) * 0.05
    return col


# 1. White marble (columns, walls' stone, mouldings).
save("T_Hall_Marble", marble(1024, 1024, (0.93, 0.92, 0.9), (0.55, 0.56, 0.6), 11, 0.8))

# 2. The floor: black and white marble squares (2 x 2 per texture), a thin gold-grey grout.
H = 1024
white = marble(H, H, (0.94, 0.93, 0.91), (0.6, 0.6, 0.64), 21, 0.9, 5)
black = marble(H, H, (0.06, 0.065, 0.08), (0.35, 0.33, 0.3), 37, 1.0, 4)
y, x = np.mgrid[0:H, 0:H]
check = ((x // (H // 2)) + (y // (H // 2))) % 2
floor = np.where(check[..., None] == 0, white, black)
gx = np.minimum(x % (H // 2), (H // 2) - x % (H // 2))
gy = np.minimum(y % (H // 2), (H // 2) - y % (H // 2))
grout = np.minimum(gx, gy) < 3
floor[grout] = np.array((0.55, 0.48, 0.33))
save("T_Hall_FloorCheck", floor)

# 3. The red carpet runner: across U (the gold border at both edges), along V (a quiet damask, repeating).
W, H = 512, 1024
y, x = np.mgrid[0:H, 0:W]
u = x / W
v = y / H
col = np.ones((H, W, 3)) * np.array((0.42, 0.03, 0.05))
pile = fbm(H, W, 6, 51)
col *= (0.82 + 0.3 * pile)[..., None]
motif = (np.sin(u * math.pi * 6) * np.sin(v * math.pi * 8) > 0.55) & (u > 0.16) & (u < 0.84)
diamond = (np.abs(((u * 3) % 1) - 0.5) + np.abs(((v * 4) % 1) - 0.5)) < 0.22
col[(motif | diamond) & (u > 0.16) & (u < 0.84)] *= 1.35
gold = np.array((0.78, 0.6, 0.24))
for lo, hi in ((0.03, 0.09), (0.91, 0.97)):
    band = (u > lo) & (u < hi)
    col[band] = gold * (0.85 + 0.25 * pile[band])[..., None]
for edge in (0.115, 0.885):
    col[np.abs(u - edge) < 0.008] = gold * 0.9
save("T_Hall_Carpet", col)

# 4. The rose window: stained glass round a ball emblem, lead lines between the panes.
S = 1024
im = Image.new("RGB", (S, S), (6, 5, 10))
d = ImageDraw.Draw(im)
c = S / 2
R = S * 0.48
palette = [(200, 30, 45), (25, 60, 170), (240, 190, 60), (30, 130, 70), (150, 40, 150), (230, 230, 240)]
rings = [(R, R * 0.74, 24), (R * 0.74, R * 0.5, 16), (R * 0.5, R * 0.3, 12)]
for ri, (outer, inner, n) in enumerate(rings):
    for i in range(n):
        a0 = 2 * math.pi * i / n
        a1 = 2 * math.pi * (i + 1) / n
        pts = []
        for k in range(9):
            a = a0 + (a1 - a0) * k / 8
            pts.append((c + math.cos(a) * outer, c + math.sin(a) * outer))
        for k in range(9):
            a = a1 - (a1 - a0) * k / 8
            pts.append((c + math.cos(a) * inner, c + math.sin(a) * inner))
        colr = palette[(i + ri * 2) % len(palette)] if ri != 1 else palette[(i * 2 + 1) % 4]
        d.polygon(pts, fill=colr, outline=(10, 8, 12))
        # petals inside the outer ring
        if ri == 0:
            am = (a0 + a1) / 2
            pr = (outer - inner) * 0.32
            px, py = c + math.cos(am) * (outer + inner) / 2, c + math.sin(am) * (outer + inner) / 2
            d.ellipse((px - pr, py - pr, px + pr, py + pr), fill=palette[(i + 3) % 6], outline=(10, 8, 12), width=5)
# the ball emblem: a white ball with its seams, on gold
d.ellipse((c - R * 0.3, c - R * 0.3, c + R * 0.3, c + R * 0.3), fill=(235, 190, 70), outline=(10, 8, 12), width=10)
d.ellipse((c - R * 0.22, c - R * 0.22, c + R * 0.22, c + R * 0.22), fill=(245, 245, 250), outline=(10, 8, 12), width=8)
d.arc((c - R * 0.36, c - R * 0.22, c + R * 0.08, c + R * 0.22), -60, 60, fill=(200, 30, 45), width=10)
d.arc((c - R * 0.08, c - R * 0.22, c + R * 0.36, c + R * 0.22), 120, 240, fill=(200, 30, 45), width=10)
for i in range(64):
    a = 2 * math.pi * i / 64
    d.line((c + math.cos(a) * R * 0.3, c + math.sin(a) * R * 0.3, c + math.cos(a) * R, c + math.sin(a) * R), fill=(10, 8, 12), width=3)
for rr in (R, R * 0.74, R * 0.5, R * 0.3):
    d.ellipse((c - rr, c - rr, c + rr, c + rr), outline=(12, 10, 14), width=12)
glass = np.asarray(im, dtype=np.float64) / 255.0
mottle = fbm(S, S, 6, 71)
glass *= (0.75 + 0.5 * mottle)[..., None]
save("T_Hall_Rose", glass)

# 5. The banner: the United States of Dett Ball's flag, balls for stars (navy canton, red and white stripes, gold fringe).
W, H = 512, 1024
im = Image.new("RGB", (W, H), (240, 238, 232))
d = ImageDraw.Draw(im)
body = int(H * 0.9)
stripes = 9
for i in range(stripes):
    if i % 2 == 0:
        d.rectangle((0, int(body * i / stripes), W, int(body * (i + 1) / stripes)), fill=(178, 22, 40))
d.rectangle((0, 0, W, int(body * 0.42)), fill=(22, 34, 92))
for row in range(4):
    for col in range(3):
        bx = W * (0.2 + 0.3 * col) + (W * 0.15 if row % 2 else 0)
        by = body * (0.06 + 0.1 * row)
        r = W * 0.06
        if bx + r > W * 0.97:
            continue
        d.ellipse((bx - r, by - r, bx + r, by + r), fill=(245, 245, 250))
        # (A ball's seam: one soft curve across it, never read as a letter.)
        d.arc((bx - r * 1.6, by - r * 1.3, bx + r * 0.6, by + r * 1.3), -38, 38, fill=(196, 200, 214), width=4)
# the fringe: a scalloped gold edge with tassels
d.rectangle((0, body, W, body + 18), fill=(200, 155, 60))
for i in range(16):
    x0 = W * i / 16
    d.polygon([(x0, body + 18), (x0 + W / 16, body + 18), (x0 + W / 32, H - 6)], fill=(190, 145, 50))
banner = np.asarray(im, dtype=np.float64) / 255.0
cloth = fbm(H, W, 5, 81)
y, x = np.mgrid[0:H, 0:W]
folds = 0.88 + 0.12 * np.sin(x / W * math.pi * 6 + cloth * 2)
banner *= (folds * (0.9 + 0.15 * cloth))[..., None]
save("T_Hall_Banner", banner)

# 6. Red velvet (curtains, the throne's cushion): deep folds.
W, H = 512, 512
y, x = np.mgrid[0:H, 0:W]
n = fbm(H, W, 5, 91)
fold = 0.5 + 0.5 * np.sin(x / W * math.pi * 2 * 5 + n * 3)
shade = 0.45 + 0.65 * fold ** 1.6
col = np.ones((H, W, 3)) * np.array((0.5, 0.03, 0.07)) * shade[..., None]
col += np.array((0.25, 0.05, 0.08)) * (fold ** 8)[..., None]
save("T_Hall_Velvet", col)

# 7. Wallpaper: cream with a soft damask of balls and laurels.
W = 512
y, x = np.mgrid[0:W, 0:W]
u, v = x / W, y / W
col = np.ones((W, W, 3)) * np.array((0.86, 0.82, 0.72))
cx, cy = (u * 2) % 1 - 0.5, (v * 2) % 1 - 0.5
ring = np.abs(np.sqrt(cx ** 2 + cy ** 2) - 0.2) < 0.018
dot = np.sqrt(cx ** 2 + cy ** 2) < 0.07
leaf = (np.abs(np.sin((cx + cy) * 18)) < 0.12) & (np.sqrt(cx ** 2 + cy ** 2) < 0.38) & (np.sqrt(cx ** 2 + cy ** 2) > 0.26)
col[ring | dot | leaf] = np.array((0.78, 0.7, 0.52))
col *= (0.94 + 0.08 * fbm(W, W, 5, 101))[..., None]
save("T_Hall_Wallpaper", col)

# 8. Dark wood panels (the wainscot, the doors).
W = 512
y, x = np.mgrid[0:W, 0:W]
grain = fbm(W, W, 6, 111)
rings = np.sin((x / W * 40 + grain * 6))
col = np.ones((W, W, 3)) * np.array((0.24, 0.11, 0.06))
col *= (0.75 + 0.18 * rings + 0.2 * grain)[..., None]
frame = (np.minimum(np.minimum(x, W - 1 - x), np.minimum(y, W - 1 - y)) < 26)
inset = (np.minimum(np.minimum(x, W - 1 - x), np.minimum(y, W - 1 - y)) < 34) & ~frame
col[frame] *= 1.15
col[inset] *= 0.6
save("T_Hall_Wood", col)

# 9. A tall window at night: deep blue, a few stars, the city's glow low, lead mullions.
W, H = 256, 512
y, x = np.mgrid[0:H, 0:W]
col = np.zeros((H, W, 3))
t = y / H
col += np.array((0.02, 0.04, 0.12)) * (1 - t)[..., None] + np.array((0.15, 0.1, 0.18)) * (t ** 3)[..., None]
stars = rng.random((H, W)) > 0.9975
col[stars] = 0.9
col[(x % (W // 3) < 3) | (y % (H // 5) < 3)] = 0.01
save("T_Hall_Window", col)

# 10. Brushed gold (tinted in the material): a fine streaky grey.
W = 256
streak = np.asarray(Image.fromarray((rng.random((W, W)) * 255).astype(np.uint8)).resize((W, W)).filter(ImageFilter.BoxBlur(0)), dtype=np.float64) / 255.0
streak = np.asarray(Image.fromarray((streak * 255).astype(np.uint8)).resize((W // 16, W), Image.BILINEAR).resize((W, W), Image.BILINEAR), dtype=np.float64) / 255.0
col = (0.8 + 0.2 * streak)[..., None] * np.ones(3)
save("T_Hall_Gold", col)

# 11. Paintings for the corridor: a ball on a cushion, in oils (three moods). No faces, no letters.
for k, (bg, ball, cushion) in enumerate((((0.16, 0.12, 0.1), (0.95, 0.95, 0.97), (0.55, 0.06, 0.08)),
                                          ((0.07, 0.1, 0.18), (0.95, 0.85, 0.4), (0.12, 0.25, 0.55)),
                                          ((0.12, 0.16, 0.1), (0.85, 0.2, 0.15), (0.65, 0.5, 0.2)))):
    W, H = 384, 512
    y, x = np.mgrid[0:H, 0:W]
    col = np.ones((H, W, 3)) * np.array(bg)
    vignette = 1 - 0.6 * (((x / W - 0.5) ** 2 + (y / H - 0.45) ** 2) * 2.5)
    col *= np.clip(vignette, 0.3, 1.2)[..., None]
    cy0 = H * 0.72
    cush = ((x - W / 2) / (W * 0.38)) ** 2 + ((y - cy0) / (H * 0.09)) ** 2 < 1
    col[cush] = np.array(cushion) * (0.7 + 0.5 * ((cy0 - y[cush]) / (H * 0.09) + 1) / 2)[..., None]
    bx, by, br = W / 2, H * 0.5, W * 0.24
    dx, dy = (x - bx) / br, (y - by) / br
    inside = dx ** 2 + dy ** 2 < 1
    nz = np.sqrt(np.clip(1 - dx ** 2 - dy ** 2, 0, 1))
    light = np.clip(-0.5 * dx - 0.6 * dy + 0.6 * nz, 0, 1)
    col[inside] = (np.array(ball) * (0.25 + 0.85 * light[inside])[..., None]) + (light[inside] ** 20)[..., None] * 0.6
    brush = fbm(H, W, 6, 121 + k)
    col *= (0.88 + 0.22 * brush)[..., None]
    save("T_Hall_Painting%d" % k, col)
