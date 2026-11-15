"""Lunar Rider (Lynx) - plays the game through the harness.

1. A recorded game. The title (the cyan logo, the bass on Mikey channel 3),
   A starts zone 1 (the HUD, the mountains, the ground); PAUSE stops the
   game (the scroll doesn't move) and PAUSE again goes on. Then a bot
   (tools/bot.py) drives: it plans its jumps over the craters, rocks and
   mines with the game's own rules (reading the terrain ring by symbol),
   at full speed, holding A (both guns). It must pass checkpoints (zone 4,
   +1000 each) and shoot rocks (+100). Then it stops jumping: the three
   lives go (deaths logged with their cause, the buggy respawns), GAME
   OVER stays on the screen although A is still held (the original left it
   at once: port fix), a new press of A goes back to the title. Checks the
   score lines add up, the frame rate from the game's own "lr: fps=" lines,
   and that the game is deterministic: it is recorded and replayed, every
   frame hash must match.
2. The enemies (rare at the start: one try a second, 2 % each): the test
   puts a small UFO, a large UFO and a meteor over the buggy (writing the
   game's enemy arrays) and the upward gun shoots them: 200, 500 (two hits)
   and 300 points; a UFO's bomb falls (it drops one by its timer); a bomb
   landing by the buggy kills it (cause=bomb).

    ./lynx test lunar_rider        (from the lynxdev checkout)
    .venv/bin/python games/lunar_rider/test.py [--shots] [--log]

--shots also writes images/lunar_rider.png, lunar_rider_title.png and
lunar_rider.gif (otherwise every picture goes to build/shots).
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
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "tools"))

from lynxdev.emu import Emulator  # noqa: E402
from bot import Bot  # noqa: E402

PROG = HERE / "build" / "lunar_rider.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
DOCS = HERE / "images" if "--shots" in sys.argv else SHOTS
REC = HERE / "build" / "bot-game.json"
failures = []

# palette indices (game.h)
COL_PURPLE, COL_BROWN, COL_RED, COL_CYAN, COL_BRYELLOW = 3, 4, 8, 10, 13
POINTS = {"rock": 100, "ufo_sm": 200, "ufo_lg": 500, "meteor": 300, "checkpoint": 1000}


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def u16(e, sym):
    return struct.unpack("<H", e.read_mem(sym, 2))[0]


def colours(img, x0, x1, y0, y1):
    return {img.getpixel((x, y)) for x in range(x0, x1) for y in range(y0, y1)}


def drive(e, bot, frames, until=None, gif=None, shot=None):
    """the bot drives for up to `frames` frames (until a log line); returns the log"""
    log = ""
    for t in range(frames):
        e.release()
        e.hold(bot.keys(e))
        e.run(1)
        log += e.log()
        if gif is not None and t % 4 == 0:
            gif.append(e.screenshot(None, scale=2).convert("RGB"))
        if shot and shot(t, log):
            shot = None
        if until and until in log:
            break
    return log


def score_lines(log):
    return [(int(d), w, int(s)) for d, w, s in re.findall(r"lr: score\+(\d+) why=(\w+) score=(\d+)", log)]


def scores_add_up(lines):
    prev = 0
    for d, why, s in lines:
        if s - prev != d:
            return False
        # one frame can score a rock and a checkpoint (the line names the last)
        if d not in (POINTS[why], POINTS[why] + 100, POINTS[why] + 1000):
            return False
        prev = s
    return True


# ------------------------------------------------------------ 1. a recorded game

with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("lr: title", 600))
    e.run(50)
    pal = e.palette()
    title = e.screenshot(None)
    check("title: the cyan LUNAR RIDER logo", tuple(pal[COL_CYAN]) in colours(title, 10, 150, 6, 20))
    e.screenshot(DOCS / ("lunar_rider_title.png" if DOCS != SHOTS else "title.png"), scale=3)
    m = e.hw()["mikey"]
    check("the bass plays on Mikey channel 3", m.get("aud3ctl", 0) & 0x18 == 0x18 and m.get("aud3vol", 0) > 0, m)

    e.press("A", hold_frames=4)
    check("A starts the game", e.run_until("lr: state=playing lives=3", 30))
    e.run(20)
    scr = e.screenshot(None)
    check("HUD: SCORE in bright yellow", tuple(pal[COL_BRYELLOW]) in colours(scr, 2, 22, 1, 6))
    check("the ground line in brown", sum(scr.getpixel((x, 80)) == tuple(pal[COL_BROWN]) for x in range(160)) > 60)
    check("the near mountains in purple", tuple(pal[COL_PURPLE]) in colours(scr, 0, 160, 32, 64))

    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("lr: paused=1", 10))
    p0 = u16(e, "scroll_px")
    e.run(50)
    check("paused: the ground doesn't scroll", u16(e, "scroll_px") == p0)
    e.screenshot(SHOTS / "paused.png", scale=3)
    e.press("PAUSE")
    check("PAUSE again goes on", e.run_until("lr: paused=0", 10))

    # the bot rides to zone 4
    bot = Bot()
    gif = []
    shot = {"done": False}

    def take(t, log):
        s = e.peek("state")
        if t > 150 and s == 1 and e.peek("buggy_alive") and struct.unpack("<h", e.read_mem("buggy_y", 2))[0] < 165:
            e.screenshot(DOCS / "lunar_rider.png", scale=3)
            return True
        return False

    log = drive(e, bot, 4000, until="lr: checkpoint zone=4", gif=gif if DOCS != SHOTS else None, shot=take)
    if gif:
        small = [g.convert("P", palette=1, colors=32) for g in gif[40:140]]
        small[0].save(DOCS / "lunar_rider.gif", save_all=True, append_images=small[1:],
                      duration=80, loop=0, optimize=True)
    check("the bot jumps (large craters; at full speed the buggy coasts over small ones)",
          log.count("lr: jump") >= 1, log.count("lr: jump"))
    check("checkpoints: zone 2, 3, 4", all(f"lr: checkpoint zone={z} " in log for z in (2, 3, 4)), log[-300:])
    check("shooting rocks scores 100", any(w == "rock" for _, w, _ in score_lines(log)))
    e.run(15)
    e.screenshot(SHOTS / "checkpoint.png", scale=3)
    small = e.screenshot(None)
    check("the checkpoint banner (ZONE 04) in green", tuple(pal[11]) in colours(small, 30, 130, 12, 26))

    # stop jumping: the lives go
    lazy = Bot(jump=False)
    log += drive(e, lazy, 6000, until="lr: gameover")
    if "--log" in sys.argv:
        print(log)
    deaths = re.findall(r"lr: death cause=(\w+) lives=(\d) zone=(\d+)", log)
    check("three deaths, with their causes", len(deaths) == 3 and
          all(c in ("crater", "rock", "mine", "bomb", "enemy") for c, _, _ in deaths), deaths)
    check("lives 3, 2, 1 at the deaths", [int(l) for _, l, _ in deaths] == [3, 2, 1], deaths)
    check("the buggy respawns twice", re.findall(r"lr: respawn lives=(\d)", log) == ["2", "1"])
    lines = score_lines(log)
    check("the score lines add up (100 a rock, 1000 a checkpoint)", scores_add_up(lines), lines[-6:])
    go = re.search(r"lr: gameover score=(\d+) zone=(\d+)", log)
    check("game over", go is not None, log[-300:])
    final = int(go.group(1)) if go else -1
    check("game over score = the last score line", lines and final == lines[-1][2], f"{final} {lines[-1:]}")
    fps = [(int(f), int(late)) for f, late in re.findall(r"lr: fps=(\d+) late=(\d+)", log)]
    avg = sum(f for f, _ in fps) / max(1, len(fps))
    print(f"  final score {final}, zone {go.group(2) if go else '?'}, {log.count('lr: jump')} jumps")
    print(f"  frame rate (fps, late frames per 100): {fps}")
    check("frame rate: 49+ fps average, never below 46", fps and avg >= 49 and min(f for f, _ in fps) >= 46,
          f"avg {avg:.1f}")

    e.run(30)                            # A still held (the bot holds it)
    check("GAME OVER stays while A is held (port fix)", e.peek("state") == 3)
    e.screenshot(SHOTS / "gameover.png", scale=3)
    small = e.screenshot(None)
    check("GAME OVER in red", tuple(pal[COL_RED]) in colours(small, 20, 140, 34, 48))
    e.release()
    e.run(2)
    e.press("A", hold_frames=4)
    check("a new press of A: back to the title", e.run_until("lr: title", 30))
    e.run(30)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")


# ------------------------------------------------------------ 2. the enemies

def put_enemy(e, i, typ, x, y, dx=0, dy=0, hp=1, bomb=0):
    e.write_mem(e.addr("en_x") + 2 * i, struct.pack("<h", x))
    e.write_mem(e.addr("en_y") + 2 * i, struct.pack("<h", y))
    e.write_mem(e.addr("en_dx") + i, struct.pack("<b", dx))
    e.write_mem(e.addr("en_dy") + i, struct.pack("<b", dy))
    e.write_mem(e.addr("en_type") + i, bytes([typ]))
    e.write_mem(e.addr("en_hp") + i, bytes([hp]))
    e.write_mem(e.addr("en_bomb") + i, bytes([bomb]))
    e.write_mem(e.addr("en_flash") + i, bytes([0]))
    e.write_mem(e.addr("en_active") + i, bytes([1]))


with Emulator() as e:
    e.load(PROG)
    e.run_until("lr: title", 600)
    e.run(40)
    e.press("A", hold_frames=4)
    e.run_until("lr: state=playing", 30)
    bot = Bot()
    drive(e, bot, 60)
    pal = e.palette()
    # the upward gun is at x 92: a small UFO above it (still), then a large
    # one (two hits), then a meteor falling onto the buggy
    put_enemy(e, 0, 1, 86, 60, bomb=200)
    log = drive(e, bot, 60, until="why=ufo_sm")
    check("a small UFO shot: 200", "lr: score+200 why=ufo_sm" in log, log[-200:])
    put_enemy(e, 0, 2, 82, 40, hp=2, bomb=200)
    e.run(3)
    e.screenshot(SHOTS / "ufo.png", scale=3)
    small = e.screenshot(None)
    check("the large UFO drawn (cyan, red lights)", tuple(pal[COL_CYAN]) in colours(small, 40, 52, 15, 21))
    log = drive(e, bot, 80, until="why=ufo_lg")
    check("a large UFO takes two hits: 500", "lr: score+500 why=ufo_lg" in log, log[-200:])
    put_enemy(e, 0, 3, 88, 20, dy=2)
    log = drive(e, bot, 80, until="why=meteor")
    check("a meteor shot: 300", "lr: score+300 why=meteor" in log, log[-200:])
    check("the enemy scores add up", scores_add_up(score_lines(e.log(0))))
    # a UFO drops its bomb when its timer runs out; one right by the buggy
    # kills it when it lands
    put_enemy(e, 1, 1, 300, 40, dx=0, bomb=2)
    e.run(4)
    bombs = e.read_mem("bm_active", 8)
    check("a UFO drops a bomb", any(bombs))
    e.write_mem("bm_active", bytes(8))
    e.write_mem(e.addr("en_active") + 1, bytes([0]))
    e.write_mem("bm_x", struct.pack("<h", 96))
    e.write_mem("bm_y", struct.pack("<h", 150))
    e.write_mem("bm_active", bytes([1]))
    log = drive(e, bot, 60, until="lr: death")
    check("a bomb landing by the buggy kills it", "lr: death cause=bomb" in log, log[-200:])
    if "--log" in sys.argv:
        print(e.log(0))

print(f"{'FAIL' if failures else 'PASS'}: lunar_rider")
sys.exit(1 if failures else 0)
