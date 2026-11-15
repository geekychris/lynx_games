"""Bullion Dash (Lynx) - plays it through the harness.

1. The title; A starts level 1; the original's music plays on Mikey; PAUSE
   stops the game (its frame counter doesn't move) and PAUSE goes on.
2. Level 1 with the guards taken out: the bot walks (route search,
   tools/bdlevels.py, the original's movement rules) to the nearest brick it
   can dig, digs it with B + LEFT/RIGHT (the hole is there, and the brick
   grows back after 150 frames), then takes all the gold along the route
   search's moves (100 a gold) and climbs out: LEVEL COMPLETE, +1000.
   Level 2 the same way.
3. Level 3 with its guards: the bot stands still and a guard catches him
   (a life lost); OPTION 1 gives up the other lives (the original's R):
   game over with a new high score; A goes back to the title.
4. The session is recorded and replayed: every frame hash must match.
5. Level 150 is won the same way and the game goes on at level 1 (the
   original's wrap), level 121's five guards chase the bot while the frame
   rate is measured from the game's own "bd: perf" lines.

    ./lynx test bullion_dash        (from the lynxdev checkout)
    .venv/bin/python games/bullion_dash/test.py [--shots] [--log]

--shots writes images/bullion_dash.png, bullion_dash_title.png and
bullion_dash.gif (otherwise every picture goes to build/shots).
"""
import re
import struct
import sys
from pathlib import Path


def _lynxdev_root() -> Path:
    """The lynxdev checkout this game is built in (lynx_games is its games/
    submodule), or $LYNX_ROOT."""
    import os
    if os.environ.get("LYNX_ROOT"):
        return Path(os.environ["LYNX_ROOT"])
    for p in Path(__file__).resolve().parents:
        if (p / "lynx").is_file() and (p / "sdk").is_dir():
            return p
    raise SystemExit("no lynxdev checkout above this game: run it from lynxdev/games/<game> (or set LYNX_ROOT)")


ROOT = _lynxdev_root()
sys.path.insert(0, str(ROOT / "tools" / "lynxdev" / "src"))

from lynxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "tools"))
import bdlevels as bd  # noqa: E402

PROG = HERE / "build" / "bullion_dash.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
IMAGES = HERE / "images" if "--shots" in sys.argv else SHOTS
IMAGES.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
failures = []

# GameState (cc65 packs structs): state, level, score, lives, gold x2, frame;
# the player (11 bytes), 5 guards (14 each), num_enemies, hidden_left, tiles
GS_PLAYER = 15
GS_ENEMY = GS_PLAYER + 11
GS_NUM = GS_ENEMY + 14 * 5
GS_TILES = GS_NUM + 2
DIRS = {"left": "LEFT", "right": "RIGHT", "up": "UP", "down": "DOWN", "fall": None}


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def player(e):
    px, py, gx, gy = struct.unpack("<hhbb", e.read_mem("gs", GS_PLAYER + 6)[GS_PLAYER:])
    return px, py, gx, gy


def grid(e):
    t = e.read_mem("gs", GS_TILES + 448)[GS_TILES:]
    return [[t[y * 28 + x] for x in range(28)] for y in range(16)]


def guards_out(e):
    base = e.addr("gs")
    for i in range(5):
        e.poke(base + GS_ENEMY + 14 * i + 12, 0)         # active = 0
    e.poke(base + GS_NUM, 0)


def start_at(e, level):
    e.write_mem("start_level", struct.pack("<H", level))
    e.press("A")
    return e.run_until(f"bd: game started at level {level}", 60)


def follow(e, g, moves, lv):
    """walk the route search's moves (straight runs merged): hold to each
    run's end, step back to the cell's edge if past it (the game catches up
    2-3 logic ticks after a slow frame). True, or why not."""
    runs = []
    for kind, cell in moves:
        if runs and runs[-1][0] == kind:
            runs[-1][1] = cell
        else:
            runs.append([kind, cell])
    done = f"bd: level {lv} complete"
    mark = e.log_end()
    for kind, (tx, ty) in runs:
        tpx, tpy = tx * 10, ty * 14

        def past(px, py):
            return {"left": px <= tpx, "right": px >= tpx, "up": py <= tpy,
                    "down": py >= tpy, "fall": py >= tpy}[kind]
        e.release()
        if DIRS[kind]:
            e.hold(DIRS[kind])
        for _ in range(400):
            e.run(1)
            px, py, x, y = player(e)
            if past(px, py) or done in e.log_text(mark):
                break
        else:
            e.release()
            return f"stuck going {kind} to {(tx, ty)} at {(x, y)}"
        e.release()
        pg = bd.plain(g)
        if pg[ty][tx] not in (bd.L, bd.R) and not bd.support(pg, tx, ty):
            continue                                     # (he falls from there: the next run)
        for _ in range(40):                              # (back to the cell's edge)
            e.run(1)
            px, py, x, y = player(e)
            if (px, py) == (tpx, tpy) or done in e.log_text(mark):
                break
            if kind == "fall":
                continue
            key = ("LEFT" if px > tpx else "RIGHT") if px != tpx else ("UP" if py > tpy else "DOWN")
            e.press(key, hold_frames=1, release_frames=1)
        else:
            return f"not on {(tx, ty)} after going {kind}: at {(px, py)}"
    return True


def here(e):
    g = grid(e)
    _, _, gx, gy = player(e)
    g[gy][gx] = bd.P
    return g


def win_level(e, lv):
    """the level to the exit along the route search's moves (guards out)"""
    g = here(e)
    order, brk = bd.plan(g)
    if order is None:
        return f"no route: {brk}"
    r = follow(e, g, bd.route(g, order), lv)
    if r is not True:
        return r
    e.hold("UP")
    ok = e.run_until(f"bd: level {lv} complete", 60) or f"bd: level {lv} complete" in e.log()
    e.release()
    return True if ok else "the route ended without the win"


def dig_somewhere(e):
    """walk to the nearest cell with a brick to dig below-left/right, dig it;
    (x, y) of the hole and the dig's log line, or (None, why)"""
    g = here(e)
    pg = bd.plain(g)
    start = bd.start_of(g)
    spots = {}
    for (x, y) in bd.reach(pg, start):
        if not bd.support(pg, x, y) or pg[y][x] == bd.L:
            continue
        for d in (-1, 1):
            if 0 <= x + d < 28 and y + 1 < 16 and pg[y + 1][x + d] == bd.B and bd.enter(pg[y][x + d]):
                spots.setdefault((x, y), d)
    if not spots:
        return None, "no brick to dig"
    p = bd.path(pg, start, set(spots))
    r = follow(e, g, p, 0) if p else True
    if r is not True:
        return None, r
    _, _, gx, gy = player(e)
    d = spots[(gx, gy)]
    e.log()
    e.press("B+" + ("LEFT" if d < 0 else "RIGHT"), hold_frames=4)
    ok = e.run_until("bd: dig", 20)
    e.run(2)
    return (gx + d, gy + 1), (ok, e.log())


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("bd: title", 600))
    e.run(40)
    e.screenshot(IMAGES / ("bullion_dash_title.png" if IMAGES != SHOTS else "title.png"), scale=3)
    pal = e.palette()
    small = e.screenshot(None)
    check("title: BULLION DASH in gold", any(small.getpixel((x, y)) == tuple(pal[7])
                                             for x in range(30, 130) for y in range(4, 12)))
    m = e.hw()["mikey"]
    check("the music plays on Mikey (bass, lead)", m.get("aud0vol", 0) > 0 and m.get("aud1vol", 0) > 0, m)

    e.press("A")                                         # (no memory writes: it is replayed)
    check("A starts a game at level 1", e.run_until("bd: state=playing level=1 lives=5", 60))
    e.run(20)
    hud = e.screenshot(None)
    check("status line: GOLD in yellow", any(hud.getpixel((x, y)) == tuple(pal[7])
                                             for x in range(80, 120) for y in range(97, 102)))

    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("bd: paused=1", 10))
    f0 = e.peek16(e.addr("gs") + 13)
    e.run(50)
    check("paused: the game doesn't advance", e.peek16(e.addr("gs") + 13) == f0)
    e.screenshot(SHOTS / "paused.png", scale=3)
    e.press("PAUSE")
    check("PAUSE again goes on", e.run_until("bd: paused=0", 10))

    # the guard (it waits 3 s, then comes) catches the bot standing still;
    # OPTION 1 gives up the other lives
    e.run(120)
    e.screenshot(SHOTS / "play.png", scale=3)
    mark = e.log_end()
    check("a guard catches him", e.run_until("bd: death caught", 1500), e.log_text(mark)[-300:])
    check("a life lost", e.run_until("bd: lives=4", 60))
    for lives in (3, 2, 1, 0):
        e.run(20)
        e.press("OPTION1")
        check(f"OPTION 1 gives up a life ({lives} left)", e.run_until(f"bd: lives={lives}", 80))
    log = e.log_text(mark) + e.log()
    m = re.search(r"bd: gameover score=(\d+) level=(\d+) hi=(\d+) new=(\d)", log)
    check("game over, no new high score", m is not None and m.group(4) == "0", log[-300:])
    e.run(40)
    go = e.screenshot(None)
    e.screenshot(SHOTS / "gameover.png", scale=3)
    check("GAME OVER in red", any(go.getpixel((x, y)) == tuple(pal[11]) for x in range(40, 120) for y in range(33, 41)))
    e.press("A")
    check("A goes back to the title", e.run_until("bd: title", 60))
    e.run(30)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")

with Emulator() as e:
    e.load(PROG)
    e.run_until("bd: title", 600)
    e.run(10)
    e.press("A")
    e.run_until("bd: state=playing level=1", 60)
    e.run(5)

    # level 1: dig, the brick grows back, the gold, the exit
    guards_out(e)
    hole, r = dig_somewhere(e)
    check("the bot walks to a brick and digs it (B + direction)", hole is not None and r[0], str(r))
    if hole:
        check("the hole is open", grid(e)[hole[1]][hole[0]] == bd.E, str(hole))
        e.screenshot(SHOTS / "dug.png", scale=3)
        e.run(160)
        check("the brick grows back after 150 frames", grid(e)[hole[1]][hole[0]] == bd.B)
    mark = e.log_end()
    r = win_level(e, 1)
    log = e.log_text(mark)
    check("level 1: the route search's moves take all the gold and win it", r is True, str(r))
    golds = re.findall(r"bd: gold=(\d+)/(\d+)", log)
    check("every gold taken (100 points each)", golds and golds[-1][0] == golds[-1][1] == "5"
          and "bd: score=500" in log, str(golds[-3:]))
    e.run(10)
    e.screenshot(SHOTS / "level1-complete.png", scale=3)
    check("LEVEL COMPLETE: +1000 and level 2",
          e.run_until("bd: state=playing level=2 lives=5 score=1500", 120))
    e.run(5)
    guards_out(e)
    r = win_level(e, 2)
    check("level 2 won the same way", r is True, str(r))
    check("level 3", e.run_until("bd: state=playing level=3", 120))
    mark = e.log_end()
    for lives in (4, 3, 2, 1, 0):
        e.run(20)
        e.press("OPTION1")
        e.run_until(f"bd: lives={lives}", 80)
    log = e.log_text(mark) + e.log()
    m = re.search(r"bd: gameover score=(\d+) level=(\d+) hi=(\d+) new=(\d)", log)
    check("game over with a new high score (it starts at 2000)", m and int(m.group(1)) > 2000
          and m.group(4) == "1" and m.group(1) == m.group(3), m.group(0) if m else log[-300:])
    e.run(40)
    e.press("A")
    e.run_until("bd: title", 60)
    e.run(30)
    e.screenshot(SHOTS / "title-hi.png", scale=3)
    t = e.screenshot(None)
    check("the title shows the new high score", any(t.getpixel((x, y)) == tuple(pal[7])
                                                     for x in range(80, 104) for y in range(88, 93)))

with Emulator() as e:
    e.load(PROG)
    e.run_until("bd: title", 600)
    e.run(10)
    check("a game from level 150 (the test's start_level)", start_at(e, 150))
    e.run_until("bd: state=playing level=150", 60)
    e.run(5)
    guards_out(e)
    r = win_level(e, 150)
    check("level 150 won along the route search's moves", r is True, str(r))
    e.screenshot(SHOTS / "level150-won.png", scale=3)
    check("after level 150: level 1 (the original's wrap)", e.run_until("bd: state=playing level=1 ", 120))

    # level 121: five guards; the frame rate while they chase the bot
    e.run(20)
    e.press("PAUSE")
    check("PAUSE pauses level 1", e.run_until("bd: paused=1", 10), e.log()[-300:])
    e.press("OPTION1")                                   # PAUSE + OPTION 1: the title
    ok = e.run_until("bd: title", 30)
    check("PAUSE + OPTION 1: back to the title", ok, e.log()[-400:])
    e.run(10)
    start_at(e, 121)
    e.run_until("bd: state=playing level=121", 60)
    e.run(150)
    n = e.read_mem("gs", GS_NUM + 1)[GS_NUM]
    check("level 121: five guards", n == 5, str(n))
    e.screenshot(IMAGES / ("bullion_dash.png" if IMAGES != SHOTS else "level121-start.png"), scale=3)
    e.log()
    gif = []
    for k in range(48):
        e.release()
        e.hold(["LEFT", "RIGHT", "UP", "DOWN"][(k // 3) % 4])
        for _ in range(5):
            e.run(5)
            if k < 24:
                gif.append(e.screenshot(None, scale=2).convert("RGB"))
    e.release()
    log = e.log()
    small = [g.convert("P", palette=1, colors=32) for g in gif]
    small[0].save(IMAGES / "bullion_dash.gif", save_all=True, append_images=small[1:],
                  duration=100, loop=0, optimize=True)
    e.screenshot(SHOTS / "level121.png", scale=3)
    vbl = [int(v) for v in re.findall(r"bd: perf frames=100 vbl=(\d+) guards=5", log)]
    fps = [round(5000 / v, 1) for v in vbl]
    print(f"  screens a second on level 121 (five guards), the rules at 50 Hz: {fps}")
    check("frame rate with five guards: 40+ screens a second", fps and min(fps) >= 40, str(fps))

if "--log" in sys.argv:
    print(log)
print(f"{'FAIL' if failures else 'PASS'}: bullion_dash")
sys.exit(1 if failures else 0)
