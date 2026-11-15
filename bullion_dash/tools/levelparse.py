"""The level file format of the ZX Spectrum SDK's levelpack.py (parse only):
`size W H`, `legend <char> <id> ...`, then `room NAME` and H lines of W chars."""


class LevelError(ValueError):
    pass


def parse(text):
    w = h = None
    legend = {}
    rooms = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        raw = lines[i]
        i += 1
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        word, _, rest = line.partition(" ")
        if word == "size":
            w, h = (int(v) for v in rest.split())
        elif word == "legend":
            toks = rest.split()
            if len(toks) % 2:
                raise LevelError(f"line {i}: legend needs character/id pairs")
            for ch, v in zip(toks[::2], toks[1::2]):
                if len(ch) != 1:
                    raise LevelError(f"line {i}: legend key {ch!r} is not one character")
                legend[ch] = int(v, 0)
        elif word == "room":
            if w is None or not legend:
                raise LevelError(f"line {i}: `size` and `legend` come before the first room")
            name = rest.strip()
            if not name:
                raise LevelError(f"line {i}: a room needs a name")
            cells = bytearray()
            for r in range(h):
                if i >= len(lines):
                    raise LevelError(f"room {name!r}: ends after {r} rows, needs {h}")
                row = lines[i].rstrip("\n")
                i += 1
                row = row.split(None, 1)[0] if row.strip() else ""
                if len(row) != w:
                    raise LevelError(f"room {name!r} row {r}: {len(row)} cells, needs {w}")
                for ch in row:
                    if ch not in legend:
                        raise LevelError(f"room {name!r} row {r}: {ch!r} is not in the legend")
                    cells.append(legend[ch])
            rooms.append((name, bytes(cells)))
        else:
            raise LevelError(f"line {i}: unknown {word!r}")
    if not rooms:
        raise LevelError("no rooms")
    return w, h, rooms
