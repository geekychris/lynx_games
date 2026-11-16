"""Sky Knights (Lynx) - plays a game through the harness.

Boots to the title (the tune on Mikey channel 3), A starts a one-player
game: wave 1 with the original's three swoopers. PAUSE stops the game and
PAUSE goes on. Then a bot reads the game by symbol (b_px, b_py, e_active,
g_x ...) and plays: it collects the pods before they hatch, and otherwise
flies above the nearest enemy and drops onto it (the higher knight wins);
in wave 2 it leaves the pods until one hatches into a new enemy.
Once it has 12000 points it gives up and flies level with the enemies until
it has lost its three lives. Checks the log lines (sk: ...) against the
original's scoring (swooper 500, raider 750, wraith 1000, a pod 250, a
cleared wave 1000), the waves, lives and game over, the screens, and the
frame rate from the game's own "sk: fps=" lines; the whole game is recorded
and replayed with every frame hash matching. Then wave 6 (eight enemies,
the busiest) is poked in and its frame rate measured.

    .venv/bin/python games/sky_knights/test.py [--shots] [--log]

--shots also writes images/sky_knights.png, sky_knights_title.png and
sky_knights.gif (otherwise every picture goes to build/shots).
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
PROG = HERE / "build" / "sky_knights.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
DOCS = HERE / "images" if "--shots" in sys.argv else SHOTS
REC = HERE / "build" / "bot-game.json"
failures = []

NB, NE, NG = 10, 8, 8                       # bodies (2 knights + 8 enemies), enemies, pods
TITLE, PLAYING, INTRO, GAMEOVER = 0, 1, 2, 4
# palette indices (main.c)
COL_PLATFORM, COL_P1_MOUNT, COL_RED, COL_SWOOPER, COL_SCORE, COL_GROUND = 2, 4, 7, 8, 12, 13


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def world(e):
    def words(sym, n):
        return list(struct.unpack(f"<{n}h", e.read_mem(sym, 2 * n)))

    def bytes_(sym, n):
        return list(e.read_mem(sym, n))
    gs = struct.unpack("<2l7h", e.read_mem("gs", 22))
    return {"px": words("b_px", NB), "py": bytes_("b_py", NB), "dy": words("b_dy", NB),
            "dx": words("b_dx", NB), "facing": bytes_("b_facing", NB),
            "alive": bytes_("p_alive", 2), "lives": struct.unpack("<2b", e.read_mem("p_lives", 2)),
            "e_active": bytes_("e_active", NE), "e_type": bytes_("e_type", NE),
            "gx": [v >> 6 for v in words("g_x", NG)], "gy": [v >> 6 for v in words("g_y", NG)],
            "g_active": bytes_("g_active", NG),
            "score": gs[0], "wave": gs[2], "state": gs[3]}


def wrap_dx(a, b):
    """b - a the short way round (the screen wraps at 320 + 20)"""
    d = b - a
    if d > 170:
        d -= 340
    if d < -170:
        d += 340
    return d


class Bot:
    """Pods first (before they hatch; but in wave 2 it leaves them until one
    has hatched), else the nearest enemy: fly to 30 pixels above it, then
    drop onto it. From `target` points on it gives up:
    it flies level with the enemies (a little under) until it is out of lives.
    (The Atari 8-bit port's bot, the same world.)"""

    def __init__(self, target=12000, hatch_wave=2):
        self.target = target
        self.give_up = False
        self.hatch_wave = hatch_wave        # in this wave it leaves the pods until one hatches
        self.hatched = False
        self.last_en = None

    def keys(self, w):
        en = sum(w["e_active"])
        if self.last_en is not None and w["state"] == PLAYING and en > self.last_en and w["wave"] == self.hatch_wave:
            self.hatched = True
        self.last_en = en if w["state"] == PLAYING else None
        if w["state"] != PLAYING or not w["alive"][0]:
            return ""
        if w["score"] >= self.target:
            self.give_up = True
        px, py, dy = w["px"][0], w["py"][0], w["dy"][0]
        enemies = [(w["px"][2 + i], w["py"][2 + i]) for i in range(NE) if w["e_active"][i]]
        pods = [(w["gx"][i], w["gy"][i]) for i in range(NG) if w["g_active"][i]]
        if self.give_up:
            if not enemies:
                return ""
            tx, ty = min(enemies, key=lambda en: abs(wrap_dx(px, en[0])))
            ty += 8
        elif pods and (w["wave"] != self.hatch_wave or self.hatched):
            tx, ty = min(pods, key=lambda g: abs(wrap_dx(px, g[0])))
            tx -= 6
            ty -= 14
        elif enemies:
            tx, ty = min(enemies, key=lambda en: abs(wrap_dx(px, en[0])) + abs(en[1] - py))
            ty -= 6 if abs(wrap_dx(px, tx)) < 14 else 30
        else:
            return ""
        dx = wrap_dx(px, tx)
        if not self.give_up:
            # an enemy close by, level with it or higher: climb away from it
            for ex, ey in enemies:
                ddx = wrap_dx(px, ex)
                if abs(ddx) < 34 and ey <= py + 3 and py > 24:
                    dx, ty = -ddx, py - 40
                    break
        k = []
        if dx < -3:
            k.append("LEFT")
        elif dx > 3:
            k.append("RIGHT")
        if py > ty and dy > -150:
            k.append("A")
        return "+".join(k)


backwards = [0]                             # enemy frames flying backwards (bot_game)


def bot_game(e, max_frames=30000):
    bot, held, log, t, gif, shot = Bot(), "", "", 0, [], False
    while t < max_frames:
        w = world(e)
        if w["state"] == GAMEOVER:
            break
        if w["state"] == PLAYING:
            # an enemy faces the way it flies (facing 1 = left); the original
            # left it facing backwards after the equal-height bounce
            for i in range(NE):
                dx = w["dx"][2 + i]
                if w["e_active"][i] and dx and w["facing"][2 + i] != (dx < 0):
                    backwards[0] += 1
        want = bot.keys(w)
        if want != held:
            e.release()
            if want:
                e.hold(want)
            held = want
        e.run(1)
        t += 1
        if 300 <= t < 700 and t % 3 == 0:
            gif.append(e.screenshot(None, scale=2).convert("RGB"))
        if not shot and w["state"] == PLAYING and sum(w["e_active"]) >= 2 and sum(w["g_active"]) >= 1 and t > 300:
            e.screenshot(DOCS / "sky_knights.png", scale=3)
            shot = True
        log += e.log()
    e.release()
    if gif:
        small = [g.convert("P", palette=1, colors=32) for g in gif]
        small[0].save(DOCS / "sky_knights.gif", save_all=True, append_images=small[1:],
                      duration=60, loop=0, optimize=True)
    return log + e.log(), t


def score_events(log):
    """the scoring lines of the bot's game: (kind, score, points since the
    previous line, the jousts' points, pods)"""
    out, prev = [], 0
    for m in re.finditer(r"sk: (score|waveclear wave=\d+|gameover)(.*?)score=(\d+)", log):
        kind = m.group(1).split()[0]
        sc = int(m.group(3))
        kills = [int(v) for v in re.findall(r"kill=(\d+)", m.group(2))]
        pods = int((re.findall(r"pods=(\d+)", m.group(2)) or [0])[0])
        out.append((kind, sc, sc - prev, kills, pods))
        prev = sc
    return out


def fps_lines(log):
    return [tuple(map(int, f)) for f in re.findall(r"sk: fps=(\d+) late=(\d+) busy=(\d+)/\d+ en=(\d+) pods=(\d+)", log)]


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("sk: title", 600))
    e.run(60)
    e.screenshot(DOCS / ("sky_knights_title.png" if DOCS != SHOTS else "title.png"), scale=3)
    pal = e.palette()
    small = e.screenshot(None)
    check("title: the yellow SKY KNIGHTS", sum(small.getpixel((x, y)) == tuple(pal[COL_SCORE])
                                               for x in range(160) for y in range(6, 20)) > 100)
    check("title: the knight on its ledge", any(small.getpixel((x, y)) == tuple(pal[COL_P1_MOUNT])
                                                 for x in range(70, 90) for y in range(38, 56)))
    m = e.hw()["mikey"]
    check("the tune plays on Mikey channel 3", m.get("aud3ctl", 0) & 0x18 == 0x18 and m.get("aud3vol", 0) > 0, m)
    e.press("A")
    check("A starts a one-player game", e.run_until("sk: start players=1", 30))
    e.run(30)
    e.screenshot(SHOTS / "wave1.png", scale=3)
    small = e.screenshot(None)
    check("wave intro: WAVE 1 in yellow", sum(small.getpixel((x, y)) == tuple(pal[COL_SCORE])
                                              for x in range(40, 120) for y in range(30, 44)) > 50)
    check("wave 1: three swoopers", e.run_until("sk: wave=1 enemies=3", 120))
    w = world(e)
    check("wave 1: swoopers only", [w["e_type"][i] for i in range(NE) if w["e_active"][i]] == [0, 0, 0], w["e_type"])
    e.run(10)
    small = e.screenshot(None)
    check("the ground (tan) and ledges (brown)", all(small.getpixel((x, 98)) == tuple(pal[COL_GROUND]) for x in range(0, 160, 7))
          and sum(small.getpixel((x, 72)) == tuple(pal[COL_PLATFORM]) for x in range(160)) >= 70)
    check("swoopers drawn (orange)", any(small.getpixel((x, y)) == tuple(pal[COL_SWOOPER])
                                         for x in range(160) for y in range(10, 94)))
    check("the knight drawn (yellow mount)", any(small.getpixel((x, y)) == tuple(pal[COL_P1_MOUNT])
                                                 for x in range(30, 60) for y in range(80, 94)))
    e.hold("A")
    e.run(3)
    m = e.hw()["mikey"]
    e.release()
    check("flapping: the wing beat on Mikey channel 0", m.get("aud0vol", 0) > 0, m)
    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("sk: paused=1", 10))
    w0 = world(e)
    e.run(50)
    check("paused: nothing moves", world(e)["px"] == w0["px"] and world(e)["py"] == w0["py"])
    e.screenshot(SHOTS / "paused.png", scale=3)
    e.press("PAUSE")
    check("PAUSE again goes on", e.run_until("sk: paused=0", 10))

    log, t = bot_game(e)
    if "--log" in sys.argv:
        print(log)
    ev = score_events(log)
    kills = [k for _, _, _, ks, _ in ev for k in ks]
    pods = sum(p for *_, p in ev)
    print(f"  played {t} frames: {len(kills)} jousts won, {pods} pods, "
          f"{log.count('sk: hatch')} hatched, {log.count('sk: waveclear')} waves cleared")
    check("every enemy faces the way it flies (also after an equal-height bounce)",
          backwards[0] == 0, f"{backwards[0]} enemy frames backwards")
    check("jousts won", len(kills) >= 5, kills)
    check("the first joust: a swooper's 500", kills[:1] == [500], kills[:1])
    check("every joust scores 500 / 750 / 1000", kills and set(kills) <= {500, 750, 1000}, set(kills))
    check("pods taken", pods >= 3, pods)
    # each frame's points: its jousts + 250 a pod (+ 1000 when that clears the wave)
    ok = True
    for i, (k, sc, d, ks, p) in enumerate(ev):
        clear = i + 1 < len(ev) and ev[i + 1][0] == "waveclear" and ev[i + 1][1] == sc
        if k == "score" and d != sum(ks) + 250 * p + (1000 if clear else 0):
            ok = False
    check("the score adds up: jousts, 250 a pod, 1000 a cleared wave", ok, ev[:12])
    check("a wave cleared: wave 2 starts", "sk: waveclear wave=1" in log and "sk: wave=2 enemies=4" in log)
    check("wave 2: a pod left alone hatches into a new enemy", "sk: hatch enemies=" in log)
    check("wave 3 brings raiders (750)", "sk: wave=3 enemies=5" in log and 750 in kills, kills)
    check("lives lost", all(f"sk: died lives={n}" in log for n in (2, 1, 0)))
    mo = re.search(r"sk: gameover score=(\d+) wave=(\d+)", log)
    check("game over after the last life", mo is not None, log[-300:])
    last = [sc for k, sc, *_ in ev if k != "gameover"]
    check("game over score = the last score", mo and last and int(mo.group(1)) == last[-1])
    fps = fps_lines(log)
    print(f"  frame rate (fps, late frames, busiest frame in display lines /105, enemies, pods): {fps}")
    check("frame rate: 50 fps in every 2-second window", fps and all(f == 50 for f, *_ in fps),
          [f for f, *_ in fps])
    e.run(20)
    e.screenshot(SHOTS / "gameover.png", scale=3)
    small = e.screenshot(None)
    check("GAME OVER in red", sum(small.getpixel((x, y)) == tuple(pal[COL_RED])
                                  for x in range(20, 140) for y in range(28, 42)) > 50)
    check("back to the title after 4 s (the original's 200 frames)", e.run_until("sk: title", 220))
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

    # the busiest waves: wave 6 on (eight enemies: swoopers, raiders, wraiths),
    # the wave number poked in during the intro; the knight just sits on the ground
    e.run(40)
    e.press("A")
    check("a new game", e.run_until("sk: start players=1", 30))
    e.write_mem(e.addr("gs") + 8, bytes([6, 0]))
    check("wave 6: eight enemies", e.run_until("sk: wave=6 enemies=8", 150))
    w = world(e)
    types = sorted(w["e_type"][i] for i in range(NE) if w["e_active"][i])
    check("wave 6: 2 swoopers, 2 raiders, 4 wraiths", types == [0, 0, 1, 1, 2, 2, 2, 2], types)
    e.run(30)
    e.screenshot(SHOTS / "wave6.png", scale=3)
    e.log()
    e.run(500)
    fps6 = fps_lines(e.log())
    print(f"  wave 6: {fps6}")
    check("wave 6: 50 fps", fps6 and all(f == 50 for f, *_ in fps6), fps6)

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")

print(f"{'FAIL' if failures else 'PASS'}: sky_knights")
sys.exit(1 if failures else 0)
