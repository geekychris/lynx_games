"""Jump Quest (Lynx) - plays the game through the harness.

1. The title: LEFT/RIGHT pick RJ or Dale, UP/DOWN one or two players, A
   starts. PAUSE stops the game (nothing moves) and goes on.
2. A bot game (recorded): RJ runs right and jumps now and then; it scores
   (? blocks, items, stomps), gets hurt, falls into pits and loses its
   lives until the game is over (GAME OVER on the screen), and A goes back
   to the title. The recording is replayed: every frame hash must match.
3. The levels: jq_autoplay (a test hook in harness builds) makes the hero
   follow demo.h's routes, which clear all three levels: LEVEL COMPLETE,
   the 500 bonus, level 2 and 3 (the view scrolls the whole way),
   CONGRATULATIONS, the score of the Spectrum and Atari ports (4200), a new
   high score on the title. The frame rate over the three levels comes from
   the game's own "jq: fps=" lines.
4. Two players with Dale: player 2 takes over when player 1's lives are gone.

    .venv/bin/python games/jump_quest/test.py [--shots] [--log]

--shots also writes images/jump_quest.png, jump_quest_title.png,
jump_quest_level2.png, jump_quest_level3.png and jump_quest.gif
(otherwise every picture goes to build/shots).
"""
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
PROG = HERE / "build" / "jump_quest.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
DOCS = HERE / "images" if "--shots" in sys.argv else SHOTS
DOCS.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
failures = []

# the palette (the original's indices)
SKY, RED, YELLOW, WHITE, GREEN = 1, 14, 13, 11, 4


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def has_colour(e, idx, box):
    pal = e.palette()
    im = e.screenshot(None)
    x0, y0, x1, y1 = box
    return any(im.getpixel((x, y)) == tuple(pal[idx]) for x in range(x0, x1) for y in range(y0, y1))


def to_title(e):
    check("boots to the title", e.run_until("jq: title", 600))
    e.run(20)


# ------------------------------------------------------------ 1, 2: title, bot game

with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    to_title(e)
    e.run(40)
    e.screenshot(DOCS / ("jump_quest_title.png" if DOCS != SHOTS else "title.png"), scale=3)
    check("title: JUMP QUEST in yellow", has_colour(e, YELLOW, (30, 12, 130, 22)))
    e.press("RIGHT")
    check("RIGHT picks Dale", e.run_until("jq: select char=1 players=1", 20))
    e.press("DOWN")
    check("DOWN: two players", e.run_until("jq: select char=1 players=2", 20))
    e.press("UP")
    e.press("LEFT")
    check("back to RJ, one player", e.run_until("jq: select char=0 players=1", 20))
    e.press("A")
    check("A starts the game", e.run_until("jq: start char=0 players=1", 30))
    e.run(30)
    check("the level is drawn (grass)", has_colour(e, GREEN, (0, 70, 160, 90)))
    check("HUD: hearts in red", has_colour(e, RED, (18, 96, 50, 102)))

    # pause: nothing moves while paused
    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("jq: paused=1", 10))
    x0 = e.read_mem(e.addr("game") + 8, 2)
    e.hold("RIGHT")
    e.run(50)
    e.release()
    check("paused: the hero doesn't move", e.read_mem(e.addr("game") + 8, 2) == x0)
    e.screenshot(SHOTS / "paused.png", scale=3)
    e.press("PAUSE")
    check("PAUSE again goes on", e.run_until("jq: paused=0", 10))

    # the bot: run right, jump every so often; jumps over the first pit
    # sometimes, falls in others; lives run out
    log, t, gif, shot = "", 0, [], False
    while "jq: gameover" not in log and t < 9000:
        phase = t % 64
        keys = "RIGHT+A" if phase < 6 or (t // 64) % 3 == 0 and phase < 20 else "RIGHT"
        e.release()
        e.hold(keys)
        e.run(4)
        t += 4
        if 40 <= t < 360:
            gif.append(e.screenshot(None, scale=2).convert("RGB"))
        log += e.log()
        if not shot and "jq: collect" in log and t > 100:
            e.screenshot(DOCS / "jump_quest.png", scale=3)
            shot = True
    e.release()
    e.run(10)
    log += e.log()
    if "--log" in sys.argv:
        print(log)
    if gif:
        small = [g.convert("P", palette=1, colors=32) for g in gif]
        small[0].save(DOCS / "jump_quest.gif", save_all=True, append_images=small[1:],
                      duration=80, loop=0, optimize=True)
    scores = [int(v) for v in re.findall(r"score=(\d+)", log)]
    check("jumps", "jq: jump" in log)
    check("collects / scores", any(k in log for k in ("jq: collect", "jq: stomp")) and max(scores or [0]) >= 100,
          str(scores[-5:]))
    died = [int(v) for v in re.findall(r"jq: died lives=(\d+)", log)]
    check("dies: a life lost each time (2, 1, 0)", died[:3] == [2, 1, 0], str(died))
    check("respawns with a life fewer", "jq: level=1 player=1 lives=2" in log)
    m = re.search(r"jq: gameover score=(\d+) won=(\d) new_hi=(\d)", log)
    check("game over (not won)", m is not None and m.group(2) == "0", log[-300:])
    e.run(30)
    e.screenshot(SHOTS / "gameover.png", scale=3)
    check("GAME OVER in red", has_colour(e, RED, (40, 28, 120, 36)))
    e.run(160)                                    # the original's 150-tick wait
    e.press("A")
    check("A goes back to the title", e.run_until("jq: title", 60))
    rec = e.record_input("save", REC)
    print(f"  bot: {t} frames, final score {m.group(1) if m else '?'}; recorded {rec['frames']} frames")

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")

# ------------------------------------------------------------ 3: the three levels

with Emulator() as e:
    e.load(PROG)
    to_title(e)
    e.poke("jq_autoplay", 1)
    e.press("A")
    check("start (autoplay)", e.run_until("jq: start char=0 players=1", 30))
    log, pics = "", {}
    for f in range(0, 5000, 50):
        e.run(50)
        log += e.log()
        if "jq: cleared level=1" in log and "lc" not in pics:
            pics["lc"] = e.screenshot(SHOTS / "level_complete.png", scale=3)
            check("LEVEL COMPLETE on the screen", has_colour(e, WHITE, (20, 30, 140, 38)))
        if "jq: level=2" in log and "l2" not in pics and f % 400 == 0:
            pics["l2"] = e.screenshot(DOCS / ("jump_quest_level2.png" if DOCS != SHOTS else "level2.png"), scale=3)
        if "jq: level=3" in log and "l3" not in pics and f % 300 == 0:
            pics["l3"] = e.screenshot(DOCS / ("jump_quest_level3.png" if DOCS != SHOTS else "level3.png"), scale=3)
        if "jq: gameover" in log:
            break
    e.run(30)
    log += e.log()
    if "--log" in sys.argv:
        print(log)
    for lv in (1, 2, 3):
        check(f"level {lv} cleared", f"jq: cleared level={lv}" in log)
    check("no life lost on the way", "jq: died" not in log)
    sc = [int(v) for v in re.findall(r"jq: cleared level=\d score=(\d+)", log)]
    nxt = [int(v) for v in re.findall(r"jq: level=[23] player=1 lives=\d health=\d score=(\d+)", log)]
    check("the 500 bonus after each level", len(sc) >= 2 and nxt[:2] == [sc[0] + 500, sc[1] + 500], f"{sc} {nxt}")
    m = re.search(r"jq: gameover score=(\d+) won=(\d) new_hi=(\d)", log)
    check("won: CONGRATULATIONS, score 4200 (as the Spectrum and Atari ports)",
          m is not None and m.group(1) == "4200" and m.group(2) == "1", m and m.group(0))
    check("a new high score", m is not None and m.group(3) == "1")
    e.screenshot(SHOTS / "won.png", scale=3)
    check("CONGRATULATIONS in yellow", has_colour(e, YELLOW, (10, 20, 150, 28)))
    fps = [int(v) for v in re.findall(r"jq: fps=(\d+)", log)]
    avg = sum(fps) / max(1, len(fps))
    print(f"  frame rate over the three levels: {fps}")
    check("frame rate: 48+ fps average, never below 44", fps and avg >= 48 and min(fps) >= 44, f"avg {avg:.1f}")
    e.run(220)                                    # the original's 200-tick wait
    e.press("A")
    check("title shows the new high score", e.run_until("jq: title hi=4200", 60))
    e.run(40)
    e.screenshot(SHOTS / "title-hi.png", scale=3)

# ------------------------------------------------------------ 4: two players

with Emulator() as e:
    e.load(PROG)
    to_title(e)
    e.press("RIGHT")
    e.press("DOWN")
    e.press("A")
    check("two players, Dale first", e.run_until("jq: start char=1 players=2", 30))
    log = ""
    for f in range(0, 3000, 20):
        e.hold("RIGHT")                            # straight into the first pit
        e.run(20)
        log += e.log()
        if "jq: level=1 player=2" in log:
            break
    e.release()
    check("player 2 (RJ) takes over after player 1's three lives", "jq: level=1 player=2 lives=3" in log, log[-300:])
    e.run(10)
    check("HUD: RJ's name", has_colour(e, WHITE, (1, 97, 9, 102)))

print(f"{'FAIL' if failures else 'PASS'}: jump_quest")
sys.exit(1 if failures else 0)
