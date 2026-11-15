#!/usr/bin/env python3
"""Orb Hunter: the title's and game over's big words from the original's own
5x7 font (draw.c font_5x7, drawn there at scale 3; here 1 bpp art that Suzy
scales 2x). Writes art/logo.txt ("ORB HUNTER") and art/gameover.txt
("GAME OVER"); then: ./lynx sprite games/orb_hunter/art/logo.txt --bpp 1 -o
games/orb_hunter/src/spr_logo.c (and the same for gameover).
Run: python3 tools/mkart.py
"""
import os
import re
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ORIG = Path(os.environ.get("ORB_HUNTER_SRC",
                           Path.home() / "code/claude_world/amiga_mcp/examples/orb_hunter"))
src = (ORIG / "draw.c").read_text()
body = src[src.index("font_5x7[96][7]"):]
glyphs = [[int(v, 16) for v in g.split(",")]
          for g in re.findall(r"\{(0x[0-9A-Fa-f]{2}(?:,0x[0-9A-Fa-f]{2}){6})\}", body)[:96]]


def word(text, name):
    rows = []
    for r in range(7):
        line = ""
        for ch in text:
            bits = glyphs[ord(ch) - 32][r]
            line += "".join("1" if bits & (0x10 >> c) else "." for c in range(5)) + "."
        rows.append(line[:-1])
    (HERE / "art" / f"{name}.txt").write_text(
        f"# \"{text}\" in the original's 5x7 font (tools/mkart.py)\n" + "\n".join(rows) + "\n")


word("ORB HUNTER", "logo")
word("GAME OVER", "gameover")
print("art/logo.txt, art/gameover.txt")
