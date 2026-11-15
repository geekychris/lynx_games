#!/usr/bin/env python3
"""Miner Meteor (Lynx): the original's tile sheet -> src/art.c, src/art.h.

The original (amiga_mcp/examples/miner_meteor) draws everything from
tilesheet_data.cpp: its art/tilesheet.png baked into 16 x 16 tiles of its
16-colour palette (tools/bake_tilesheet.py). art/sheet16.txt holds the tiles
this game uses, copied out of that file (`--from <tilesheet_data.cpp>`
rewrites it). Here each 16 x 16 tile becomes 8 x 7 Lynx pixels (the view is
the original's 320 x 240 at half width and 7/16 height): each Lynx pixel
takes the colour that covers most of its 2 x 2.29 source pixels, a colour
beating black when it covers a third.

- Tiles: black is pen 0 (transparent): a tile is drawn over a black box.
- Sprites (miner, guardians): black connected to the tile's edge is pen 0;
  black inside the figure (eyes, mouths) is colour 13, a second black
  (port: the original's 13, light green, is only in decorations this game
  doesn't draw), because pen 0 can't be drawn by a normal sprite.

Run: python3 tools/mkart.py [--from path/to/tilesheet_data.cpp] [--preview out.png]
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent


def _lynxdev_root() -> Path:
    import os
    if os.environ.get("LYNX_ROOT"):
        return Path(os.environ["LYNX_ROOT"])
    for p in HERE.parents:
        if (p / "lynx").is_file() and (p / "sdk").is_dir():
            return p
    raise SystemExit("no lynxdev checkout above this game")


sys.path.insert(0, str(_lynxdev_root() / "tools" / "lynxdev" / "src"))
from lynxdev.sprite import encode  # noqa: E402

# the original's palette (render.cpp PALETTE), 8 bits a channel
PALETTE = [
    (0, 0, 0), (240, 240, 240), (168, 48, 48), (128, 88, 40),
    (64, 208, 224), (224, 80, 192), (240, 224, 64), (64, 200, 64),
    (72, 72, 80), (240, 64, 64), (240, 160, 64), (80, 128, 240),
    (128, 128, 128), (168, 224, 128), (240, 168, 200), (40, 40, 40),
]
BLACK2 = 13            # port: a drawable black (see above)

# (name, sheet row, sheet col) of the tiles used: render.cpp's SR_*/SC_*
TILES = [("empty", 0, 0), ("solid_a", 0, 1), ("solid_b", 0, 2), ("collapse", 0, 3),
         ("conv_l", 0, 4), ("conv_r", 0, 5), ("hazard", 0, 6), ("key", 0, 7),
         ("exit_locked", 1, 0), ("exit_open", 1, 1)]
SPRITES = ([(f"player_r{i}", 2, i) for i in range(4)] + [(f"player_l{i}", 3, i) for i in range(4)]
           + [("player_jump", 4, 0), ("player_fall", 4, 1)]
           + [(f"guard_{g}{i}", 5 + k, i) for k, g in enumerate("abc") for i in range(8)])
SHEET = HERE / "art" / "sheet16.txt"


def extract(cpp: Path):
    """tilesheet_data.cpp -> art/sheet16.txt (the tiles in TILES + SPRITES)"""
    body = cpp.read_text()
    body = re.sub(r"/\*.*?\*/", "", body[body.index("= {"):], flags=re.S)
    nums = [int(v) for v in re.findall(r"\b\d+\b", body)]
    assert len(nums) >= 8 * 20 * 256
    out = ["# Miner Meteor: the original's tiles (amiga_mcp/examples/miner_meteor/tilesheet_data.cpp,",
           "# baked from its art/tilesheet.png), one hex digit a pixel = its palette index.",
           "# Written by tools/mkart.py --from <tilesheet_data.cpp>."]
    for name, r, c in TILES + SPRITES:
        base = (r * 20 + c) * 256
        out.append(f"tile {name} {r} {c}")
        for y in range(16):
            out.append("".join("%x" % nums[base + y * 16 + x] for x in range(16)))
    SHEET.write_text("\n".join(out) + "\n")


def load():
    tiles, cur = {}, None
    for line in SHEET.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        if line.startswith("tile "):
            cur = []
            tiles[line.split()[1]] = cur
        else:
            cur.append([int(ch, 16) for ch in line])
    return tiles


def outside_black(g):
    """black pixels connected to the tile's edge"""
    seen = set()
    stack = [(x, y) for y in range(16) for x in range(16) if (x in (0, 15) or y in (0, 15)) and g[y][x] == 0]
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < 16 and 0 <= y < 16) or g[y][x] != 0:
            continue
        seen.add((x, y))
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return seen


def shrink(g, sprite):
    """16 x 16 palette indices -> 8 x 7 pens (0 = transparent)"""
    src = [[(7 if v == 13 else v) for v in row] for row in g]     # light green -> green
    if sprite:
        out_b = outside_black(src)
        src = [[(BLACK2 if v == 0 and (x, y) not in out_b else v) for x, v in enumerate(row)]
               for y, row in enumerate(src)]
    res = []
    for ty in range(7):
        y0, y1 = ty * 16 / 7, (ty + 1) * 16 / 7
        row = []
        for tx in range(8):
            w = {}
            for y in range(int(y0), min(16, int(y1 + 0.999))):
                wy = min(y + 1, y1) - max(y, y0)
                if wy <= 0:
                    continue
                for x in (2 * tx, 2 * tx + 1):
                    w[src[y][x]] = w.get(src[y][x], 0) + wy
            tot = sum(w.values())
            col = [(v, k) for k, v in w.items() if k != 0]
            best = max(col) if col else (0, 0)
            row.append(best[1] if best[0] >= tot / 3 else 0)
        res.append(row)
    return res


def main():
    args = sys.argv[1:]
    if "--from" in args:
        extract(Path(args[args.index("--from") + 1]))
    tiles = load()
    shr = {}
    for name, _, _ in TILES:
        shr[name] = shrink(tiles[name], False)
    # the collapsing floor in the second half of its timer: render.cpp swaps
    # brown (3) for dim (15)
    shr["collapse_dim"] = [[15 if v == 3 else v for v in row] for row in shr["collapse"]]
    for name, _, _ in SPRITES:
        shr[name] = shrink(tiles[name], True)
    # a black box (colour 13) to erase a cell before its tile is drawn again
    shr["black"] = [[BLACK2] * 8 for _ in range(7)]

    c = ["/* art.c: made by tools/mkart.py from art/sheet16.txt (the original's tile sheet) - don't edit, re-run it. */",
         '#include "art.h"', ""]
    for name, g in shr.items():
        data = encode(g, 4)
        c.append(f"const unsigned char art_{name}[{len(data)}] = {{ " + ", ".join("0x%02X" % b for b in data) + " };")
    c.append("")
    c.append("const unsigned char * const art_player[2][4] = {")
    for d in "lr":
        c.append("    { " + ", ".join(f"art_player_{d}{i}" for i in range(4)) + " },")
    c.append("};")
    c.append("const unsigned char * const art_guard[3][8] = {")
    for g in "abc":
        c.append("    { " + ", ".join(f"art_guard_{g}{i}" for i in range(8)) + " },")
    c.append("};")
    rgb = ["0x%03X" % ((r // 16) << 8 | (g // 16) << 4 | (b // 16)) for r, g, b in PALETTE]
    rgb[BLACK2] = "0x000"
    c.append("const uint16_t art_palette[16] = { " + ", ".join(rgb) + " };")
    (HERE / "src" / "art.c").write_text("\n".join(c) + "\n")

    h = ["/* art.h: made by tools/mkart.py - don't edit, re-run it. */",
         "#ifndef ART_H", "#define ART_H", "#include <stdint.h>", "",
         "/* 8 x 7 pictures, 4 bpp literal; tiles: pen 0 where the original is black */"]
    for name, g in shr.items():
        h.append(f"extern const unsigned char art_{name}[{len(encode(g, 4))}];")
    h += ["extern const unsigned char * const art_player[2][4];   /* [facing][walk frame] */",
          "extern const unsigned char * const art_guard[3][8];    /* [colour 4/5/6][frame] */",
          "extern const uint16_t art_palette[16];                 /* 0xRGB: the original's, 13 = black */",
          f"#define COL_BLACK2 {BLACK2}", "", "#endif"]
    (HERE / "src" / "art.h").write_text("\n".join(h) + "\n")

    if "--preview" in args:
        from PIL import Image
        names = list(shr)
        im = Image.new("RGB", (len(names) * 10, 9), (60, 0, 60))
        for i, n in enumerate(names):
            for y, row in enumerate(shr[n]):
                for x, v in enumerate(row):
                    im.putpixel((i * 10 + 1 + x, 1 + y), (255, 0, 255) if v == 0 and n not in [t[0] for t in TILES] else
                                (0, 0, 0) if v == BLACK2 else PALETTE[v])
        im.resize((im.width * 8, im.height * 8), Image.NEAREST).save(args[args.index("--preview") + 1])
    print(f"wrote src/art.c, src/art.h ({len(shr)} pictures)")


if __name__ == "__main__":
    main()
