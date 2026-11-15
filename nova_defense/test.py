"""Nova Defense (Lynx) - plays a game through the harness.

The title, A starts wave 1 (48 aliens, three shields, the status line).
PAUSE stops the game (nothing moves) and PAUSE again goes on. Then a bot
reads the game's state by symbol (alive_mask, grid_x/y, player_x, bul_*)
and plays: it moves under the alien a shot will meet (leading the march),
fires when its shot is gone, and dodges the aliens' bullets. It clears wave
1; in wave 2 it stops dodging, so lives are lost and the game ends.
Checks the log lines (nd: ...): scores 10 / 20 / 30 an alien by row, the
wave clear and wave 2, a life lost, game over with the last score and the
high score, the frame rate from the game's own "nd: fps=" lines, the
screens, and that the game is deterministic: it is recorded and replayed,
every frame hash must match.

    .venv/bin/python games/nova_defense/test.py [--shots] [--log]

--shots also writes images/nova_defense.png, nova_defense_title.png and
nova_defense.gif (otherwise every picture goes to build/shots).
"""
import os
import re
import struct
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
PROG = HERE / "build" / "nova_defense.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
DOCS = HERE / "images" if "--shots" in sys.argv else SHOTS
REC = HERE / "build" / "bot-game.json"
failures = []

# game.h
CELL_W, CELL_H, SHIFT, ALIEN_W, ALIEN_H = 26, 14, 12, 14, 10
PLAYER_Y, PLAYER_W = 170, 15
SHOT_SPEED = 8
POINTS = {0: 30, 1: 30, 2: 20, 3: 20, 4: 10, 5: 10}


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def arr(e, sym, n, fmt="B"):
    size = struct.calcsize("<" + fmt)
    return struct.unpack(f"<{n}{fmt}", e.read_mem(sym, n * size))


def state(e):
    bx, by = arr(e, "bul_x", 4, "h"), arr(e, "bul_y", 4, "h")
    act = arr(e, "bul_active", 4)
    return {"mask": arr(e, "alive_mask", 6), "gx": arr(e, "grid_x", 1, "h")[0],
            "gy": arr(e, "grid_y", 1, "h")[0], "dir": arr(e, "swarm_dir", 1, "b")[0],
            "alive": e.peek("alive_count"), "px": arr(e, "player_x", 1, "h")[0],
            "shot": act[0], "bullets": [(bx[i], by[i]) for i in range(1, 4) if act[i]],
            "state": e.peek("state"), "delay": e.peek("move_delay"),
            "shields": e.read_mem("shield_px", 3 * 7 * 16),
            "ufo": e.peek("ufo_active"), "ufo_x": arr(e, "ufo_x", 1, "h")[0], "ufo_dir": arr(e, "ufo_dir", 1, "b")[0]}


def blocked(s, x):
    """a shield is over world x (its column has crystal left)"""
    for i, sx in enumerate((64, 144, 224)):
        if sx <= x < sx + 32:
            col = (x - sx) >> 1
            return any(s["shields"][i * 112 + y * 16 + col] for y in range(7))
    return False


def speed(alive, delay):
    """the swarm's sideways speed, world pixels a frame (game.c)"""
    d = 1 if alive <= 20 else 2 if alive <= 35 else delay
    step = 10 if alive <= 1 else 8 if alive <= 5 else 5 if alive <= 10 else 4 if alive <= 20 else 3
    return step / d


def aliens(s):
    """(row, x, y) of each alive alien, x where it will be when a shot fired
    now gets to its height (leading the march; not the turns)"""
    v = speed(s["alive"], s["delay"]) * s["dir"]
    out = []
    for r in range(6):
        for c in range(8):
            if s["mask"][r] & (1 << c):
                ay = s["gy"] + r * CELL_H
                t = (PLAYER_Y - 2 - (ay + ALIEN_H // 2)) / SHOT_SPEED
                out.append((r, s["gx"] + c * CELL_W + (SHIFT if r & 1 else 0) + v * t, ay))
    return out


def keys(s, dodge, fired):
    """the lowest row first (it is the one that invades), the nearest of it;
    fire whenever a shot from here would meet an alien"""
    out = []
    me = s["px"] + PLAYER_W / 2
    al = aliens(s)
    danger = [bx for bx, by in s["bullets"] if by > PLAYER_Y - 50 and abs(bx - me) < 14]
    if dodge and danger:
        out.append("LEFT" if (danger[0] > me and s["px"] > 30) or s["px"] > 280 else "RIGHT")
    elif al:
        low = max(r for r, _, _ in al)
        cands = [x + ALIEN_W / 2 for r, x, _ in al if r == low and 12 <= x + ALIEN_W / 2 <= 308]
        if cands:
            tx = min(cands, key=lambda x: abs(x - me))
            if tx < me - 2:
                out.append("LEFT")
            elif tx > me + 2:
                out.append("RIGHT")
    shot_x = s["px"] + PLAYER_W // 2
    comet = False
    if s["ufo"]:                          # where the comet will be when a shot gets up there
        cx = s["ufo_x"] + s["ufo_dir"] * (PLAYER_Y - 2 - 14) / SHOT_SPEED
        comet = cx + 2 <= shot_x < cx + 14
    if not s["shot"] and not fired and not blocked(s, shot_x) and (
            comet or any(x + 1 <= shot_x < x + ALIEN_W - 1 for _, x, _ in al)):
        out.append("A")
    return "+".join(out), "A" in out


def bot_game(e, max_frames=20000):
    log, t, gif, shot, fired, shots = "", 0, [], False, False, 0
    while "nd: gameover" not in log and t < max_frames:
        s = state(e)
        k, fired = keys(s, dodge="nd: wave=2" not in log, fired=fired)
        shots += fired
        e.release()
        if k:
            e.hold(k)
        e.run(1)
        t += 1
        if 200 <= t < 600 and t % 4 == 0:
            gif.append(e.screenshot(None, scale=2).convert("RGB"))
        if not shot and t >= 400 and s["alive"] <= 40:
            e.screenshot(DOCS / "nova_defense.png", scale=3)
            shot = True
        log += e.log()
    print(f"  the bot fired {shots} shots")
    e.release()
    e.run(5)
    if gif:
        small = [g.convert("P", palette=1, colors=32) for g in gif]
        small[0].save(DOCS / "nova_defense.gif", save_all=True, append_images=small[1:],
                      duration=80, loop=0, optimize=True)
    return log + e.log(), t


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("nd: title", 600))
    e.run(60)
    e.screenshot(DOCS / ("nova_defense_title.png" if DOCS != SHOTS else "title.png"), scale=3)
    pal = e.palette()
    small = e.screenshot(None)
    cols = {small.getpixel((x, y)) for x in range(160) for y in range(0, 16)}
    check("title: the magenta NOVA logo", tuple(pal[7]) in cols)
    m = e.hw()["mikey"]
    check("the arpeggio plays on Mikey channel 3", m.get("aud3ctl", 0) & 0x18 == 0x18, m)
    e.press("A")
    check("A starts wave 1", e.run_until("nd: wave=1 lives=3", 30))
    e.run(20)
    s = state(e)
    check("48 aliens in wave 1", s["alive"] == 48 and list(s["mask"]) == [255] * 6, s)
    scr = e.screenshot(None)
    check("status line: the cyan ground line", all(scr.getpixel((x, 94)) == tuple(pal[6]) for x in range(0, 160, 7)))
    check("shields drawn", sum(scr.getpixel((x, 77)) == tuple(pal[6]) for x in range(160)) >= 20)

    m = e.hw()["mikey"]
    check("the march plays on Mikey channel 0", m.get("aud0ctl", 0) & 0x18 == 0x18 and m.get("aud0vol", 0) > 0, m)
    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("nd: paused=1", 10))
    g0 = state(e)["gx"]
    e.run(50)
    check("paused: the swarm doesn't move", state(e)["gx"] == g0)
    e.screenshot(SHOTS / "paused.png", scale=3)
    e.press("PAUSE")
    check("PAUSE again goes on", e.run_until("nd: paused=0", 10))

    log, t = bot_game(e)
    if "--log" in sys.argv:
        print(log)
    hits = [(int(r), int(sc), int(a)) for r, sc, a in re.findall(r"nd: hit row=(\d+) score=(\d+) alive=(\d+)", log)]
    comets = [int(v) for v in re.findall(r"nd: comet score=(\d+)", log)]
    fps = [tuple(map(int, f)) for f in re.findall(r"nd: fps=(\d+) late=(\d+) busy=(\d+)/\d+ alive=(\d+)", log)]
    # every alien hit adds its row's points (or a comet's 50-300 came in between)
    ok, prev = True, 0
    events = sorted([(m.start(), "hit", m) for m in re.finditer(r"nd: hit row=(\d+) score=(\d+)", log)] +
                    [(m.start(), "comet", m) for m in re.finditer(r"nd: comet score=(\d+)", log)] +
                    [(m.start(), "wave", m) for m in re.finditer(r"nd: wave=1 ", log)], key=lambda x: x[0])
    for _, kind, mm in events:
        if kind == "wave":
            prev = 0
            continue
        sc = int(mm.group(2) if kind == "hit" else mm.group(1))
        if kind == "hit" and sc - prev != POINTS[int(mm.group(1))]:
            ok = False
        if kind == "comet" and sc - prev not in (50, 100, 150, 300):
            ok = False
        prev = sc
    check("shooting aliens scores", len(hits) >= 48, len(hits))
    check("30 / 20 / 10 points by row (comets 50-300)", ok and {r for r, *_ in hits} >= {0, 2, 4})
    check("the comet shot down (50-300 points)", len(comets) >= 1)
    check("wave 1 cleared", "nd: waveclear wave=1" in log)
    check("wave 2", "nd: wave=2 lives=" in log)
    lives = [int(v) for v in re.findall(r"nd: lives=(\d+)", log)]
    check("a life is lost", "nd: hit player" in log and lives and min(lives) < 3, lives)
    mo = re.search(r"nd: gameover score=(\d+) wave=(\d+) hi=(\d+)", log)
    check("game over", mo is not None, log[-300:])
    final = int(mo.group(1)) if mo else -1
    last = max([sc for _, sc, _ in hits] + comets) if hits else 0
    check("game over score = the last score line", final == last, f"{final} {last}")
    check("high score = the score", mo and int(mo.group(3)) == final)
    print(f"  played {t} frames, final score {final}, wave {mo.group(2) if mo else '?'}, comets {len(comets)}")
    print(f"  frame rate (fps, late frames, busiest frame in display lines /105, aliens): {fps}")
    avg = sum(f for f, *_ in fps) / max(1, len(fps))
    check("frame rate: 50 fps average, never below 40", fps and avg >= 48 and min(f for f, *_ in fps) >= 40,
          f"avg {avg:.1f}")
    e.run(20)
    e.screenshot(SHOTS / "gameover.png", scale=3)
    small = e.screenshot(None)
    check("GAME OVER in red", any(small.getpixel((x, y)) == tuple(pal[4]) for x in range(20, 140) for y in range(18, 32)))
    e.press("A")
    check("A goes back to the title", e.run_until("nd: title", 200))
    e.run(60)
    small = e.screenshot(SHOTS / "title-hi.png", scale=3)
    small = e.screenshot(None)
    check("title shows the high score", any(small.getpixel((x, y)) == tuple(pal[8]) for x in range(40, 120) for y in range(79, 84)))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")

print(f"{'FAIL' if failures else 'PASS'}: nova_defense")
sys.exit(1 if failures else 0)
