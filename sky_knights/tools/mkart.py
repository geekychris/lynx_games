#!/usr/bin/env python3
"""Sky Knights (Lynx): sprite data and tables (src/art.c, src/art.h).

The Amiga original (amiga_mcp/examples/sky_knights/draw.c) draws a mounted
knight from rectangles in a 20 x 22 box (the knight's head 2 lines above
its position) on a 320 x 256 screen. The Lynx shows that world at x / 2,
y * 2 / 5, so a knight here is 10 x 9: the rider (head, body, arm and lance
forward), the mount (head and yellow beak forward, body, wings, legs).
Four pictures, as the original's: wings up (flapping) or down, and the two
leg steps. They face right; facing left is the same picture flipped by Suzy.
Pens: 1 = mount, 2 = rider, 3 = white (the lance), 4 = yellow (the beak);
each SCB's pen palette gives the mount and rider colours, as the original's
body_color / knight_color pair.

Also: the pod (8 x 8 -> 4 x 3), the original's 5x7 font as sprites (title
and messages, Suzy-scaled), and ytab, the world-y -> screen-y table.

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

# 'm' mount (pen 1), 'k' rider (pen 2), 'w' white lance (3), 'y' yellow beak (4)
# row 0 is the rider's head (the original's y - 2), rows 1..8 the knight's
# 20 lines at 2/5. Facing right.
WINGS_DOWN = ["....kk....",
              "...kkkk...",
              "...kkkwwww",
              "...kkk.mmy",
              ".mmmmmmmm.",
              "mmmmmmmmmm",
              "m.mmmmmm.m"]
WINGS_UP = ["....kk....",
            "mm.kkkk...",
            ".mmkkkwwww",
            "..mkkk.mmy",
            ".mmmmmmmm.",
            ".mmmmmmmm.",
            "..mmmmmm.."]
LEGS = [["...m..m...",           # the original's anim_frame 0: front leg long
         "...m......"],
        ["...m..m...",
         "......m..."]]

POD = [".##.",
       "####",
       ".##."]

FONT = {  # the original's 5x7 font (draw.c font_5x7), bits 4-0 = columns left to right
    "!": [0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04],
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
GLYPHS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!"

PENS = {".": 0, "m": 1, "#": 1, "k": 2, "w": 3, "y": 4}


def grid(art):
    return [[PENS[c] for c in row] for row in art]


def knights():
    """[flapping * 2 + legs]"""
    return [grid((WINGS_UP if flap else WINGS_DOWN) + LEGS[legs]) for flap in (0, 1) for legs in (0, 1)]


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

    frames("knight", knights(), 3)
    frames("pod", [grid(POD)], 1)
    frames("glyph", [[[1 if bits & (0x10 >> x) else 0 for x in range(5)] for bits in FONT[ch]] for ch in GLYPHS], 1)
    # world y (0..255) -> screen line, rounded: y * 2 / 5
    ytab = [min(101, (2 * y + 2) // 5) for y in range(256)]
    c.append("const unsigned char ytab[256] = {")
    c += ["    " + ", ".join(str(v) for v in ytab[i:i + 16]) + "," for i in range(0, 256, 16)]
    c.append("};")
    h.append("extern const unsigned char ytab[256];")
    h += ["#define KNIGHT_W 10", "#define KNIGHT_H 9", "", "#endif", ""]
    (HERE / "src" / "art.c").write_text("\n".join(c) + "\n")
    (HERE / "src" / "art.h").write_text("\n".join(h))
    if "--show" in sys.argv:
        for g in knights():
            for row in g:
                print("".join(".mkwy"[v] for v in row))
            print()


if __name__ == "__main__":
    main()
