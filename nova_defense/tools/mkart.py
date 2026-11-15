#!/usr/bin/env python3
"""Nova Defense (Lynx): sprite data (src/art.c, src/art.h).

The Amiga original (amiga_mcp/examples/nova_defense/draw.c) draws its
aliens, gun and comet from character art on a 320 x 256 screen. The Lynx
shows the original's world at half size (160 pixels across), so each
picture here is the original's at half size: the aliens 7 x 5 (were
14 x 10), the gun 7 x 4 (15 x 8), the comet 8 x 4 (16 x 7). They were
made by halving the original art (2 x 2 blocks) and then touched up by hand
so the shapes still read at that size: eye pods with trailing tendrils,
rays beating their wings, spore crawlers walking, the delta gun with its
cockpit, the comet's head and tail.

Pens: 1 = the body colour, 2 = the accent colour (each SCB's pen palette
gives the colours, as the original's body/accent pair).

Also: the expanding-cross explosions (drawn at world resolution as the
original, then halved), the original's 5x7 font as sprites (title and
messages), the crystal shield halved (16 x 7), the bullets.

Run: python3 tools/mkart.py [--show]
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

# '#' body (pen 1), 'o' accent (pen 2)
ALIENS = {
    "alien_c": [  # eye pod trailing tendrils (30 points)
        [".#####.",
         "##o.o##",
         ".#####.",
         ".#.#.#.",
         "#..#..#"],
        [".#####.",
         "#o.o###",
         ".#####.",
         "..#.#..",
         ".#.#.#."],
    ],
    "alien_b": [  # ray beating its wings (20 points)
        ["#.....#",
         "##.#.##",
         "#######",
         ".#o#o#.",
         "..#.#.."],
        [".......",
         "..###..",
         "#######",
         "#.o#o.#",
         "..#.#.."],
    ],
    "alien_a": [  # spore crawler (10 points)
        ["...#...",
         "..#o#..",
         ".#####.",
         "#.#.#.#",
         "#.....#"],
        ["...#...",
         "..#o#..",
         ".#####.",
         ".#.#.#.",
         "..#.#.."],
    ],
}
PLAYER = ["...#...",
          "..#o#..",
          ".#####.",
          "#######"]
COMET = ["....oo..",
         "##ooooo.",
         "..######",
         "##ooooo."]
PBULLET = ["o", "#", "#"]          # white tip (pen 2), cyan (pen 1)
ABULLET = [[".#.", "###", ".#."],  # the spinning spore: a plus and an x
           ["#.#", ".#.", "#.#"]]

FONT = {  # the original's 5x7 font, bits 4-0 = columns left to right
    "0": [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E], "1": [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E],
    "2": [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F], "3": [0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E],
    "4": [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02], "5": [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
    "6": [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E], "7": [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
    "8": [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E], "9": [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C],
    "A": [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11], "B": [0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E],
    "C": [0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E], "D": [0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C],
    "E": [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F], "F": [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10],
    "G": [0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F], "H": [0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
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


def grid(art):
    return [[2 if c == "o" else 1 if c == "#" else 0 for c in row] for row in art]


def shield():
    """The original's crystal hexagon (32 x 14: pointed top and bottom, a
    centre ridge and diagonal facets) redrawn at half size, 16 x 7.
    1 = crystal, 2 = facet. (Halving the original pixel by pixel turned its
    facets into noise.)"""
    inset = [5, 3, 1, 0, 0, 1, 3]
    return [[(2 if (x == 8 or (x + y) % 6 == 2 or (x - y + 12) % 6 == 2) else 1)
             if inset[y] <= x < 16 - inset[y] else 0 for x in range(16)] for y in range(7)]


def explosion(size):
    """The original's explosion at size 0..9 (world pixels; size = 10 -
    timer): a cross of 3-pixel bars with arms `size` long, and diagonal dots
    at size - 1 from size 3. At half size on an 11 x 11 grid, centre (5, 5):
    1-pixel bars, arms (size + 1) / 2."""
    g = [[0] * 11 for _ in range(11)]
    c, a = 5, (size + 1) // 2
    t = 0
    if size > 0:
        for y in range(c - t, c + t + 1):
            for x in range(c - a, c + a + 1):
                g[y][x] = 1
        for y in range(c - a, c + a + 1):
            for x in range(c - t, c + t + 1):
                g[y][x] = 1
    if size > 2:
        d = size // 2
        for x, y in ((c - d, c - d), (c + d, c - d), (c - d, c + d), (c + d, c + d)):
            g[y][x] = 1
    return g


def main():
    c = ["/* art.c: made by tools/mkart.py - don't edit, re-run it. */", '#include "art.h"', ""]
    h = ["/* art.h: made by tools/mkart.py - don't edit, re-run it. */", "#ifndef ART_H", "#define ART_H", ""]

    def arr(name, data):
        c.append(f"static const unsigned char {name}[{len(data)}] = {{ " + ", ".join(f"0x{b:02X}" for b in data) + " };")

    def frames(name, grids, bpp):
        for i, g in enumerate(grids):
            arr(f"{name}_{i}", encode(g, bpp))
        c.append(f"const unsigned char * const {name}_frames[{len(grids)}] = {{ " +
                 ", ".join(f"{name}_{i}" for i in range(len(grids))) + " };")
        c.append("")
        h.append(f"extern const unsigned char * const {name}_frames[{len(grids)}];")

    # aliens: [type * 2 + frame], type 0 = C (top rows) .. 2 = A
    al = [grid(f) for n in ("alien_c", "alien_b", "alien_a") for f in ALIENS[n]]
    frames("alien", al, 2)
    frames("player", [grid(PLAYER)], 2)
    frames("comet", [grid(COMET)], 2)
    frames("pbullet", [grid(PBULLET)], 2)
    frames("abullet", [grid(f) for f in ABULLET], 1)
    frames("boom", [explosion(s) for s in range(10)], 1)
    frames("glyph", [[[1 if bits & (0x10 >> x) else 0 for x in range(5)] for bits in FONT[ch]] for ch in GLYPHS], 1)
    sh = shield()
    c.append("const unsigned char shield_pattern[7][16] = {")
    c += ["    { " + ", ".join(str(v) for v in row) + " }," for row in sh]
    c.append("};")
    h.append("extern const unsigned char shield_pattern[7][16];")
    h += ["", "#endif", ""]
    (HERE / "src" / "art.c").write_text("\n".join(c) + "\n")
    (HERE / "src" / "art.h").write_text("\n".join(h))
    if "--show" in sys.argv:
        for row in sh:
            print("".join(".#o"[v] for v in row))
        for s in (3, 6, 9):
            for row in explosion(s):
                print("".join(".#"[v] for v in row))
            print()


if __name__ == "__main__":
    main()
