#!/usr/bin/env python3
"""The original's pictures for the Lynx: src/art.c / art.h.

The Amiga original draws everything with RectFill / Move / Draw
(gfx.c gfx_draw_tile, player.c player_draw, enemy.c enemies_draw, items.c
items_draw). Those calls are transcribed here, drawn at the original's size
(16 x 16 tiles, the 14 x 28 hero) and scaled to the Lynx view: half width,
7/16 height (the 14 tile rows of 16 lines become 14 rows of 7, so the whole
playfield fits the 102-line screen). Each Lynx pixel takes the colour that
covers most of its source area; sky (the background) counts a little less,
so thin details survive.

    python3 projects/jump_quest/tools/mkart.py [--png build/art]

--png also writes enlarged previews of every picture.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "lynxdev" / "src"))
from lynxdev.sprite import encode  # noqa: E402

# the original's palette indices (game.h); 0 = sky = transparent here
SKY, SKY2, BROWN, DKBROWN, GREEN, DKGREEN, ORANGE, DKORANGE = range(8)
SKIN, GREY, BLUE, WHITE, BLACK, YELLOW, RED, LTGREEN = range(8, 16)
STRIP_TILES = (1, 2, 3, 5, 10)
PALETTE = [0x59C, 0x59C, 0xA62, 0x741, 0x4A2, 0x381, 0xD83, 0xA52,
           0xEC9, 0x999, 0x35A, 0xEEE, 0x111, 0xDD3, 0xD22, 0x4D4]


class Canvas:
    """A RastPort: SetAPen / RectFill / Move / Draw, pen -1 = nothing."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[-1] * w for _ in range(h)]
        self.pen = 0
        self.cx = self.cy = 0

    def set(self, pen):
        self.pen = pen

    def plot(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = self.pen

    def rect(self, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.plot(x, y)

    def move(self, x, y):
        self.cx, self.cy = x, y

    def draw(self, x, y):
        x0, y0 = self.cx, self.cy
        dx, dy = abs(x - x0), -abs(y - y0)
        sx, sy = (1 if x > x0 else -1), (1 if y > y0 else -1)
        err = dx + dy
        while True:
            self.plot(x0, y0)
            if x0 == x and y0 == y:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy
        self.cx, self.cy = x, y

    def line(self, x0, y0, x1, y1):
        self.move(x0, y0)
        self.draw(x1, y1)


def scale(c, out_h=None, ox=0):
    """Half width, 7/16 height. Each output pixel: the pen with the most
    coverage (sky/transparent weighted 0.6). ox: horizontal phase (0/1)."""
    out_w = (c.w + ox + 1) // 2
    out_h = out_h or -(-c.h * 7 // 16)
    grid = []
    for j in range(out_h):
        y0, y1 = j * 16 / 7, (j + 1) * 16 / 7
        row = []
        for i in range(out_w):
            w = {}
            for sx in (2 * i - ox, 2 * i + 1 - ox):
                if not 0 <= sx < c.w:
                    continue
                for sy in range(int(y0), min(c.h, int(y1) + 1)):
                    cov = min(y1, sy + 1) - max(y0, sy)
                    if cov <= 0:
                        continue
                    p = c.px[sy][sx]
                    w[p] = w.get(p, 0) + cov * (0.6 if p in (-1, SKY) else 1.0)
            best = max(w.items(), key=lambda kv: kv[1])[0] if w else -1
            row.append(0 if best in (-1, SKY) else best)
        grid.append(row)
    return grid


# ------------------------------------------------------------ tiles (gfx.c)

def tile(t):
    c = Canvas(16, 16)
    sx = sy = 0
    ex = ey = 15
    S = c.set
    R = c.rect
    L = c.line
    if t == 1:      # TILE_GROUND
        S(BROWN); R(sx, sy, ex, ey)
        S(DKBROWN); L(sx, sy + 4, ex, sy + 4); L(sx + 4, sy + 8, ex, sy + 8); L(sx, sy + 12, sx + 8, sy + 12)
    elif t == 2:    # TILE_GRASS
        S(BROWN); R(sx, sy + 4, ex, ey)
        S(GREEN); R(sx, sy, ex, sy + 3)
        S(DKGREEN); R(sx + 2, sy, sx + 3, sy + 1); R(sx + 8, sy, sx + 9, sy + 1); R(sx + 13, sy, sx + 14, sy + 1)
    elif t == 3:    # TILE_BRICK
        S(ORANGE); R(sx, sy, ex, ey)
        S(DKORANGE); L(sx, sy + 7, ex, sy + 7); L(sx + 8, sy, sx + 8, sy + 6); L(sx, sy + 15, ex, sy + 15)
        L(sx + 4, sy + 8, sx + 4, sy + 14); L(sx + 12, sy + 8, sx + 12, sy + 14)
    elif t == 4:    # TILE_QBLOCK: a gift box
        S(RED); R(sx, sy, ex, ey)
        S(BLACK); L(sx, sy, ex, sy); L(sx, ey, ex, ey); L(sx, sy, sx, ey); L(ex, sy, ex, ey)
        S(YELLOW); R(sx + 7, sy + 1, sx + 8, ey - 1); R(sx + 1, sy + 7, ex - 1, sy + 8)
        R(sx + 4, sy + 2, sx + 6, sy + 4); R(sx + 9, sy + 2, sx + 11, sy + 4)
    elif t == 13:   # TILE_QBLOCK_HIT
        S(GREY); R(sx, sy, ex, ey)
        S(DKBROWN); L(sx, sy, ex, sy); L(sx, ey, ex, ey); L(sx, sy, sx, ey); L(ex, sy, ex, ey)
    elif t == 5:    # TILE_STONE
        S(GREY); R(sx, sy, ex, ey)
        S(BLACK); L(sx, sy, ex, sy); L(sx, ey, ex, ey); L(sx, sy, sx, ey); L(ex, sy, ex, ey)
        S(WHITE); L(sx + 1, sy + 1, ex - 1, sy + 1); L(sx + 1, sy + 1, sx + 1, ey - 1)
    elif t == 6:    # TILE_PIPE_TL (stone columns with a capital)
        S(GREY); R(sx, sy + 2, ex, sy + 5); R(sx + 3, sy + 6, ex, ey)
        S(WHITE); L(sx, sy + 2, ex, sy + 2); L(sx + 5, sy + 7, sx + 5, ey)
        S(BLACK); L(sx, sy + 5, ex, sy + 5)
    elif t == 7:    # TILE_PIPE_TR
        S(GREY); R(sx, sy + 2, ex, sy + 5); R(sx, sy + 6, ex - 3, ey)
        S(WHITE); L(sx, sy + 2, ex, sy + 2)
        S(BLACK); L(sx, sy + 5, ex, sy + 5); L(sx + 10, sy + 7, sx + 10, ey)
    elif t == 8:    # TILE_PIPE_BL
        S(GREY); R(sx + 3, sy, ex, ey)
        S(WHITE); L(sx + 5, sy, sx + 5, ey)
        S(BLACK); L(sx + 3, sy, sx + 3, ey)
    elif t == 9:    # TILE_PIPE_BR
        S(GREY); R(sx, sy, ex - 3, ey)
        S(BLACK); L(sx + 10, sy, sx + 10, ey); L(ex - 3, sy, ex - 3, ey)
    elif t == 10:   # TILE_PLATFORM
        S(BROWN); R(sx, sy, ex, sy + 3)
        S(DKBROWN); L(sx, sy + 3, ex, sy + 3)
    elif t == 11:   # TILE_CLOUD_L
        S(WHITE); R(sx + 4, sy + 4, ex, sy + 12); R(sx + 8, sy + 2, ex, sy + 4)
    elif t == 12:   # TILE_CLOUD_R
        S(WHITE); R(sx, sy + 4, sx + 12, sy + 12); R(sx, sy + 2, sx + 8, sy + 4)
    elif t == 14:   # TILE_FLAG
        S(GREY); R(sx + 7, sy, sx + 8, ey)
        S(RED); R(sx + 9, sy + 1, sx + 15, sy + 6)
        S(YELLOW); R(sx + 6, sy, sx + 9, sy + 1)
    elif t == 15:   # TILE_BUSH
        S(GREEN); R(sx + 2, sy + 6, sx + 14, sy + 14); R(sx + 4, sy + 4, sx + 12, sy + 6)
        S(DKGREEN); R(sx + 6, sy + 8, sx + 10, sy + 10)
    return c


# ------------------------------------------------------------ hero (player.c)

def hero(character, leg_off):
    """facing right (left = Suzy's HFLIP); leg_off 0, 2, -2"""
    rj = character == 0
    ph = 28 if rj else 24
    c = Canvas(14, ph)
    S, R = c.set, c.rect
    sx = sy = 0
    bw = 10 if rj else 12
    ox = (14 - bw) // 2
    if rj:
        S(GREY); R(sx + ox + 1, sy, sx + ox + bw - 2, sy + 2)
        S(SKIN); R(sx + ox + 1, sy + 3, sx + ox + bw - 2, sy + 7)
        S(BLACK); R(sx + ox + 5, sy + 4, sx + ox + 6, sy + 5)
        S(BLACK); R(sx + ox + 2, sy + 7, sx + ox + bw - 3, sy + 8)
        S(WHITE); R(sx + ox, sy + 9, sx + ox + bw - 1, sy + 17)
        S(BLUE); R(sx + ox, sy + 18, sx + ox + bw // 2 - 1 + leg_off, sy + 24)
        R(sx + ox + bw // 2 + 1 - leg_off, sy + 18, sx + ox + bw - 1, sy + 24)
        S(BROWN); R(sx + ox, sy + 25, sx + ox + bw // 2 - 1 + leg_off, sy + 27)
        R(sx + ox + bw // 2 + 1 - leg_off, sy + 25, sx + ox + bw - 1, sy + 27)
    else:
        S(GREY); R(sx + ox, sy, sx + ox + bw - 1, sy + 2)
        S(SKIN); R(sx + ox, sy + 3, sx + ox + bw - 1, sy + 7)
        S(BLACK); R(sx + ox + 6, sy + 4, sx + ox + 7, sy + 5)
        S(BLACK); R(sx + ox + 1, sy + 7, sx + ox + bw - 2, sy + 8)
        S(WHITE); R(sx + ox - 1, sy + 9, sx + ox + bw, sy + 15)
        S(BLUE); R(sx + ox, sy + 16, sx + ox + bw // 2 - 1 + leg_off, sy + 20)
        R(sx + ox + bw // 2 + 1 - leg_off, sy + 16, sx + ox + bw - 1, sy + 20)
        S(BROWN); R(sx + ox, sy + 21, sx + ox + bw // 2 - 1 + leg_off, sy + 23)
        R(sx + ox + bw // 2 + 1 - leg_off, sy + 21, sx + ox + bw - 1, sy + 23)
    return c


# ------------------------------------------------------------ enemies (enemy.c)

def enemy(t, frame):
    c = Canvas(16, 16)
    S, R, L = c.set, c.rect, c.line
    sx = sy = 0
    if t == 2:      # beetle
        S(BROWN); R(sx + 2, sy + 4, sx + 13, sy + 12)
        S(DKBROWN); R(sx + 4, sy + 1, sx + 11, sy + 4)
        if frame:
            R(sx, sy + 8, sx + 2, sy + 14); R(sx + 13, sy + 6, sx + 15, sy + 14)
        else:
            R(sx, sy + 6, sx + 2, sy + 14); R(sx + 13, sy + 8, sx + 15, sy + 14)
        S(WHITE); R(sx + 5, sy + 2, sx + 6, sy + 3); R(sx + 9, sy + 2, sx + 10, sy + 3)
    elif t == 3:    # fly
        S(GREY); R(sx + 4, sy + 6, sx + 11, sy + 13)
        S(WHITE)
        if frame:
            R(sx + 1, sy + 1, sx + 6, sy + 5); R(sx + 9, sy + 1, sx + 14, sy + 5)
        else:
            R(sx + 1, sy + 3, sx + 6, sy + 6); R(sx + 9, sy + 3, sx + 14, sy + 6)
        S(RED); R(sx + 5, sy + 7, sx + 6, sy + 8); R(sx + 9, sy + 7, sx + 10, sy + 8)
    elif t == 4:    # spider (the legs keep the last pen: black)
        S(BLACK); R(sx + 4, sy + 4, sx + 11, sy + 11)
        if frame:
            L(sx + 4, sy + 5, sx, sy + 2); L(sx + 11, sy + 5, sx + 15, sy + 2)
            L(sx + 4, sy + 9, sx, sy + 13); L(sx + 11, sy + 9, sx + 15, sy + 13)
        else:
            L(sx + 4, sy + 5, sx + 1, sy + 1); L(sx + 11, sy + 5, sx + 14, sy + 1)
            L(sx + 4, sy + 9, sx + 1, sy + 14); L(sx + 11, sy + 9, sx + 14, sy + 14)
        S(WHITE); L(sx + 7, sy, sx + 7, sy + 4)
        S(RED); R(sx + 5, sy + 6, sx + 6, sy + 7); R(sx + 9, sy + 6, sx + 10, sy + 7)
    elif t == 5:    # ant
        S(RED); R(sx + 5, sy + 6, sx + 10, sy + 12); R(sx + 6, sy + 3, sx + 9, sy + 6)
        S(BLACK); L(sx + 6, sy + 3, sx + 4, sy); L(sx + 9, sy + 3, sx + 11, sy)
        if frame:
            L(sx + 5, sy + 8, sx + 2, sy + 14); L(sx + 10, sy + 8, sx + 13, sy + 14)
        else:
            L(sx + 5, sy + 8, sx + 3, sy + 14); L(sx + 10, sy + 8, sx + 12, sy + 14)
    return c


# ------------------------------------------------------------ items (items.c)

def item(t):
    c = Canvas(16, 16)
    S, R, L = c.set, c.rect, c.line
    sx = sy = 0
    if t == 10:     # circuit board
        S(DKGREEN); R(sx + 2, sy + 3, sx + 13, sy + 12)
        S(YELLOW); L(sx + 4, sy + 5, sx + 11, sy + 5); L(sx + 4, sy + 8, sx + 11, sy + 8); L(sx + 7, sy + 3, sx + 7, sy + 12)
        S(BLACK); R(sx + 4, sy + 6, sx + 6, sy + 7); R(sx + 9, sy + 9, sx + 11, sy + 10)
    elif t == 11:   # diskette
        S(BLUE); R(sx + 2, sy + 2, sx + 13, sy + 13)
        S(GREY); R(sx + 4, sy + 2, sx + 11, sy + 4)
        S(WHITE); R(sx + 4, sy + 7, sx + 11, sy + 11)
        S(BLACK); R(sx + 7, sy + 9, sx + 8, sy + 10)
    elif t == 12:   # ball
        S(RED); R(sx + 4, sy + 3, sx + 11, sy + 12); R(sx + 3, sy + 5, sx + 12, sy + 10)
        S(WHITE); R(sx + 5, sy + 4, sx + 7, sy + 6)
        S(YELLOW); L(sx + 3, sy + 7, sx + 12, sy + 7)
    elif t == 13:   # pillow
        S(WHITE); R(sx + 2, sy + 4, sx + 13, sy + 12); R(sx + 3, sy + 3, sx + 12, sy + 13)
        S(GREY); L(sx + 2, sy + 8, sx + 13, sy + 8); L(sx + 7, sy + 4, sx + 7, sy + 12)
    elif t == 14:   # plant
        S(LTGREEN); R(sx + 4, sy + 2, sx + 11, sy + 7); R(sx + 2, sy + 4, sx + 13, sy + 6)
        S(GREEN); R(sx + 7, sy + 7, sx + 8, sy + 10)
        S(ORANGE); R(sx + 4, sy + 10, sx + 11, sy + 14)
        S(DKORANGE); L(sx + 4, sy + 10, sx + 11, sy + 10)
    elif t == 20:   # briefcase
        S(BROWN); R(sx + 1, sy + 4, sx + 14, sy + 13)
        S(DKBROWN); R(sx + 5, sy + 2, sx + 10, sy + 4)
        S(YELLOW); R(sx + 7, sy + 8, sx + 8, sy + 9)
        S(WHITE); R(sx + 2, sy + 5, sx + 4, sy + 6)
    elif t == 21:   # heart
        S(RED); R(sx + 3, sy + 4, sx + 7, sy + 8); R(sx + 8, sy + 4, sx + 12, sy + 8); R(sx + 2, sy + 5, sx + 13, sy + 7)
        R(sx + 4, sy + 8, sx + 11, sy + 11); R(sx + 5, sy + 11, sx + 10, sy + 12); R(sx + 6, sy + 12, sx + 9, sy + 13)
        S(WHITE); R(sx + 4, sy + 5, sx + 5, sy + 6)
    elif t == 22:   # pillow with ZZ: an extra life
        S(WHITE); R(sx + 2, sy + 4, sx + 13, sy + 12); R(sx + 3, sy + 3, sx + 12, sy + 13)
        S(BLUE); c.move(sx + 5, sy + 6); c.draw(sx + 8, sy + 6); c.draw(sx + 5, sy + 10); c.draw(sx + 8, sy + 10)
    elif t == 23:   # beer
        S(YELLOW); R(sx + 4, sy + 4, sx + 11, sy + 13)
        S(WHITE); R(sx + 3, sy + 2, sx + 12, sy + 5)
        S(GREY); L(sx + 4, sy + 4, sx + 4, sy + 13); L(sx + 11, sy + 4, sx + 11, sy + 13)
        R(sx + 12, sy + 6, sx + 14, sy + 10)
    elif t == 24:   # wine
        S(GREY); R(sx + 7, sy + 9, sx + 8, sy + 12); R(sx + 5, sy + 12, sx + 10, sy + 13)
        S(RED); R(sx + 4, sy + 3, sx + 11, sy + 9); R(sx + 5, sy + 2, sx + 10, sy + 3)
        S(WHITE); R(sx + 5, sy + 4, sx + 6, sy + 5)
    return c


# ------------------------------------------------------------ HUD heart (hud.c draw_heart), 1 bpp

def heart_small():
    c = Canvas(6, 7)
    c.set(1)
    c.rect(0, 1, 2, 4); c.rect(3, 1, 5, 4); c.rect(0, 2, 5, 3); c.rect(1, 4, 4, 5); c.rect(2, 5, 3, 6)
    # port: 6 x 7 at full size would be 3 x 3 here; a 5 x 4 heart reads better
    return [[0, 1, 0, 1, 0], [1, 1, 1, 1, 1], [0, 1, 1, 1, 0], [0, 0, 1, 0, 0]]


# ------------------------------------------------------------ output

def main():
    pics = {}            # name -> grid
    for t in range(1, 16):
        if t in (0,):
            continue
        pics[f"tile_{t}"] = scale(tile(t), 7)
    for ch, name in ((0, "rj"), (1, "dale")):
        for k, lo in enumerate((0, 2, -2)):
            pics[f"{name}_{k}"] = scale(hero(ch, lo))
    for t, name in ((2, "beetle"), (3, "fly"), (4, "spider"), (5, "ant")):
        for f in (0, 1):
            pics[f"{name}_{f}"] = scale(enemy(t, f), 7)
    items = (10, 11, 12, 13, 14, 20, 21, 22, 23, 24)
    for t in items:
        pics[f"item_{t}"] = scale(item(t), 7)
    pics["dot"] = [[RED]]
    # runs of 2-4 identical tiles as one sprite (main.c rows_index)
    strips = []
    for t in STRIP_TILES:
        g = pics[f"tile_{t}"]
        for n in (2, 3, 4):
            pics[f"strip_{t}_{n}"] = [row * n for row in g]
            strips.append(f"strip_{t}_{n}")                                 # a particle (2 x 2 there, 1 x 1 here)
    heart = heart_small()

    c = ["/* art.c: made by tools/mkart.py from the original's drawing code - don't edit, re-run it. */",
         '#include "art.h"', ""]
    total = 0
    for name, g in pics.items():
        d = encode(g, 4)
        total += len(d)
        c.append(f"static const unsigned char {name}[{len(d)}] = {{ " + ", ".join(f"0x{b:02X}" for b in d) + " };")
    d = encode(heart, 1)
    c.append(f"const unsigned char art_heart[{len(d)}] = {{ " + ", ".join(f"0x{b:02X}" for b in d) + " };")
    c.append("")
    c.append("/* by tile id (0: sky, nothing drawn) */")
    c.append("const unsigned char * const art_tiles[16] = { 0, " +
             ", ".join(f"tile_{t}" for t in range(1, 16)) + " };")
    c.append("/* [character][0: standing, 1: leg forward, 2: leg back] */")
    c.append("const unsigned char * const art_hero[2][3] = { { rj_0, rj_1, rj_2 }, { dale_0, dale_1, dale_2 } };")
    c.append("/* [type - ENT_BEETLE][frame] */")
    c.append("const unsigned char * const art_enemy[4][2] = { { beetle_0, beetle_1 }, { fly_0, fly_1 }, "
             "{ spider_0, spider_1 }, { ant_0, ant_1 } };")
    c.append("/* [type - ENT_BOARD]: 10-14 collectibles, 20-24 power-ups (0 between) */")
    tbl = [f"item_{t}" if t in items else "0" for t in range(10, 25)]
    c.append(f"const unsigned char * const art_item[15] = {{ {', '.join(tbl)} }};")
    c.append("const unsigned char * const art_dot = dot;")
    c.append("/* [STRIP_TILES index][length - 2] */")
    c.append(f"const unsigned char * const art_strip[{len(STRIP_TILES)}][3] = {{ " +
             ", ".join("{ " + ", ".join(f"strip_{t}_{n}" for n in (2, 3, 4)) + " }" for t in STRIP_TILES) + " };")
    c.append(f"const uint16_t art_palette[16] = {{ {', '.join(f'0x{v:03X}' for v in PALETTE)} }};")
    c.append("")
    (HERE / "src" / "art.c").write_text("\n".join(c))
    h = ["/* art.h: made by tools/mkart.py - don't edit, re-run it. */",
         "#ifndef ART_H", "#define ART_H", "#include <stdint.h>", "",
         f"#define HERO_W 7",
         f"#define HERO_H_RJ {len(pics['rj_0'])}",
         f"#define HERO_H_DALE {len(pics['dale_0'])}",
         "extern const unsigned char * const art_tiles[16];",
         "extern const unsigned char * const art_hero[2][3];",
         "extern const unsigned char * const art_enemy[4][2];",
         "extern const unsigned char * const art_item[15];",
         "extern const unsigned char * const art_dot;",
         f"/* runs of tiles {', '.join(map(str, STRIP_TILES))} (ground, grass, brick, stone, platform) */",
         f"extern const unsigned char * const art_strip[{len(STRIP_TILES)}][3];",
         f"extern const unsigned char art_heart[{len(d)}];",
         "extern const uint16_t art_palette[16];   /* 0xRGB, the original's (gfx.c) */",
         "", "#endif", ""]
    (HERE / "src" / "art.h").write_text("\n".join(h))
    print(f"mkart: {len(pics)} pictures, {total} bytes")

    if "--png" in sys.argv:
        from PIL import Image
        out = Path(sys.argv[sys.argv.index("--png") + 1])
        out.mkdir(parents=True, exist_ok=True)
        rgb = [((v >> 8) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17) for v in PALETTE]
        names = list(pics)
        cols = 12
        cell = 10
        sheet = Image.new("RGB", (cols * cell, (len(names) // cols + 1) * 16), (40, 40, 40))
        for k, n in enumerate(names):
            g = pics[n]
            x0, y0 = (k % cols) * cell, (k // cols) * 16
            for y, r in enumerate(g):
                for x, p in enumerate(r):
                    sheet.putpixel((x0 + x, y0 + y), rgb[p] if p else rgb[0])
        sheet.resize((sheet.width * 8, sheet.height * 8), Image.NEAREST).save(out / "sheet.png")


if __name__ == "__main__":
    main()
