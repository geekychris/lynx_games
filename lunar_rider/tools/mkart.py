#!/usr/bin/env python3
"""Lunar Rider (Lynx): sprite data (src/art.c, src/art.h).

The Amiga original (amiga_mcp/examples/lunar_rider/draw.c) draws everything
with RectFill on a 320 x 256 screen. The Lynx shows the original's world at
half width and 2/5 height (Y(y) = y * 2 / 5: the ground at y 200 is Lynx
line 80), so each picture here is the original's at that size, touched up
by hand where a pixel-by-pixel reduction lost the shape:

- the buggy (24 x 16 incl. its upward gun -> 13 x 7): yellow body and
  roof, cyan windscreen, orange guns, grey wheels with the original's two
  spoke frames, the dark grey undercarriage;
- the small UFO (12 x 7 -> 6 x 3) and the large one (20 x 11 -> 10 x 4),
  each in two frames: the body pulsing cyan / white as the original's
  (flash & 8), the cyan dome, the dark grey rim, the large one's red lights;
- the meteor (pink with an orange core and a red trail), the bomb;
- the boulder, the mine and the hill's bump (the original's formula);
- the far and near mountain ranges: the original's clipped triangle waves
  (approx_sin) sampled at its 2-pixel columns, i.e. one Lynx column each.
  Three far periods and five near periods are exactly 256 Lynx columns, so
  each range is one 256-wide 1-bpp strip drawn twice, scrolled by Suzy;
- the original's 5x7 font (digits and capitals) for the title and messages;
- the original's 30 stars (its own generator, seed 42).

Pens are the palette indices (the original's 16 colours, kept as they
are), so 4-bpp pictures use spr_pens_identity.

Run: python3 tools/mkart.py
"""
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent


def _lynxdev_root() -> Path:
    if os.environ.get("LYNX_ROOT"):
        return Path(os.environ["LYNX_ROOT"])
    for p in HERE.parents:
        if (p / "lynx").is_file() and (p / "sdk").is_dir():
            return p
    raise SystemExit("no lynxdev checkout above this game (or set LYNX_ROOT)")


sys.path.insert(0, str(_lynxdev_root() / "tools" / "lynxdev" / "src"))
from lynxdev.sprite import encode  # noqa: E402


def Y(y):
    """the original's screen y -> Lynx line (game.c's ytab)"""
    return (y * 2) // 5


def ctrunc(a, b):
    """C's integer division (truncates toward zero)"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# hex digit = palette index (main.c palette: the original's colours)
def grid(art):
    return [[0 if c == "." else int(c, 16) for c in row] for row in art]


# 6 yellow, A cyan, 9 orange, 7 grey, E dark grey
BUGGY = [
    [".....9.......",
     "...6666A.....",
     ".66666666699.",
     ".6666666666..",
     ".777EEEE777..",
     ".EEE....EEE..",
     ".777....777.."],
    [".....9.......",
     "...6666A.....",
     ".66666666699.",
     ".6666666666..",
     ".7E7EEEE7E7..",
     ".7E7....7E7..",
     ".7E7....7E7.."],
]

# body B = cyan (A) or white (1): the original's flash & 8
def ufo_sm(b):
    return [f"..AA..", f".{b*4}.", "EEEEEE"]


def ufo_lg(b):
    return ["...AAAA...", f".{b*8}.", "EEEEEEEEEE", "E8EEEEEE8E"]


METEOR = ["....8",
          "FFFF.",
          "F99F.",
          "FFFF."]
BOMB = ["C1",
        "CC"]
ROCK = ["EE",
        "EE",
        "EE",
        "EE"]
MINE = ["88",
        "88"]


def hill():
    """the original's 24-pixel parabolic bump (game.c), max 8 high, at
    half width: brown (4) surface, dark brown (5) under it, down to the
    ground line (Lynx line 80, drawn by the ground band)"""
    tops = []
    for c in range(12):
        hs = []
        for w in (2 * c, 2 * c + 1):
            hw = w - 12
            bump = 8 - ctrunc(hw * hw, 18)
            hs.append(200 - max(bump, 0))
        tops.append(Y(min(hs)))
    top = min(tops)
    rows = []
    for y in range(top, 80):
        rows.append("".join("." if y < t else "4" if y == t else "5" for t in tops))
    return rows, top


def approx_sin(phase):
    phase &= 255
    if phase < 64:
        return phase * 4
    if phase < 192:
        return 255 - (phase - 64) * 2
    return -255 + (phase - 192) * 4


def mountains(mult_num, base, amp, lo, hi):
    """draw_far_mountains / draw_near_mountains: h = base - peak * amp / 255,
    clipped to lo..hi, sampled at orig x = 2u (the original's 2-pixel
    columns) for u = 0..255. Returns the rows from the highest peak down to
    Y(hi) (exclusive: from there the range is solid, a box)."""
    tops = []
    for u in range(256):
        phase = (2 * u) * mult_num // 2
        h = base - ctrunc(approx_sin(phase) * amp, 255)
        h = max(lo, min(hi, h))
        tops.append(Y(h))
    top = min(tops)
    rows = ["".join("1" if y >= t else "." for t in tops) for y in range(top, Y(hi))]
    return rows, top


FONT = {  # the original's 5x7 font (draw.c), bits 4-0 = columns left to right
    "0": [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E], "1": [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E],
    "2": [0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F], "3": [0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E],
    "4": [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02], "5": [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
    "6": [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E], "7": [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
    "8": [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E], "9": [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C],
    "A": [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11], "B": [0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E],
    "C": [0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E], "D": [0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E],
    "E": [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F], "F": [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10],
    "G": [0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E], "H": [0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
    "I": [0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E], "J": [0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C],
    "K": [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11], "L": [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F],
    "M": [0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11], "N": [0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11],
    "O": [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E], "P": [0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10],
    "Q": [0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D], "R": [0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11],
    "S": [0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E], "T": [0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
    "U": [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E], "V": [0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04],
    "W": [0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11], "X": [0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11],
    "Y": [0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04], "Z": [0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F],
}
GLYPHS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def stars():
    """draw_stars: 30 stars from its LCG (seed 42), x < 320, y < 80"""
    seed, out = 42, []
    for _ in range(30):
        seed = (seed * 1103515245 + 12345) & 0xFFFFFFFF
        sx = (seed >> 16) % 320
        sy = (seed >> 8) % 80
        out.append((sx // 2, Y(sy)))
    return out


def main():
    c = ["/* art.c: made by tools/mkart.py - don't edit, re-run it. */", '#include "art.h"', ""]
    h = ["/* art.h: made by tools/mkart.py - don't edit, re-run it. */", "#ifndef ART_H", "#define ART_H", ""]

    def arr(name, data):
        c.append(f"const unsigned char {name}[{len(data)}] = {{")
        c.extend("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + "," for i in range(0, len(data), 16))
        c.append("};")
        h.append(f"extern const unsigned char {name}[{len(data)}];")

    def frames(name, grids, bpp):
        for i, g in enumerate(grids):
            arr(f"{name}_{i}", encode(g, bpp))
        c.append(f"const unsigned char * const {name}_frames[{len(grids)}] = {{ " +
                 ", ".join(f"{name}_{i}" for i in range(len(grids))) + " };")
        h.append(f"extern const unsigned char * const {name}_frames[{len(grids)}];")

    frames("buggy", [grid(f) for f in BUGGY], 4)
    frames("ufo_sm", [grid(ufo_sm("A")), grid(ufo_sm("1"))], 4)
    frames("ufo_lg", [grid(ufo_lg("A")), grid(ufo_lg("1"))], 4)
    arr("meteor_data", encode(grid(METEOR), 4))
    arr("bomb_data", encode(grid(BOMB), 4))
    arr("rock_data", encode(grid(ROCK), 4))
    arr("mine_data", encode(grid(MINE), 4))
    rows, top = hill()
    # a hill cut short by its chunk's end shows only its first columns:
    # frame n - 1 is the first n Lynx columns
    frames("hill", [grid([r[:n] for r in rows]) for n in range(1, 13)], 4)
    h.append(f"#define HILL_TOP {top}           /* Lynx line of the hill sprite's top */")

    far, far_top = mountains(3, 140, 40, 90, 140)
    near, near_top = mountains(5, 130, 60, 80, 160)
    arr("far_data", encode([[int(ch == "1") for ch in r] for r in far], 1))
    arr("near_data", encode([[int(ch == "1") for ch in r] for r in near], 1))
    h.append(f"#define FAR_TOP {far_top}            /* the strips' top lines; solid below */")
    h.append(f"#define FAR_SOLID {Y(140)}          /* far range: solid from here to FAR_BOTTOM */")
    h.append(f"#define FAR_BOTTOM {Y(160)}")
    h.append(f"#define NEAR_TOP {near_top}")
    h.append(f"#define NEAR_SOLID {Y(160)}")
    h.append(f"#define NEAR_BOTTOM {Y(170)}")

    glyphs = []
    for ch in GLYPHS:
        glyphs.append([[1 if FONT[ch][r] & (0x10 >> x) else 0 for x in range(5)] for r in range(7)])
    frames("glyph", glyphs, 1)

    st = stars()
    c.append("const unsigned char star_x[30] = { " + ", ".join(str(x) for x, _ in st) + " };")
    c.append("const unsigned char star_y[30] = { " + ", ".join(str(y) for _, y in st) + " };")
    h.append("extern const unsigned char star_x[30], star_y[30];")

    h += ["", "#endif", ""]
    (HERE / "src" / "art.c").write_text("\n".join(c) + "\n")
    (HERE / "src" / "art.h").write_text("\n".join(h))
    print("far rows", len(far), "top", far_top, "near rows", len(near), "top", near_top,
          "hill rows", len(rows), "top", top)
    if "--show" in sys.argv:
        for r in far + [""] + near + [""] + rows:
            print(r[:160])


if __name__ == "__main__":
    main()
