#!/usr/bin/env python3
"""Miner Meteor: src/levels.txt (the original's caverns) -> src/levels_data.c,
the 20 x 15 tile kinds of each cavern as bytes (1.8 KB) and its name.
Run: python3 tools/mklevels.py"""
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
w = h = None
legend, rooms, cur = {}, [], None
for line in (HERE / "src" / "levels.txt").read_text().splitlines():
    if not line.strip() or line.startswith(";"):
        continue
    word = line.split()[0]
    if word == "size":
        w, h = int(line.split()[1]), int(line.split()[2])
    elif word == "legend":
        parts = line.split()[1:]
        legend = {parts[i]: int(parts[i + 1]) for i in range(0, len(parts), 2)}
    elif word == "room":
        cur = [line.split(None, 1)[1], []]
        rooms.append(cur)
    else:
        assert len(line) == w, line
        cur[1].append([legend[c] for c in line])
out = ["/* made by tools/mklevels.py from levels.txt - edit that */", "#include <stdint.h>", '#include "levels_data.h"', ""]
for i, (name, rows) in enumerate(rooms):
    assert len(rows) == h, name
    data = [v for r in rows for v in r]
    out.append(f"static const uint8_t lv_{i}[{w * h}] = {{  /* {name} */")
    for k in range(0, len(data), 20):
        out.append("    " + ", ".join(map(str, data[k:k + 20])) + ",")
    out.append("};")
out.append(f"const uint8_t *const lv_maps[{len(rooms)}] = {{ " + ", ".join(f"lv_{i}" for i in range(len(rooms))) + " };")
out.append(f"const char *const lv_names[{len(rooms)}] = {{ " + ", ".join(f'"{n}"' for n, _ in rooms) + " };")
(HERE / "src" / "levels_data.c").write_text("\n".join(out) + "\n")
(HERE / "src" / "levels_data.h").write_text(
    f"/* made by tools/mklevels.py */\n#ifndef LV_DATA_H\n#define LV_DATA_H\n#include <stdint.h>\n"
    f"#define LV_COUNT {len(rooms)}\n#define LV_W {w}\n#define LV_H {h}\n"
    f"extern const uint8_t *const lv_maps[LV_COUNT];\nextern const char *const lv_names[LV_COUNT];\n#endif\n")
print(len(rooms), "caverns")
