"""Orb Hunter (Lynx) - plays it through the harness.

1. The title (the logo in the hunter's teal; the tune plays on Mikey); A
   starts a game in room 2,2, whose tiles are the original's (levels.c,
   compiled on the host by tools/gen_world.py when the Amiga source is
   there). PAUSE stops the game (its frame counter doesn't move) and PAUSE
   goes on.
2. The bot (tools/bot.py, the Atari 8-bit port's) runs right, down the gap
   into the caverns, shoots the creatures (100 points each), takes the
   Spring Boots in room 3,3 (the item box closes on A), then walks into the
   creatures until its energy runs out: dead, GAME OVER with the score; A
   goes back to the title. Frame rate from the game's "oh: perf" lines.
3. That session was recorded from reset; it is replayed: every frame hash
   must match.
4. The power-ups, given as the original's give_items bridge hook does
   (roll form, bombs, rockets): DOWN rolls, A lays a bomb that blows and
   throws the hunter up (the bomb jump), UP stands up; in the beast's den a
   rocket (UP + A) opens the barred door; the beast walks and fires its
   spreads, the frame rate measured meanwhile.

    ./lynx test orb_hunter        (from the lynxdev checkout)
    .venv/bin/python games/orb_hunter/test.py [--shots] [--log]

--shots writes images/orb_hunter.png, orb_hunter_title.png and
orb_hunter.gif (otherwise every picture goes to build/shots).
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
from bot import Bot, step, probe, PLAYING, GAMEOVER  # noqa: E402

PROG = HERE / "build" / "orb_hunter.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
IMAGES = HERE / "images" if "--shots" in sys.argv else SHOTS
IMAGES.mkdir(parents=True, exist_ok=True)
REC = HERE / "build" / "bot-game.json"
failures = []

# GameState (cc65 packs it): room_x, room_y, score (4), state, state_timer, frame
GS_FRAME = 10
FIX = 16                                     # 12.4 fixed point


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def original_world():
    """the original's 48 rooms (levels.c), or None without its source / cc"""
    try:
        import gen_world
        return gen_world.original_world()[0]
    except Exception as ex:                  # noqa: BLE001
        print(f"  (the original's world not checked: {ex})")
        return None


def has_colour(img, rgb, box):
    x0, y0, x1, y1 = box
    return any(img.getpixel((x, y))[:3] == rgb for x in range(x0, x1) for y in range(y0, y1))


def perf_fps(log):
    """screens a second from the game's perf lines (50 * 100 / vbl), and the
    rules' rate (50 * ticks / vbl)"""
    rows = [dict(re.findall(r"(\w+)=(\d+)", l)) for l in log.splitlines() if l.startswith("oh: perf")]
    return [(5000 / int(r["vbl"]), 50 * int(r["ticks"]) / int(r["vbl"]), int(r["full"])) for r in rows]


world = original_world()
log1 = ""

with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("oh: ready", 600))
    e.run(25)                                       # (PRESS A TO START showing)
    pal = e.palette()
    title = e.screenshot(None)
    e.screenshot(IMAGES / ("orb_hunter_title.png" if IMAGES != SHOTS else "title.png"), scale=3)
    check("title: ORB HUNTER in the hunter's teal", has_colour(title, tuple(pal[2]), (20, 7, 140, 21)))
    m = e.hw()["mikey"]
    check("the tune plays on Mikey (lead, bass)", m.get("aud2vol", 0) > 0 or m.get("aud3vol", 0) > 0, m)

    e.press("A")
    check("A starts a game in room 2,2", e.run_until("oh: start room=2,2", 60))
    e.run(30)
    if world:
        check("room 2,2 is the original's (levels.c)", e.read_mem("room", 320) == world[18 * 320:19 * 320])
    shot = e.screenshot(None)
    e.screenshot(SHOTS / "start.png", scale=3)
    check("the start room drawn: rock, the save point", has_colour(shot, tuple(pal[4]), (0, 6, 160, 102))
          and has_colour(shot, tuple(pal[14]), (20, 80, 40, 96)) or has_colour(shot, tuple(pal[15]), (20, 80, 40, 96)))
    check("HUD: the energy bar in green", has_colour(shot, tuple(pal[15]), (10, 1, 42, 4)))

    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("oh: paused=1", 10))
    f0 = e.peek16(e.addr("gs") + GS_FRAME)
    e.run(50)
    check("paused: the game doesn't advance", e.peek16(e.addr("gs") + GS_FRAME) == f0)
    e.screenshot(SHOTS / "paused.png", scale=3)
    e.press("PAUSE")
    check("PAUSE again goes on", e.run_until("oh: paused=0", 10))

    bot, held = Bot(target=200), [""]
    rooms, shots_taken, gif, flashed = set(), 0, [], 0
    for f in range(9000):
        p = step(e, bot, held)
        rooms.add((p["room_x"], p["room_y"]))
        flashed = max(flashed, max(e.read_mem("e_hit_timer", 4)))
        if p["state"] == PLAYING and p["enemies"] and (p["room_x"], p["room_y"]) == (2, 3) and shots_taken == 0 \
                and p["score"] >= 100 and p["invuln"] == 0:
            e.screenshot(SHOTS / "fight.png", scale=3)
            shots_taken = 1
        if p["state"] == 4 and shots_taken == 1:
            e.run(4)
            e.screenshot(SHOTS / "item.png", scale=3)
            shots_taken = 2
        if len(gif) < 150 and f % 3 == 0 and f > 30:
            gif.append(e.screenshot(None, scale=2).convert("RGB"))
        if p["state"] == GAMEOVER:
            break
    e.release()
    log1 = e.log(0)
    check("into the caverns (another room)", len(rooms) >= 2, str(rooms))
    check("a power-up: SPRING BOOTS (and the box closed on A)", "oh: item SPRING BOOTS" in log1, log1[-400:])
    scores = [int(s) for s in re.findall(r"oh: score=(\d+)", log1)]
    check("a creature hit flashes (its 8-frame hit timer)", flashed == 8, flashed)
    check("creatures shot: 100 each", scores and max(scores) >= 100 and all(s % 100 == 0 for s in scores),
          str(scores[-3:]))
    check("hit by the creatures (5 a hit)", "oh: hit health=25" in log1)
    check("energy runs out: dead, GAME OVER with the score",
          "oh: dead" in log1 and f"oh: gameover score={max(scores) if scores else 0}" in log1)
    e.run(30)
    go = e.screenshot(None)
    e.screenshot(SHOTS / "gameover.png", scale=3)
    check("GAME OVER in lava red", has_colour(go, tuple(pal[8]), (25, 30, 135, 45)))
    e.press("A")
    check("A goes back to the title", e.run_until("oh: title", 60))
    e.run(20)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")
    if gif:
        small = [g.convert("P", palette=1, colors=32) for g in gif]
        small[0].save(IMAGES / ("orb_hunter.gif" if IMAGES != SHOTS else "play.gif"), save_all=True,
                      append_images=small[1:], duration=60, loop=0, optimize=True)

    fps = perf_fps(log1)
    play = [x for x in fps if x[2] == 0]             # (windows without a room painted)
    if fps:
        avg = sum(x[0] for x in fps) / len(fps)
        print(f"  screens a second over the bot's game: {avg:.1f} average, {min(x[0] for x in fps):.1f} lowest"
              f" ({len(fps)} windows of 100); rules {sum(x[1] for x in fps) / len(fps):.1f} Hz;"
              f" in the {len(play)} windows without a room change: {sum(x[0] for x in play) / max(1, len(play)):.1f}")
        check("rules at 50 Hz (they catch up after a slow frame; a room change costs a step or two)",
              sum(x[1] for x in fps) / len(fps) >= 49.5 and all(x[1] >= 47 for x in fps),
              str([round(x[1], 1) for x in fps]))
        check(">= 45 screens a second on average", avg >= 45, f"{avg:.1f}")

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")

with Emulator() as e:
    e.load(PROG)
    e.run_until("oh: ready", 600)
    e.run(10)
    e.press("A")
    e.run_until("oh: start room=2,2", 60)
    e.run(80)                                       # (the start's invulnerability: 60 frames)
    # the original's give_items hook: roll form, bombs, rockets (30), far shot
    for sym, v in (("h_has_roll_form", 1), ("h_has_bombs", 1), ("h_has_missiles", 1), ("h_has_long_beam", 1)):
        e.poke(e.addr(sym), v)
    e.write_mem(e.addr("h_missiles"), struct.pack("<h", 30))
    e.write_mem(e.addr("h_max_missiles"), struct.pack("<h", 30))
    e.log()
    e.press("DOWN")
    check("DOWN: the roll form", e.run_until("oh: rolled=1", 20))
    e.screenshot(SHOTS / "rolled.png", scale=3)
    y0 = probe(e)["hy"]
    e.press("A")
    bombs = e.read_mem("bm_on", 3)
    check("A rolled: a bomb", any(bombs), str(list(bombs)))
    e.run(12)
    e.screenshot(SHOTS / "bomb.png", scale=3)
    low, dy = y0, 0
    for _ in range(30):
        e.run(1)
        low = min(low, probe(e)["hy"])
        dy = min(dy, struct.unpack("<h", e.read_mem("h_dy", 2))[0])
    # (the original's bomb jump: dy = -3 pixels a frame; its short-jump rule
    # halves that every frame UP isn't held, so on open ground it is a hop)
    check("the bomb blows and throws the hunter up (the bomb jump: dy -3)", dy == -3 * FIX,
          f"dy {dy}, y {y0} -> {low}")
    e.run(30)
    e.press("UP")
    check("UP: stands up", e.run_until("oh: rolled=0", 20))
    # into the beast's den (4,3) the way the room change does it: from the
    # right edge of 3,3 at the floor; then onto its left ledge (row 8)
    gs = e.addr("gs")
    e.write_mem(gs, bytes([3, 3]))
    e.write_mem(e.addr("h_x"), struct.pack("<h", (320 - 14) * FIX))
    e.write_mem(e.addr("h_y"), struct.pack("<h", (224 - 16) * FIX))
    check("into the beast's den (room 4,3)", e.run_until("oh: room 4,3", 10))
    e.run(2)
    check("the den's door is barred (the original's levels.c)", e.read_mem("room", 320)[7 * 20] == 6)
    e.write_mem(e.addr("h_x"), struct.pack("<h", 56 * FIX))
    e.write_mem(e.addr("h_y"), struct.pack("<h", 112 * FIX))
    e.run(3)
    e.press("LEFT", hold_frames=1, release_frames=2)
    e.log()
    m0 = probe(e)["missiles"]
    e.press("UP+A", hold_frames=2, release_frames=1)
    check("UP + A: a rocket (one fewer)", probe(e)["missiles"] == m0 - 1, f"{m0} -> {probe(e)['missiles']}")
    check("the rocket opens the barred door", e.run_until("oh: door open", 30))
    e.run(4)
    door = e.read_mem("room", 320)[6 * 20]
    check("the door tile is open now", door == 7 or e.read_mem("room", 320)[7 * 20] == 7, str(door))
    # the beast's den: it walks and fires its spreads; the picture when the
    # hunter shows and its shots fly; the frame rate meanwhile
    e.log()
    shot, fired = None, None
    for k in range(300):
        e.run(1)
        if fired is None and sum(e.read_mem("eb_on", 6)) >= 2:
            fired = k
        if shot is None and fired is not None and k >= fired + 10 and probe(e)["invuln"] == 0 \
                and sum(e.read_mem("eb_on", 6)):            # (its spread clear of it)
            e.screenshot(IMAGES / ("orb_hunter.png" if IMAGES != SHOTS else "den.png"), scale=3)
            shot = e.screenshot(None)
    den = shot or e.screenshot(None)
    check("the beast is drawn (its purple) and fires", shot is not None and
          has_colour(den, tuple(e.palette()[11]), (60, 40, 160, 102)))
    fps = perf_fps(e.log())
    if fps:
        print(f"  screens a second in the beast's den: {[round(x[0], 1) for x in fps]}")
        check("the den at 50 screens a second", all(x[0] >= 49 for x in fps if x[2] == 0), str(fps))

# ------------------------------------------------------------ 5. the original's fixes (amiga_games #50)

def screen_has(e, rgb, box):
    return has_colour(e.screenshot(None), rgb, box)


with Emulator() as e:
    e.load(PROG)
    e.run_until("oh: ready", 600)
    e.run(10)
    e.press("A")
    e.run_until("oh: start room=2,2", 60)
    pal = e.palette()
    gs = e.addr("gs")
    # one frame count a tick (the original counted twice a loop, so its
    # drawing always saw the count odd)
    f0 = e.peek16(gs + GS_FRAME)
    e.run(20)
    df = e.peek16(gs + GS_FRAME) - f0
    check("the frame counter goes up once a tick", 19 <= df <= 21, df)
    # the start's invulnerability: the hunter flickers (seen on about half the frames)
    seen = 0
    for _ in range(20):
        e.run(1)
        seen += screen_has(e, tuple(pal[2]), (0, 6, 160, 102))
    check("invulnerable: the hunter flickers (drawn every other frame)", 7 <= seen <= 13, seen)
    e.run(60)
    # the citadel (3,4), the way the room change does it: from the right
    # edge of 2,4 on the citadel's floor; its creatures stand on row 12
    e.write_mem(gs, bytes([2, 4]))
    e.write_mem(e.addr("h_x"), struct.pack("<h", (320 - 14) * FIX))
    e.write_mem(e.addr("h_y"), struct.pack("<h", (12 * 16 - 16) * FIX))
    check("into the citadel (room 3,4)", e.run_until("oh: room 3,4", 10))
    on = list(e.read_mem("e_on", 4))
    ys = [struct.unpack("<h", e.read_mem(e.addr("e_y") + 2 * i, 2))[0] // FIX for i in range(4) if on[i]]
    check("the citadel's creatures spawn on its floor (row 12, not in it at row 13)",
          len(ys) == 2 and all(y == 12 * 16 for y in ys), ys)
    e.run(10)
    e.screenshot(SHOTS / "citadel.png", scale=3)
    # the item box closes after game.c's 90 frames (or a press)
    e.write_mem(gs + 6, struct.pack("<hh", 4, 90))
    e.run(60)
    check("the item box stays up without a press...", e.peek(gs + 6) == 4)
    e.run(40)
    check("...and closes after 90 frames", e.peek(gs + 6) == 1)
    # GAME OVER goes back to the title after 180 frames (or a press)
    e.write_mem(e.addr("h_health"), struct.pack("<h", 0))
    e.write_mem(gs + 6, struct.pack("<hh", 2, 1))       # dead: GAME OVER next frame
    check("GAME OVER", e.run_until("oh: gameover", 10))
    e.run(30)
    e.screenshot(SHOTS / "gameover_score.png", scale=3)
    check("GAME OVER shows the score", screen_has(e, tuple(pal[7]), (0, 54, 160, 61)))
    check("...and goes back to the title by itself after 180 frames", e.run_until("oh: title", 180))

if "--log" in sys.argv:
    print(log1)
print(f"{'FAIL' if failures else 'PASS'}: orb_hunter")
sys.exit(1 if failures else 0)
