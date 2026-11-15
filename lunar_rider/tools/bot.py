"""Lunar Rider (Lynx): a bot that drives the buggy (used by test.py).

It reads the game's state by symbol (the terrain ring, the scroll, the
buggy) and plans the next frames with the game's own rules (game.c
update_scrolling + update_buggy, in the original's units): a depth-first
search over "jump now or not" (and full or normal speed) on the frames the
buggy is on the ground, for a plan that survives the craters, rocks and
mines of the next HORIZON frames. It holds A (both guns) all the time, so
rocks in front and UFOs overhead are shot; it ignores the bombs.
"""
import struct

TERRAIN_LEN = 640
GROUND_Y, BUGGY_H, BUGGY_X, BUGGY_W = 200, 12, 80, 24
CRATERS, ROCK, MINE = (1, 2), 3, 5
SPEEDS = {"RIGHT": 300, "": 150, "LEFT": 50}
HORIZON = 70


def trunc100(v):
    q = abs(v) // 100
    return q if v >= 0 else -q


def read_state(e):
    ring = struct.unpack("<H", e.read_mem("scroll_ring", 2))[0]
    speed = struct.unpack("<H", e.read_mem("scroll_speed", 2))[0]
    y, vy = struct.unpack("<hh", e.read_mem("buggy_y", 2) + e.read_mem("buggy_vy", 2))
    return {"ring": ring, "frac": e.peek("scroll_frac"), "speed": speed, "y": y, "vy": vy,
            "og": e.peek("buggy_on_ground"), "alive": e.peek("buggy_alive"), "state": e.peek("state")}


def step(t, th, s, key, jump):
    """one frame of update_scrolling + update_buggy; returns the new state
    or None if the buggy dies"""
    ring, frac, speed, y, vy, og = s
    f = frac + speed
    ring = (ring + f // 100) % TERRAIN_LEN
    frac = f % 100
    speed = SPEEDS[key]
    if jump and og:
        vy, og = -600, 0
    if not og:
        vy += 40
    y += trunc100(vy)
    idx = (ring + BUGGY_X + BUGGY_W // 2) % TERRAIN_LEN
    gh = th[idx] - BUGGY_H
    if y >= gh:
        if t[idx] in CRATERS:
            if (og or vy > 0) and y >= GROUND_Y - BUGGY_H + 5:
                return None
        else:
            y, vy, og = gh, 0, 1
    else:
        og = 0
    if t[(ring + BUGGY_X + BUGGY_W) % TERRAIN_LEN] == ROCK and og:
        return None
    if t[idx] == MINE and og:
        return None
    return (ring, frac, speed, y, vy, og)


def plan(t, th, s0, keys=("RIGHT", "")):
    """a list of (key, jump) for the next HORIZON frames that doesn't die on
    the terrain, or the longest-surviving one"""
    best = [[]]
    seen = set()

    def dfs(s, depth, path):
        if depth == HORIZON:
            best[0] = list(path)
            return True
        if len(path) > len(best[0]):
            best[0] = list(path)
        k = (depth, s)
        if k in seen:
            return False
        seen.add(k)
        options = [(key, False) for key in keys]
        if s[5]:                         # on the ground: may jump
            options += [(key, True) for key in keys]
        for key, jump in options:
            n = step(t, th, s, key, jump)
            if n is None:
                continue
            path.append((key, jump))
            if dfs(n, depth + 1, path):
                return True
            path.pop()
        return False

    dfs(s0, 0, [])
    return best[0]


class Bot:
    """bot = Bot(); each frame: keys = bot.keys(e) -> e.hold(keys); e.run(1)"""

    def __init__(self, jump=True):
        self.jump = jump
        self.queue = []
        self.expect = None

    def keys(self, e):
        s = read_state(e)
        if s["state"] not in (1, 4) or not s["alive"] or not self.jump:
            self.queue = []
            return "A"
        cur = (s["ring"], s["frac"], s["speed"], s["y"], s["vy"], s["og"])
        if not self.queue or cur != self.expect:
            t = e.read_mem("terrain", TERRAIN_LEN)
            th = e.read_mem("terrain_h", TERRAIN_LEN)
            self.terrain = (t, th)
            self.queue = plan(t, th, cur)[:12] or [("", True)]
            self.state0 = cur
        key, jump = self.queue.pop(0)
        n = step(*self.terrain, cur, key, jump)
        self.expect = n
        return "+".join(x for x in ("A", key, "UP" if jump else "") if x)
