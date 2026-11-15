"""Boing Twist (Lynx) - plays the puzzle through the harness.

The title (the Boing ball bouncing, turned by colour cycling, OPTION 2
changes the room), the select screen, then an Easy game: the scramble (its
hash checked against the original's bt_core.c compiled on the host with the
same seed), a twist and its undo, a peek, a pause; then a bot reads the
scramble from the puzzle in memory (P.hist) and plays it backwards through
the controls (the band by UP/DOWN, the half by B + LEFT/RIGHT, twists by
LEFT/RIGHT, flips by A) until the ball is solved: the solved screen, the
best moves and time kept. Then a Medium game (level progress: a 6 x 12
grid), given up from the pause menu: it solves itself, move by move. B goes
back to the title. Checks the frame rate from the game's "bt: anim" lines,
the screens' pixels, and that the game is deterministic: it is recorded and
replayed, every frame hash must match.

    ./lynx test boing_twist         (from the lynxdev checkout)
    .venv/bin/python games/boing_twist/test.py [--shots] [--log]

--shots also writes images/boing_twist.png (play), boing_twist_title.png,
boing_twist_select.png and boing_twist.gif (otherwise every picture goes to
build/shots).
"""
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path


def _lynxdev_root() -> Path:
    """The lynxdev checkout this game is built in (lynx_games is its games/
    submodule), or $LYNX_ROOT."""
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
PROG = HERE / "build" / "boing_twist.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
DOCS = HERE / "images" if "--shots" in sys.argv else SHOTS
REC = HERE / "build" / "bot-game.json"
PUZZLE = 0xF100                       # ball.h HI_PUZZLE: the BtPuzzle while playing
BT_MAX_T, BT_HIST = 128, 760          # bt_core.h (BT_HIST: the Lynx's)
OFF_SLOT, OFF_HIST = 4, 4 + 256 + BT_MAX_T * 8
OFF_NHIST = OFF_HIST + 2 * BT_HIST
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def puzzle(e):
    b = e.read_mem(PUZZLE, OFF_NHIST + 6, space="ram")
    bands, segs = struct.unpack_from("<hh", b, 0)
    nhist, nscramble, moves = struct.unpack_from("<hhh", b, OFF_NHIST)
    hist = list(struct.unpack_from(f"<{nhist}h", b, OFF_HIST))
    slots = [(b[OFF_SLOT + 2 * i], b[OFF_SLOT + 2 * i + 1]) for i in range(bands * segs)]
    return {"bands": bands, "segs": segs, "hist": hist, "nscramble": nscramble, "moves": moves, "slots": slots}


def host_hash(bands, segs, seed, n):
    """the original's bt_core.c on the host: the hash of the same scramble"""
    src = HERE / "src" / "bt_core.c"
    exe = HERE / "build" / "host_bt"
    main = HERE / "build" / "host_bt_main.c"
    main.write_text('#include <stdio.h>\n#include <stdlib.h>\n#include "bt_core.h"\n'
                    "int main(int c, char **v) { static BtPuzzle p; bt_init(&p, atoi(v[1]), atoi(v[2]));\n"
                    "  bt_scramble(&p, (uint32_t)strtoul(v[3], 0, 16), atoi(v[4]));\n"
                    '  printf("%08X\\n", (unsigned)bt_hash(&p)); return 0; }\n')
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        return None
    subprocess.run([cc, "-O1", "-I", str(HERE / "src"), "-o", str(exe), str(main), str(src)], check=True)
    return subprocess.run([str(exe), str(bands), str(segs), seed, str(n)], capture_output=True,
                          text=True, check=True).stdout.strip()


def wait_idle(e, max_frames=600):
    for _ in range(max_frames // 4):
        e.run(4)
        if e.peek("_bt_idle"):
            return True
    return False


def tap(e, key, hold=3, after=3):
    e.hold(key)
    e.run(hold)
    e.release(key)
    e.run(after)


def until(e, text, frames):
    """run_until, then a few frames more: it stops as soon as the text is
    written, the rest of the line can still be coming"""
    ok = e.run_until(text, frames)
    e.run(3)
    return ok


def do_move(e, m, gif=False):
    """a move through the controls: the band by UP/DOWN then LEFT (west) or
    RIGHT (east); the half by B + LEFT/RIGHT then A"""
    if m & 0x100:
        half, segs = m & 0xFF, puzzle(e)["segs"]
        cur = e.peek("_sel_half")
        d = (half - cur) % segs
        key = "RIGHT" if d <= segs // 2 else "LEFT"
        steps = d if key == "RIGHT" else segs - d
        e.hold("B")
        e.run(2)
        for _ in range(steps):
            tap(e, key)
        e.release()
        e.run(2)
        wait_idle(e)
        tap(e, "A")
    else:
        band, west = m >> 1 & 0x7F, m & 1
        while e.peek("_sel_band") != band:
            tap(e, "UP" if e.peek("_sel_band") > band else "DOWN")
        tap(e, "LEFT" if west else "RIGHT")
    for i in range(40):
        if gif:
            grab(e, 3)
        else:
            e.run(3)
        if e.peek("_bt_idle"):
            break


GIF = []


def grab(e, frames, step=3):
    """frames for the GIF: one every `step` (60 ms at 50 Hz)"""
    for _ in range(frames // step):
        e.run(step)
        if "--shots" in sys.argv and len(GIF) < 160:
            GIF.append(e.screenshot(None, scale=2).convert("RGB"))


def inverse(m):
    return m if m & 0x100 else m ^ 1


def colours(img, box):
    x0, y0, x1, y1 = box
    return {img.getpixel((x, y)) for x in range(x0, x1) for y in range(y0, y1)}


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("bt: screen=TITLE", 600))
    grab(e, 90)
    for _ in range(80):                  # the picture with the ball low, under the logo
        if struct.unpack("<h", e.read_mem("_ball_cy", 2))[0] >= 58:
            break
        e.run(1)
    title = e.screenshot(DOCS / ("boing_twist_title.png" if DOCS != SHOTS else "title.png"), scale=3)
    small = e.screenshot(None)
    cols = colours(small, (0, 8, 160, 94))
    pal = e.palette()
    check("title: the Boing ball's red and white", any(r > 200 and g < 80 and b < 80 for r, g, b in cols)
          and any(r > 220 and g > 220 and b > 220 for r, g, b in cols))
    h0 = e.screen_hash()
    e.run(10)
    check("title: the ball moves (bounce and colour cycling)", e.screen_hash() != h0)
    tap(e, "OPTION2", after=10)
    check("title: OPTION 2 changes the room", e.run_until("bt: room n=1", 30))
    for _ in range(9):
        tap(e, "OPTION2", after=10)
    check("ten rooms, back to the first", e.run_until("bt: room n=0", 60))

    tap(e, "A")
    check("A: the select screen", e.run_until("bt: screen=SELECT", 60))
    e.run(40)
    e.screenshot(DOCS / ("boing_twist_select.png" if DOCS != SHOTS else "select.png"), scale=3)
    tap(e, "DOWN")
    tap(e, "RIGHT")
    check("select: the next picture (Earth)", e.run_until("bt: preview pic=1 level=0", 60))
    tap(e, "LEFT")
    check("select: back to the Boing ball", e.run_until("bt: preview pic=0 level=0", 60))
    tap(e, "RIGHT", after=10)
    tap(e, "RIGHT", after=10)
    tap(e, "RIGHT", after=10)
    check("Easy offers Earth, Toytown, Fruit Bowl", e.run_until("bt: preview pic=6 level=0", 60))
    e.run(30)

    # ---- an Easy game (the Fruit Bowl: no two tiles alike, so the bot plays the whole scramble back)
    tap(e, "A")
    check("A plays: Easy is 4 x 8, 8 moves", until(e, "bt: play pic=6 level=0 bands=4 segs=8 scramble=8", 60))
    grab(e, 45)
    seed = re.search(r"seed=([0-9A-F]{8})", e.log_text(0))
    check("the scramble animates, then is done", until(e, "bt: scrambled n=8", 900))
    log = e.log_text(0)
    m = re.search(r"bt: scrambled n=8 hash=([0-9A-F]{8})", log)
    hh = host_hash(4, 8, seed.group(1), 8) if seed else None
    check("the scramble = the original's bt_core.c on the host, same seed", m and hh and m.group(1) == hh,
          f"lynx {m and m.group(1)} host {hh}")
    wait_idle(e)
    p = puzzle(e)
    check("the puzzle in memory: 4 x 8, 8 scramble moves", p["bands"] == 4 and p["segs"] == 8 and p["nscramble"] == 8,
          p)
    e.run(20)
    play = e.screenshot(SHOTS / "play.png", scale=3)
    small = e.screenshot(None)
    pal = e.palette()
    check("play: the band markers drawn", any(small.getpixel((x, y)) == tuple(pal[5]) for x in (42, 43) for y in range(19, 83)),
          sorted(colours(small, (42, 19, 44, 83))))

    # a twist and its undo
    tap(e, "RIGHT")
    band = e.peek("_sel_band")
    check("RIGHT twists the band east", e.run_until(f"bt: move m={band * 2} moves=1", 60))
    wait_idle(e)
    tap(e, "OPTION1")
    check("OPTION 1 undoes it", e.run_until(f"bt: move m={band * 2 + 1} moves=0", 60))
    wait_idle(e)
    check("undo: the puzzle as scrambled again", puzzle(e)["hist"] == p["hist"])

    # peek and pause
    e.hold("OPTION2")
    check("OPTION 2 held peeks", e.run_until("bt: peek n=1", 30))
    e.run(10)
    e.screenshot(SHOTS / "peek.png", scale=3)
    e.release()
    e.run(10)
    tap(e, "PAUSE")
    check("PAUSE pauses", e.run_until("bt: paused on=1", 20))
    e.run(10)
    e.screenshot(SHOTS / "paused.png", scale=3)
    secs = e.peek16("_secs")
    tap(e, "RIGHT")
    e.run(60)
    check("paused: no move, the clock stopped", "bt: move" not in e.log() and e.peek16("_secs") == secs)
    tap(e, "PAUSE")
    check("PAUSE again goes on", e.run_until("bt: paused on=0", 20))
    wait_idle(e)

    # the bot: the scramble backwards, through the controls
    p = puzzle(e)
    moves = [inverse(mv) for mv in reversed(p["hist"])]
    for i, mv in enumerate(moves):
        do_move(e, mv, i < 3)
        if i == 2:
            e.run(10)
            e.screenshot(DOCS / "boing_twist.png", scale=3)

    log = e.log_text(0)
    sm = re.search(r"bt: solved moves=(\d+) secs=(\d+) peeks=(\d+) gave_up=(\d) best=(\d)", log)
    check("the bot solves it: the solved screen", sm is not None and "bt: screen=SOLVED" in log, log[-300:])
    check("solved: moves = the scramble's length, one peek, a new best",
          sm and int(sm.group(1)) == len(moves) and sm.group(3) == "1" and sm.group(4) == "0" and sm.group(5) == "1",
          sm and sm.group(0))
    grab(e, 60)
    e.screenshot(SHOTS / "solved.png", scale=3)
    grab(e, 75)

    # ---- a Medium game (level progress), given up
    tap(e, "A")
    check("A after solving: the select screen", e.run_until("bt: screen=SELECT", 120))
    tap(e, "UP")                             # the level row
    tap(e, "RIGHT")                          # Medium
    check("select: Medium", e.run_until("bt: preview pic=6 level=1", 60))
    tap(e, "A")
    check("Medium is 6 x 12, 20 moves", e.run_until("bt: play pic=6 level=1 bands=6 segs=12 scramble=20", 60))
    check("its scramble", e.run_until("bt: scrambled n=20", 1500))
    wait_idle(e)
    e.run(20)
    e.screenshot(SHOTS / "medium.png", scale=3)
    tap(e, "PAUSE")
    e.run(5)
    tap(e, "B")
    check("paused, B gives up", e.run_until("bt: give up", 30))
    check("it solves itself, move by move", until(e, "bt: solved", 4000))
    log = e.log_text(0)
    check("given up: no best", re.search(r"bt: solved moves=\d+ secs=\d+ peeks=0 gave_up=1 best=0", log) is not None)
    e.run(60)
    e.screenshot(SHOTS / "given_up.png", scale=3)
    tap(e, "B")
    check("B: back to the title", e.run_until("bt: screen=TITLE", 120))
    e.run(30)

    # the frame rate while the bot's moves animate: pictures drawn / ticks (50 a second)
    log = e.log_text(0)
    easy = log[log.index("bt: scrambled n=8"):log.index("bt: screen=SOLVED")]
    an = [(int(f), int(t)) for f, t in re.findall(r"bt: anim frames=(\d+) ticks=(\d+)", easy)]
    fr, tk = (an[-1][0] - an[1][0], an[-1][1] - an[1][1]) if len(an) > 2 else (0, 1)
    fps = 50.0 * fr / max(1, tk)
    print(f"  moves animated at {fps:.1f} pictures a second ({fr} pictures in {tk} ticks)")
    check("the ball redrawn at least 15 times a second while moves animate", fps >= 15)
    if "--log" in sys.argv:
        print(log)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

    if GIF:
        small = [g.convert("P", palette=1, colors=64) for g in GIF]
        small[0].save(DOCS / ("boing_twist.gif" if DOCS != SHOTS else "play.gif"), save_all=True,
                      append_images=small[1:], duration=60, loop=0, optimize=True)

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")

print(f"{'FAIL' if failures else 'PASS'}: boing_twist")
sys.exit(1 if failures else 0)
