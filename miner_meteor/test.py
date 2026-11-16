"""Miner Meteor (Lynx) - plays the game through the harness.

1. The title, then a bot game (recorded from reset): the bot follows the
   ZX Spectrum port's solver route through cavern 1 (keys, the exit opening,
   the cavern bonus); in cavern 2 PAUSE stops the game; then the miner walks
   into the spikes until the game is over (a life lost each time), GAME
   OVER, and A goes back to the title. The recording is replayed: every
   frame hash must match.
2. The routes through all six caverns (tools/routes.json, found by a search
   over the same game logic, which test 1 of the a8dev port also plays):
   each cavern cleared without a death, its score the original's rule
   (100 a key, 1000 a cavern plus half the air left), ALL CAVERNS CLEARED,
   and the frame rate from the game's own "mm: fps=" lines.
3. The drawing: the same routes again with the whole cavern redrawn every
   frame; at 60 points the buffer just drawn must be byte for byte the one
   the dirty-cell redraw made in run 2.
4. PAUSE, OPTION 1 gives up (back to the title).

The bot sets the pad at a breakpoint on input_update() from the game's
tick counter (gs.level_ticks), a frame ahead (the pad is read at the next
frame), so a late frame can't put it out of step with the route.

    .venv/bin/python games/miner_meteor/test.py [--shots] [--log]

--shots also writes images/miner_meteor.png, miner_meteor_title.png and
miner_meteor.gif (otherwise every picture goes to build/shots).
"""
import hashlib
import json
import re
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
PROG = HERE / "build" / "miner_meteor.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
DOCS = HERE / "images" if "--shots" in sys.argv else SHOTS
DOCS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
failures = []

# the original's palette indices
WHITE, YELLOW, GREEN, RED, BLUE = 1, 6, 7, 9, 11
# gs (game.h): +0 mode, +1 level_index, +2 lives, +3 keys_remaining,
# +5 level_ticks, +7 score, +11 air, +13 state_timer, +15 player x, +17 y,
# +25 player walk frame
GM_TITLE, GM_PLAYING, GM_WIN, GM_LOSE, GM_LEVELWIN = 0, 1, 2, 3, 4

ROUTES = []
for cavern in json.loads((HERE / "tools" / "routes.json").read_text()):
    ROUTES.append([f for f, n in cavern for _ in range(n)])


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def has_colour(e, idx, box):
    pal = e.palette()
    im = e.screenshot(None)
    x0, y0, x1, y1 = box
    return any(im.getpixel((x, y)) == tuple(pal[idx]) for x in range(x0, x1) for y in range(y0, y1))


def state(e):
    m = e.read_mem(e.addr("_gs"), 26)
    return {"mode": m[0], "level": m[1], "lives": m[2], "keys": m[3], "lt": m[5] | m[6] << 8,
            "score": int.from_bytes(m[7:11], "little"), "air": m[11] | m[12] << 8,
            "timer": m[13] | m[14] << 8, "x": int.from_bytes(m[15:17], "little", signed=True),
            "frame": m[25]}


def keys_of(flags):
    return "+".join(k for b, k in ((1, "LEFT"), (2, "RIGHT"), (4, "A")) if flags & b)


def route_bot(e, stop, on_present=None, force_full=False, max_loops=8000):
    """Play the routes from the title until stop(log, state) is true.
    Returns the log. on_present(state) is called at each gfx_present()."""
    e.break_at("_input_update")
    if on_present:
        e.break_at("_gfx_present")
    log, held, started, loops = "", None, 0, 0
    nf = e.addr("_need_full")
    while loops < max_loops:
        r = e.continue_(10)
        log += e.log()
        s = state(e)
        if r.get("symbol") == "_gfx_present":
            on_present(s)
            continue
        loops += 1
        if stop(log, s):
            break
        if force_full:
            e.write_mem(nf, b"\x01\x01")
        # the input of the next loop's tick (the pad is read a frame later)
        k = ""
        if s["mode"] == GM_TITLE:
            k = "A" if started == 0 else keys_of(ROUTES[0][0])
            started += 1
        elif s["mode"] == GM_PLAYING:
            rt = ROUTES[s["level"]]
            k = keys_of(rt[s["lt"] + 1]) if s["lt"] + 1 < len(rt) else ""
        elif s["mode"] == GM_LEVELWIN and s["timer"] == 0 and s["level"] < len(ROUTES):
            k = keys_of(ROUTES[s["level"]][0])
        if k != held:
            e.release()
            if k:
                e.hold(k)
            held = k
    e.delete()
    if on_present:
        e.delete()
    e.release()
    return log


def to_title(e):
    check("boots to the title", e.run_until("mm: title", 600))
    e.run(20)                                     # (the title is drawn into both buffers)


# ------------------------------------------------------------ 1: the bot game (recorded)

with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    to_title(e)
    e.run(40)
    e.screenshot(DOCS / ("miner_meteor_title.png" if DOCS != SHOTS else "title.png"), scale=3)
    check("title: MINER METEOR on the blue bar", has_colour(e, BLUE, (0, 10, 160, 28)) and has_colour(e, WHITE, (30, 15, 130, 23)))
    log = route_bot(e, lambda log, s: s["mode"] == GM_PLAYING and s["level"] == 1)
    if "--log" in sys.argv:
        print(log)
    check("A starts the game", "mm: start" in log and "mm: level=1 lives=3 keys=4 score=0" in log)
    check("jumps", "mm: jump" in log)
    keys = re.findall(r"mm: key left=(\d) score=(\d+)", log)
    check("cavern 1: four keys, 100 each", [k for k, _ in keys] == ["3", "2", "1", "0"] and
          [int(v) for _, v in keys] == [100, 200, 300, 400], str(keys))
    m = re.search(r"mm: cleared level=1 score=(\d+) air=(\d+) ticks=(\d+)", log)
    check("cavern 1 cleared: 1000 + half the air", m is not None and int(m.group(1)) == 400 + 1000 + int(m.group(2)) // 2,
          m and m.group(0))
    check("on to cavern 2", "mm: level=2 lives=3 keys=3" in log)
    e.run(10)
    e.screenshot(SHOTS / "cavern2.png", scale=3)
    check("cavern 2 drawn: spikes (grey), the HUD's air bar (green)",
          has_colour(e, 12, (0, 0, 160, 90)) and has_colour(e, GREEN, (51, 94, 111, 99)))

    # pause: the miner doesn't move
    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("mm: paused=1", 10))
    x0 = state(e)["x"]
    e.hold("RIGHT")
    e.run(50)
    e.release()
    check("paused: the miner doesn't move", state(e)["x"] == x0)
    check("PAUSED on the screen", has_colour(e, YELLOW, (50, 40, 110, 48)))
    e.press("PAUSE")
    check("PAUSE again goes on", e.run_until("mm: paused=0", 10))

    # into the spikes until the game is over
    log, t = "", 0
    while "mm: gameover" not in log and t < 3000:
        e.hold("RIGHT")
        e.run(10)
        t += 10
        log += e.log()
    e.release()
    if "--log" in sys.argv:
        print(log)
    died = [int(v) for v in re.findall(r"mm: died lives=(\d)", log)]
    check("the spikes kill: a life lost each time (2, 1, 0)", died == [2, 1, 0], str(died))
    check("the cavern restarts with a life fewer", "mm: level=2 lives=2" in log and "mm: level=2 lives=1" in log)
    m = re.search(r"mm: gameover score=(\d+)", log)
    check("game over with cavern 1's score", m is not None and int(m.group(1)) >= 1400, m and m.group(0))
    e.run(20)
    e.screenshot(SHOTS / "gameover.png", scale=3)
    check("GAME OVER on red", has_colour(e, RED, (0, 26, 160, 42)))
    e.run(160)                                    # the original's 150-tick wait
    e.press("A")
    check("A goes back to the title", e.run_until("mm: title", 60))
    rec = e.record_input("save", REC)
    print(f"  bot game: recorded {rec['frames']} frames")

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")

# ------------------------------------------------------------ 2: all six caverns

SAMPLES = {(lv, lt) for lv in range(6) for lt in range(3, len(ROUTES[lv]), max(1, len(ROUTES[lv]) // 10))}


def back_buffer_sampler(e, out):
    def f(s):
        key = (s["level"], s["lt"])
        if s["mode"] == GM_PLAYING and key in SAMPLES and key not in out:
            vidbas = e.hw()["suzy"]["vidbas"]
            out[key] = hashlib.sha1(e.read_mem(vidbas, 8160, space="ram")).hexdigest()
    return f


drawn = {}
with Emulator() as e:
    e.load(PROG)
    to_title(e)
    gif, shot, walk = [], [False], set()

    def present(s):
        back_buffer_sampler(e, drawn)(s)
        if s["mode"] == GM_PLAYING:
            walk.add(s["frame"])
        if s["mode"] == GM_PLAYING and s["level"] == 5 and s["lt"] == 150 and not shot[0]:
            shot[0] = True
            e.screenshot(DOCS / "miner_meteor.png", scale=3)
        if s["mode"] == GM_PLAYING and s["level"] == 3 and s["lt"] < 400 and s["lt"] % 3 == 0:
            gif.append(e.screenshot(None, scale=2).convert("RGB"))

    log = route_bot(e, lambda log, s: s["mode"] == GM_WIN, on_present=present)
    e.run(30)
    log += e.log()
    if "--log" in sys.argv:
        print(log)
    if gif:
        small = [g.convert("P", palette=1, colors=32) for g in gif]
        small[0].save(DOCS / "miner_meteor.gif", save_all=True, append_images=small[1:],
                      duration=60, loop=0, optimize=True)
    prev = 0
    for lv in range(1, 7):
        m = re.search(rf"mm: cleared level={lv} score=(\d+) air=(\d+) ticks=(\d+)", log)
        nkeys = len(re.findall(rf"mm: key left=\d score=\d+", log.split(f"mm: level={lv} ")[1].split("mm: cleared")[0])) \
            if f"mm: level={lv} " in log else -1
        ok = m is not None and int(m.group(1)) == prev + nkeys * 100 + 1000 + int(m.group(2)) // 2 \
            and int(m.group(3)) == len(ROUTES[lv - 1])
        check(f"cavern {lv} cleared ({nkeys} keys, the route's {len(ROUTES[lv - 1])} ticks, its score)", ok,
              m and m.group(0))
        if m:
            prev = int(m.group(1))
    check("no life lost on the way", "mm: died" not in log)
    check("the miner walks through all 8 walk frames (the original showed 4)", walk == set(range(8)), sorted(walk))
    m = re.search(r"mm: won score=(\d+)", log)
    check("won: all six caverns", m is not None and int(m.group(1)) == prev, m and m.group(0))
    e.screenshot(SHOTS / "won.png", scale=3)
    check("ALL CAVERNS CLEARED on green", has_colour(e, GREEN, (0, 26, 160, 42)))
    fps = [int(v) for v in re.findall(r"mm: fps=(\d+)", log)]
    late = [int(v) for v in re.findall(r"mm: fps=\d+ late=(\d+)", log)]
    avg = sum(fps) / max(1, len(fps))
    print(f"  frame rate over the six caverns: {fps}  (late frames per 2 s: {late})")
    check("frame rate: 49+ fps average, never below 48", fps and avg >= 49 and min(fps) >= 48, f"avg {avg:.1f}")
    e.run(220)
    e.press("A")
    check("back to the title", e.run_until("mm: title", 60))

# ------------------------------------------------------------ 3: the dirty-cell drawing

full = {}
with Emulator() as e:
    e.load(PROG)
    to_title(e)
    route_bot(e, lambda log, s: s["mode"] == GM_WIN, on_present=back_buffer_sampler(e, full), force_full=True)
diff = [k for k in drawn if full.get(k) != drawn[k]]
check(f"dirty-cell redraw = full redraw at {len(drawn)} points", len(drawn) >= 50 and not diff, f"differ at {diff[:5]}")

# ------------------------------------------------------------ 4: give up

with Emulator() as e:
    e.load(PROG)
    to_title(e)
    e.press("B")
    check("B starts too", e.run_until("mm: start", 30))
    e.run(20)
    e.press("PAUSE")
    e.press("OPTION1")
    check("PAUSE, OPTION 1 gives up", e.run_until("mm: give up", 20) and e.run_until("mm: title", 20))

print(f"{'FAIL' if failures else 'PASS'}: miner_meteor")
sys.exit(1 if failures else 0)
