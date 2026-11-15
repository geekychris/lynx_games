#!/usr/bin/env python3
"""Boing Twist for the Atari Lynx: everything made on the host.

Run it after changing anything here (its outputs are checked in):

    python3 games/boing_twist/tools/gen_data.py

What it makes:

  src/view.s     the play ball's sprite skeleton (64 x 64, literal 4 bpp) and
                 the tables view_play (src/ballasm.s) draws it from: per
                 sprite line, its pairs of pixels in runs sharing a texture
                 window and a light step; per pair the two texels' offsets
                 at turn 0. The light is the original's (fixed to the eye:
                 diffuse + highlight) in three steps.
  cart/flip.bin  the flip's tables: the half resampled round its axis (16
                 rings x 64 angles) and back (cartridge file, read into the
                 second screen buffer when a game starts)
  cart/pic_NN.bin  the 17 pictures, 64 x 32 texels in 3 colours, 2 bits each
  cart/room_NN.bin the ten rooms as Suzy shapes (src/roomasm.s)
  cart/title.bin the title's Boing ball: 52 x 52, leaning -17 degrees, its
                 pens the colour-cycling phases, in sub-sprites by light step
  src/gfx_data.c/.h  the pictures' titles, levels, rooms and colours; the
                 rooms' colours; the bands' rows; the sub-sprites' places;
                 the logo and the shadow's disc
  src/directory.s, src/cart.s, boing_twist.cfg  the cartridge's files

The original (amiga_mcp/examples/boing_twist, or $BT_ORIG) is the source of
the camera (45 degree lens, the ball 3.6 away, tipped 12 degrees), the
light, the rooms (themes/*.theme: back wall 6.5 away, floor 1.5 down, grid
of 0.5), the pictures (images/*.jpg, titles.txt) and the title's ball.
"""
import math
import os

import numpy as np
from PIL import Image, ImageEnhance

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
ORIG = os.environ.get("BT_ORIG") or os.path.normpath(
    os.path.join(PROJ, "..", "..", "..", "amiga_mcp", "examples", "boing_twist"))

TEX_W, TEX_H, ROW = 64, 32, 96
PLAY_D = 64                                   # the play ball's diameter (pixels)
PLAY_DIST, TITLE_DIST = 3.6, 4.4
PITCH = 12.0                                  # the original's play view: tipped towards you
FOCAL = (PLAY_D / 2) / math.tan(math.asin(1 / PLAY_DIST))   # pixels per unit at distance 1
CX, CY = 80, 51                               # the screen point straight ahead (the play ball's middle)
LIGHT = np.array([-0.42, 0.52, 0.74])         # render.c: towards the light, eye space
LIGHT /= np.linalg.norm(LIGHT)
NR, NA = 16, 64                               # the flip's polar grid: radius x angle
ROOM_Z, ROOM_Y, BOX_X, BOX_Y = -6.5, -1.5, 3.0, 2.2
HUD_TOP, HUD_BOT = 7, 95                      # the room shows between the HUD strips


def rot_axis(ax, ay, az, deg):
    a = math.radians(deg)
    c, s, t = math.cos(a), math.sin(a), 1 - math.cos(a)
    return np.array([
        [t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay],
        [t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax],
        [t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c],
    ])


def ball_rot(pitch, roll):
    """render.c ball_rot at yaw 0: the axis tipped (pitch), then leaned (roll).
    Maps the ball's own frame to the eye's; q = p @ M takes eye to ball."""
    return rot_axis(0, 0, 1, roll) @ rot_axis(1, 0, 0, pitch)


def ball_pixels(dist, pitch, roll):
    """every pixel the ball covers, relative to its middle: x, y (pixel
    offsets), q (the point in the ball's frame), n (the eye-space normal)"""
    n = int(FOCAL * math.tan(math.asin(1 / dist))) + 2
    ys, xs = np.mgrid[-n:n, -n:n]
    d = np.stack([(xs + 0.5) / FOCAL, -(ys + 0.5) / FOCAL, -np.ones(xs.shape)], -1)
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    C = np.array([0.0, 0.0, -dist])
    b = d @ C
    disc = b * b - (C @ C - 1.0)
    hit = disc >= 0
    t = b - np.sqrt(np.where(hit, disc, 0))
    p = t[..., None] * d - C
    q = p @ ball_rot(pitch, roll)
    return xs[hit], ys[hit], q[hit], p[hit]


def lonlat(q):
    lat = np.degrees(np.arcsin(np.clip(q[..., 1], -1, 1)))
    lon = np.degrees(np.arctan2(q[..., 0], q[..., 2]))
    return lon, lat


def tex_uv(q):
    lon, lat = lonlat(q)
    u = np.floor(lon * TEX_W / 360.0).astype(int) % TEX_W
    v = np.clip(np.floor((90.0 - lat) * TEX_H / 180.0).astype(int), 0, TEX_H - 1)
    return u, v


def shade_class(n):
    """the original's light: diffuse + a highlight; 0 mid, 1 dark, 2 bright"""
    lam = np.clip(n @ LIGHT, 0, 1)
    b = 0.30 + 0.70 * lam
    h = np.array([0.0, 0.0, 1.0]) + LIGHT
    h /= np.linalg.norm(h)
    spec = np.clip(n @ h, 0, 1) ** 24
    cls = np.where(b < 0.62, 1, 0)
    cls = np.where(spec > 0.35, 2, cls)
    return cls, b, spec


# ---------------------------------------------------------------- the play view
class PlayView:
    def __init__(self):
        xs, ys, q, n = ball_pixels(PLAY_DIST, PITCH, 0.0)
        self.R = int(round(PLAY_D / 2))
        u, v = tex_uv(q)
        ur = (u + 16) % TEX_W
        assert ur.max() < 32 and ur.min() >= 0, (ur.min(), ur.max())
        self.xs, self.ys, self.u, self.v, self.ur = xs, ys, u, v, ur
        self.q = q
        self.cls, self.bright, self.spec = shade_class(n)
        self.x0 = int(xs.min())
        self.y0 = int(ys.min())
        self.w = int(xs.max()) - self.x0 + 1
        self.h = int(ys.max()) - self.y0 + 1
        print(f"  play view: {len(xs)} pixels, {self.w} x {self.h}, shade classes "
              f"{[int((self.cls == k).sum()) for k in range(3)]}")

    def tables(self):
        """The drawing tables (ballasm.s view_play reads them). Per sprite
        line: the pixels in pairs (one byte of the 4-bit sprite each); for
        each pair the texel offsets of its two pixels (even[], odd[]: dv * 96
        + ur, from the run's window row v0, dv <= 2; odd 255 when both pixels
        show the same texel); runs of pairs with one
        window row and one light step (a pair takes its left pixel's step);
        masks for a first / last byte half outside the ball. Also the
        sprite's skeleton (each line's length byte, zeros, the pad byte)."""
        pix = {}
        for x, y, v, ur, c in zip(self.xs, self.ys, self.v, self.ur, self.cls):
            pix[(int(y) - self.y0, int(x) - self.x0)] = (int(v), int(ur), int(c))
        spr, rows, even, odd = [], [], [], []
        k = 0
        nruns = 0
        for r in range(self.h):
            cols = sorted(c for (rr, c) in pix if rr == r)
            first, last = cols[0], cols[-1]
            nb = last // 2 + 1
            spr.append(nb + 2)
            k += 1
            b0 = first // 2
            out_off = k + b0
            spr += [0] * nb + [0]
            k += nb + 1
            pairs = []
            for bx in range(b0, nb):
                a, b = pix.get((r, 2 * bx)), pix.get((r, 2 * bx + 1))
                pairs.append((a, b))
            masks = (1 if pairs[0][0] is None else 0) | (2 if pairs[-1][1] is None else 0)
            runs, cur = [], None
            for i, (a, b) in enumerate(pairs):
                sh = (a or b)[2]
                vs = [q[0] for q in (a, b) if q]
                if cur and cur["sh"] == sh and max(vs + [cur["vmax"]]) - min(vs + [cur["vmin"]]) <= 2:
                    cur["vmin"] = min(vs + [cur["vmin"]])
                    cur["vmax"] = max(vs + [cur["vmax"]])
                    cur["pairs"].append((a, b))
                else:
                    cur = {"sh": sh, "vmin": min(vs), "vmax": max(vs), "pairs": [(a, b)]}
                    runs.append(cur)
            stream_off = len(even)
            hdr_runs = []
            end = 0
            for run in runs:
                v0 = run["vmin"]
                for a, b in run["pairs"]:
                    qa, qb = a or b, b or a
                    even.append((qa[0] - v0) * ROW + qa[1])
                    # 255: the same texel as the even pixel (one fetch makes the byte)
                    odd.append(255 if qb[:2] == qa[:2] else (qb[0] - v0) * ROW + qb[1])
                end += len(run["pairs"])
                hdr_runs.append((end, v0, run["sh"]))
            nruns += len(runs)
            rows.append((out_off, stream_off, len(pairs), masks, hdr_runs))
        spr.append(0)
        return spr, rows, even, odd, nruns

    def flip_tables(self):
        """The flip turns the half facing you 180 degrees about the axis
        through its middle (in the ball's frame +z at turn 0). P is the half
        resampled round that axis (NR rings x NA angles). pbuild[r][a]: the
        window texel (v * 96 + ur) that P[r][a] samples. wrow[v]: per window
        row, the first ur and the P cell (r, a) of each texel."""
        used = {}
        for v, ur in zip(self.v, self.ur):
            used.setdefault(int(v), set()).add(int(ur))
        pbuild = []
        for r in range(NR):
            for a in range(NA):
                rad = math.radians((r + 0.5) * 90.0 / NR)
                ang = math.radians((a + 0.5) * 360.0 / NA)
                q = np.array([math.sin(rad) * math.cos(ang), math.sin(rad) * math.sin(ang), math.cos(rad)])
                u, v = tex_uv(q[None, :])
                ur = (int(u[0]) + 16) % TEX_W
                ur = min(max(ur, 0), 31)
                pbuild.append(int(v[0]) * ROW + ur)
        rows = []
        for v in sorted(used):
            u0, u1 = min(used[v]), max(used[v])
            idx = []
            for ur in range(u0, u1 + 1):
                lon = math.radians((ur - 16 + 0.5) * 360.0 / TEX_W)
                lat = math.radians(90.0 - (v + 0.5) * 180.0 / TEX_H)
                q = np.array([math.cos(lat) * math.sin(lon), math.sin(lat), math.cos(lat) * math.cos(lon)])
                rad = math.degrees(math.acos(max(-1.0, min(1.0, q[2]))))
                ang = math.degrees(math.atan2(q[1], q[0])) % 360.0
                r = min(int(rad * NR / 90.0), NR - 1)
                a = int(ang * NA / 360.0) % NA
                idx.append((r, a))
            rows.append((v, u0, idx))
        return pbuild, rows

    def band_rows(self, bands):
        """each band's screen rows down the ball's middle column (from the
        ball's top), for the markers"""
        out = []
        mid = self.xs == -1
        for b in range(bands):
            v0 = b * TEX_H // bands
            v1 = (b + 1) * TEX_H // bands
            sel = mid & (self.v >= v0) & (self.v < v1)
            ys = self.ys[sel] - self.y0
            out.append((int(ys.min()), int(ys.max())) if len(ys) else (0, 0))
        return out


# ---------------------------------------------------------------- the title's Boing ball
TITLE_LEVELS = 5
TITLE_PHASES = 14                       # the Amiga demo's 14 colour-cycling steps


def title_ball():
    """The 1984 demo's ball: 8 x 16 squares, leaning -17 degrees, 4.4 away.
    Each pixel's pen is its phase, 1 + (step + 7 * row parity) mod 14, where
    step is its longitude in 1/7ths of a square: cycling which pens are red
    and which white turns the ball (the demo's colour cycling). The light
    is baked in as zones: rows in fours, each split where the light's step
    changes; each zone is a sub-sprite whose pen palette has that step's
    red and white. Returns the sub-sprites (dx, dy, level, w, h, data)."""
    xs, ys, q, n = ball_pixels(TITLE_DIST, 0.0, -17.0)
    lon, lat = lonlat(q)
    step = np.floor((lon % 360.0) / (22.5 / 7)).astype(int)
    row = np.clip(np.floor((90.0 - lat) / 22.5).astype(int), 0, 7)
    pen = 1 + (step + 7 * (row & 1)) % TITLE_PHASES
    lam = np.clip(n @ LIGHT, 0, 1)
    h = np.array([0.0, 0.0, 1.0]) + LIGHT
    h /= np.linalg.norm(h)
    spec = np.clip(n @ h, 0, 1) ** 24
    lev = np.clip(np.floor((0.30 + 0.70 * lam) * TITLE_LEVELS - 0.6).astype(int), 0, TITLE_LEVELS - 2)
    lev = np.where(spec > 0.35, TITLE_LEVELS - 1, lev)
    x0, y0 = int(xs.min()), int(ys.min())
    W, H = int(xs.max()) - x0 + 1, int(ys.max()) - y0 + 1
    img = np.zeros((H, W), int)
    lv = np.full((H, W), -1)
    img[ys - y0, xs - x0] = pen
    lv[ys - y0, xs - x0] = lev
    subs = []
    G = 4
    for gy in range(0, H, G):
        band = lv[gy:gy + G]
        cols = [c for c in range(W) if (band[:, c] >= 0).any()]
        if not cols:
            continue
        # per column the most common level, runs merged when shorter than 3
        colv = []
        for c in range(cols[0], cols[-1] + 1):
            vals = band[:, c][band[:, c] >= 0]
            colv.append(int(np.bincount(vals).argmax()) if len(vals) else -1)
        runs = []
        for i, val in enumerate(colv):
            if runs and runs[-1][2] == val:
                runs[-1][1] = i
            else:
                runs.append([i, i, val])
        changed = True
        while changed and len(runs) > 1:
            changed = False
            for i, r in enumerate(runs):
                if r[1] - r[0] + 1 < 3:
                    j = i - 1 if i > 0 else i + 1
                    if i + 1 < len(runs) and i > 0 and (runs[i + 1][1] - runs[i + 1][0]) > (runs[i - 1][1] - runs[i - 1][0]):
                        j = i + 1
                    a, b = sorted((i, j))
                    runs[a] = [runs[a][0], runs[b][1], runs[j][2]]
                    del runs[b]
                    changed = True
                    break
        for a, b, val in runs:
            c0, c1 = cols[0] + a, cols[0] + b
            sub = img[gy:gy + G, c0:c1 + 1]
            data = bytearray()
            hh = sub.shape[0]
            for rr in range(hh):
                line = list(sub[rr])
                if len(line) % 2:
                    line.append(0)
                bts = bytes((line[i] << 4) | line[i + 1] for i in range(0, len(line), 2))
                data.append(len(bts) + 2)
                data += bts
                data.append(0)
            data.append(0)
            subs.append((c0 + x0, gy + y0, max(val, 0), c1 - c0 + 1, hh, bytes(data)))
    print(f"  title ball: {len(xs)} pixels, {W} x {H}, {len(subs)} sub-sprites, "
          f"{sum(len(s[5]) for s in subs)} bytes")
    return subs, (x0, y0, W, H)


# ---------------------------------------------------------------- colours
def rgb12(rgb):
    r, g, b = (max(0, min(15, int(round(k / 255.0 * 15)))) for k in rgb)
    return (r << 8) | (g << 4) | b


def hexrgb(s):
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def dim(rgb, k):
    return tuple(int(c * k) for c in rgb)


# ---------------------------------------------------------------- pictures
LEVELS = {"easy": 0, "medium": 1, "hard": 2, "expert": 3}
ROOM_ORDER = ["boing", "midnight", "space", "aquarium", "arcade", "garden", "observatory",
              "retroroom", "study", "workshop"]


def read_titles():
    pics = []
    for line in open(os.path.join(ORIG, "images", "titles.txt")):
        line = line.split("#")[0].strip()
        if not line or "=" not in line:
            continue
        name, rest = (s.strip() for s in line.split("=", 1))
        parts = [p.strip() for p in rest.split("|")]
        title = parts[0].upper()
        level = LEVELS[parts[1].lower()] if len(parts) > 1 and parts[1] else 0
        room = parts[2] if len(parts) > 2 else ""
        pics.append((int(name.split("_")[0]), name, title, level, room))
    pics.sort()
    return pics


def picture(path):
    """64 x 32 texels in 3 colours (values 1..3; 0 is the groove), darkest first"""
    im = Image.open(path).convert("RGB").resize((TEX_W, TEX_H), Image.LANCZOS)
    im = ImageEnhance.Color(im).enhance(1.4)
    q = im.quantize(colors=3, method=Image.Quantize.MEDIANCUT, kmeans=4, dither=Image.Dither.NONE)
    pal = q.getpalette()[:9]
    idx = np.array(q, dtype=np.uint8)
    order = sorted(range(3), key=lambda i: sum(pal[3 * i:3 * i + 3]))
    remap = {o: k for k, o in enumerate(order)}
    idx = np.vectorize(lambda v: remap[v] + 1)(idx).astype(np.uint8)
    cols = [tuple(pal[3 * o:3 * o + 3]) for o in order]
    packed = bytearray()
    for row in idx:
        for x in range(0, TEX_W, 4):
            packed.append((row[x] << 6) | (row[x + 1] << 4) | (row[x + 2] << 2) | row[x + 3])
    return bytes(packed), [rgb12(c) for c in cols]


# ---------------------------------------------------------------- rooms
def read_theme(name):
    th = {}
    for line in open(os.path.join(ORIG, "themes", name + ".theme")):
        line = line.split("#")[0].strip()
        if "=" in line:
            k, v = (s.strip() for s in line.split("=", 1))
            th[k] = v
    return th


def proj(x, y, z):
    return CX + FOCAL * x / -z, CY - FOCAL * y / -z


# room ops: (kind, ...) ('P': a row pattern stretched down: the back wall's vertical
# grid lines) with pens 0 bg, 1 back, 2 floor, 3 grid, 4 shadow, 5 text,
# 6 side walls, 7 ceiling (remapped to 0 or 1 in main: the ball needs pens 6-15)
# 'B' box x y w h pen;  'T' trapezoid y h xtop wtop xbot wbot pen (rows y..y+h-1)
def room_ops(th):
    ops = []
    face = {}
    for k in ("back", "floor", "left", "right", "ceiling"):
        f = th.get(k, "off").split()
        face[k] = None if f[0] == "off" else f
    xl = -BOX_X if face["left"] else -20.0
    xr = BOX_X if face["right"] else 20.0
    yt = BOX_Y if face["ceiling"] else 20.0
    W, H = 160, 102
    if not (face["back"] and face["floor"]):      # else the faces cover it all
        ops.append(("B", 0, HUD_TOP, W, HUD_BOT - HUD_TOP, 0))
    # the back wall's screen rectangle
    bx0, by0 = proj(xl, yt, ROOM_Z)
    bx1, by1 = proj(xr, ROOM_Y, ROOM_Z)
    bx0, bx1 = max(0, bx0), min(W, bx1)
    by0 = max(HUD_TOP, by0)
    # side walls (behind everything: the ceiling and floor trapezoids cover their corners)
    if face["left"]:
        ops.append(("B", 0, HUD_TOP, int(round(bx0)), HUD_BOT - HUD_TOP, 6))
    if face["right"]:
        x = int(round(bx1))
        ops.append(("B", x, HUD_TOP, W - x, HUD_BOT - HUD_TOP, 6))
    if face["ceiling"]:
        # from the screen's top (z where y = yt meets the top row) to the back wall's top edge
        ytop = HUD_TOP
        z_top = -FOCAL * yt / (CY - ytop)
        xa, _ = proj(xl, yt, z_top)
        xb, _ = proj(xr, yt, z_top)
        ops.append(("T", ytop, int(round(by0)) - ytop, xa, xb - xa, bx0, bx1 - bx0, 7))
    if face["floor"]:
        yb = HUD_BOT
        z_bot = -FOCAL * -ROOM_Y / (yb - CY)
        xa, _ = proj(xl, ROOM_Y, z_bot)
        xb, _ = proj(xr, ROOM_Y, z_bot)
        y0 = int(round(by1))
        floor_ops = [("T", y0, yb - y0, bx0, bx1 - bx0, xa, xb - xa, 2)]
    else:
        floor_ops = []
    if face["back"]:
        ops.append(("B", int(round(bx0)), int(round(by0)), int(round(bx1 - bx0)), int(round(by1 - by0)), 1))
    grid_ops, floor_grid = [], []
    if face["back"] and face["back"][1] != "none":
        g = float(face["back"][2])
        k = math.ceil(xl / g)
        xs = []
        while k * g <= xr:
            x, _ = proj(k * g, 0, ROOM_Z)
            if bx0 <= x < bx1 - 0.5:
                xs.append(int(x))
            k += 1
        if xs:
            # all the vertical lines as one sprite: a 1-bpp row with a pixel per
            # line, stretched down the wall by Suzy (one SCB, not one each)
            grid_ops.append(("P", min(xs), int(round(by0)), int(round(by1 - by0)), [x - min(xs) for x in xs], 3))
        k = 0
        while ROOM_Y + k * g <= yt:
            _, y = proj(0, ROOM_Y + k * g, ROOM_Z)
            if by0 <= y <= by1:
                grid_ops.append(("B", int(round(bx0)), int(min(y, by1 - 1)), int(round(bx1 - bx0)), 1, 3))
            k += 1
    if face["floor"] and face["floor"][1] != "none":
        g = float(face["floor"][2])
        yb = HUD_BOT
        z_bot = -FOCAL * -ROOM_Y / (yb - CY)
        z = ROOM_Z
        while z <= z_bot + 1e-6:
            xa, y = proj(xl, ROOM_Y, z)
            xb, _ = proj(xr, ROOM_Y, z)
            xa, xb = max(0, xa), min(W, xb)
            if y < yb:
                floor_grid.append(("B", int(round(xa)), int(y), int(round(xb - xa)), 1, 3))
            z += g
        # the lines running away from you: tilted boxes
        k = math.ceil(xl / g)
        while k * g <= xr:
            xa, ya = proj(k * g, ROOM_Y, ROOM_Z)
            xb, yb2 = proj(k * g, ROOM_Y, z_bot)
            y0 = int(round(ya))
            hh = yb - y0
            if hh > 0 and -40 < xb < 200:
                slope = (xb - xa) / max(1e-6, (yb2 - ya))
                w = max(1, int(math.ceil(abs(slope))))
                xs = xa - (w - 1) / 2.0
                floor_grid.append(("T", y0, hh, xs, w, xs + slope * hh, w, 3))
            k += 1
    if face["ceiling"] and face["ceiling"][1] != "none":
        g = float(face["ceiling"][2])
        z = ROOM_Z
        ytop = HUD_TOP
        z_top = -FOCAL * yt / (CY - ytop)
        while z <= z_top + 1e-6:
            xa, y = proj(xl, yt, z)
            xb, _ = proj(xr, yt, z)
            if y >= ytop:
                grid_ops.append(("B", int(round(xa)), int(y), int(round(xb - xa)), 1, 3))
            z += g
    if face["left"] and face["left"][1] != "none":
        g = float(face["left"][2])
        for x in (bx0,):
            k = 0
            while ROOM_Y + k * g <= yt:
                yy = ROOM_Y + k * g
                # a line on the side wall from the back edge out to the screen edge
                _, ya = proj(xl, yy, ROOM_Z)
                z_e = -FOCAL * -xl / (CX - 0)
                _, yb3 = proj(xl, yy, z_e)
                if HUD_TOP <= ya <= HUD_BOT:
                    grid_ops.append(("L", 0, yb3, bx0, ya, 3))
                    grid_ops.append(("L", W - 1, yb3, bx1, ya, 3))
                k += 1
    return ops, grid_ops, floor_ops, floor_grid


def room_palette(th):
    bg = hexrgb(th["background"])
    back = hexrgb(th["back"].split()[0]) if th.get("back", "off") != "off" else bg
    fl = th.get("floor", "off").split()
    floor = hexrgb(fl[0]) if fl[0] != "off" else bg
    gridc = None
    for k in ("back", "floor"):
        f = th.get(k, "off").split()
        if f[0] != "off" and f[1] != "none":
            gridc = hexrgb(f[1])
            break
    sh = th.get("shadow", "floor 50").split()
    wall = sh[0] == "wall"
    pct = int(sh[1])
    shadow = dim(back if wall else floor, (100 - pct) / 100.0)
    side = th.get("left", "off").split()
    sidec = hexrgb(side[0]) if side[0] != "off" else bg
    ceil = th.get("ceiling", "off").split()
    ceilc = hexrgb(ceil[0]) if ceil[0] != "off" else bg
    if gridc is None:
        gridc = back
    return [rgb12(bg), rgb12(back), rgb12(floor), rgb12(gridc), rgb12(shadow), rgb12(sidec), rgb12(ceilc)], wall


# ---------------------------------------------------------------- small sprites
FONT5x7 = {  # a 5x7 font: 5 columns, bit 0 = the top row
    "B": [0x7F, 0x49, 0x49, 0x49, 0x36], "G": [0x3E, 0x41, 0x49, 0x49, 0x7A],
    "I": [0x00, 0x41, 0x7F, 0x41, 0x00], "N": [0x7F, 0x04, 0x08, 0x10, 0x7F],
    "O": [0x3E, 0x41, 0x41, 0x41, 0x3E], "S": [0x46, 0x49, 0x49, 0x49, 0x31],
    "T": [0x01, 0x01, 0x7F, 0x01, 0x01], "W": [0x3F, 0x40, 0x38, 0x40, 0x3F], " ": [0, 0, 0],
}


def literal(img, bpp):
    """rows of pens -> literal Suzy data (length byte, pixels, pad byte; 0 at the end)"""
    out = bytearray()
    per = 8 // bpp
    for row in img:
        row = list(row) + [0] * ((-len(row)) % per)
        bts = bytearray()
        for i in range(0, len(row), per):
            v = 0
            for k in range(per):
                v = (v << bpp) | row[i + k]
            bts.append(v)
        out.append(len(bts) + 2)
        out += bts
        out.append(0)
    out.append(0)
    return bytes(out)


def logo():
    """BOING TWIST in the 5x7 font, pen 1 the letters, pen 2 a drop shadow
    (2 bpp); Suzy zooms it in on the title"""
    cols = []
    for ch in "BOING TWIST":
        cols += FONT5x7[ch] + [0]
    W, H = len(cols) + 1, 8
    img = [[0] * W for _ in range(H)]
    for x, c in enumerate(cols):
        for y in range(7):
            if c >> y & 1:
                img[y + 1][x + 1] = max(img[y + 1][x + 1], 2)
    for x, c in enumerate(cols):
        for y in range(7):
            if c >> y & 1:
                img[y][x] = 1
    return W, H, literal(img, 2)


def disc(n):
    img = [[1 if (x + 0.5 - n / 2) ** 2 + (y + 0.5 - n / 2) ** 2 <= (n / 2) ** 2 else 0 for x in range(n)] for y in range(n)]
    return literal(img, 1)



def s16(v):
    v = int(round(v * 256)) & 0xFFFF
    return [v & 255, v >> 8]


def clip_trap(y, h, xt, wt, xb, wb, W=160):
    """A trapezoid (rows y..y+h-1, left edge xt -> xb, width wt -> wb) clipped
    to the screen's width: Suzy's sizes are 8.8 (at most 255 pixels), so a
    floor running out past the screen's sides is cut into trapezoids whose
    edges stay on screen (an edge that leaves the screen is clamped: up to
    three pieces)"""
    def edges(t):
        l = xt + (xb - xt) * t
        r = l + wt + (wb - wt) * t
        return l, r
    # rows where an edge crosses 0 or W
    cuts = {0.0, 1.0}
    for f in (lambda t: edges(t)[0], lambda t: edges(t)[1]):
        a, b = f(0.0), f(1.0)
        for lim in (0, W):
            if (a - lim) * (b - lim) < 0:
                cuts.add((lim - a) / (b - a))
    ts = sorted(cuts)
    out = []
    for t0, t1 in zip(ts, ts[1:]):
        r0, r1 = int(round(y + t0 * h)), int(round(y + t1 * h))
        if r1 <= r0:
            continue
        a0, a1 = (r0 - y) / h, (r1 - y) / h
        l0, rr0 = edges(a0)
        l1, rr1 = edges(a1)
        l0, l1 = min(max(l0, 0), W), min(max(l1, 0), W)
        rr0, rr1 = min(max(rr0, 0), W), min(max(rr1, 0), W)
        if rr0 - l0 <= 0 and rr1 - l1 <= 0:
            continue
        out.append((r0, r1 - r0, l0, max(rr0 - l0, 0), l1, max(rr1 - l1, 0)))
    return out


def clip_line(x1, y1, x2, y2, xa, ya, xb, yb):
    """Liang-Barsky: the part of the segment inside the rectangle, or None"""
    t0, t1, dx, dy = 0.0, 1.0, x2 - x1, y2 - y1
    for p, q in ((-dx, x1 - xa), (dx, xb - x1), (-dy, y1 - ya), (dy, yb - y1)):
        if p == 0:
            if q < 0:
                return None
            continue
        t = q / p
        if p < 0:
            t0 = max(t0, t)
        else:
            t1 = min(t1, t)
    if t0 > t1:
        return None
    return (x1 + t0 * dx, y1 + t0 * dy, x1 + t1 * dx, y1 + t1 * dy)


# ---------------------------------------------------------------- the cartridge
def cart_files(npics, nrooms, flip_size):
    """src/directory.s, src/cart.s and boing_twist.cfg: the program, then the
    pictures (into pic_buf), the flip's tables (into the second screen
    buffer, $E018), the rooms' shapes (into room_buf), the title ball's
    sub-sprites (into texl)"""
    files = [(f"PIC{i}", f"cart/pic_{i:02d}.bin", "_pic_buf", 512) for i in range(1, npics + 1)]
    files.append(("FLIP", "cart/flip.bin", "$E018", flip_size))
    files += [(f"ROOM{i}", f"cart/room_{i:02d}.bin", "_room_buf",
               os.path.getsize(os.path.join(PROJ, "cart", f"room_{i:02d}.bin"))) for i in range(nrooms)]
    files.append(("TITLE", "cart/title.bin", "_texl", os.path.getsize(os.path.join(PROJ, "cart", "title.bin"))))
    d = ["; made by tools/gen_data.py - do not edit",
         "; The cartridge's directory: entry 0 the program (as cc65's defdir.s), then",
         "; the data files (src/cart.s), each read by lynx_load(n) to its address.",
         '        .include "lynx.inc"',
         "        .import         __STARTOFDIRECTORY__",
         "        .import         __MAIN_START__",
         "        .import         __STARTUP_LOAD__, __BSS_LOAD__",
         "        .import         __BANK0BLOCKSIZE__",
         "        .import         _pic_buf, _room_buf, _texl",
         "        .export         __DEFDIR__: absolute = 1", "",
         '        .segment "DIRECTORY"', "",
         "__DIRECTORY_START__:",
         "off0 = __STARTOFDIRECTORY__ + (__DIRECTORY_END__ - __DIRECTORY_START__)",
         "len0 = __BSS_LOAD__ - __STARTUP_LOAD__",
         "        .byte   <(off0 / __BANK0BLOCKSIZE__)",
         "        .word   off0 & (__BANK0BLOCKSIZE__ - 1)",
         "        .byte   $88",
         "        .word   __MAIN_START__",
         "        .word   len0"]
    off = 0
    for seg, path, dest, size in files:
        d += [f"; {path}",
              f"        .byte   <((off0 + len0 + {off}) / __BANK0BLOCKSIZE__)",
              f"        .word   (off0 + len0 + {off}) & (__BANK0BLOCKSIZE__ - 1)",
              "        .byte   $88",
              f"        .word   {dest}",
              f"        .word   {size}"]
        off += size
    d += ["__DIRECTORY_END__:", ""]
    with open(os.path.join(PROJ, "src", "directory.s"), "w") as f:
        f.write("\n".join(d))
    c = ["; made by tools/gen_data.py - do not edit", "; the cartridge's data files (src/directory.s)"]
    for seg, path, dest, size in files:
        c += [f'.segment "{seg}"', f'        .incbin "{path}"']
    with open(os.path.join(PROJ, "src", "cart.s"), "w") as f:
        f.write("\n".join(c) + "\n")
    cfg = open(os.path.join(PROJ, "..", "..", "sdk", "cfg", "lynx.cfg")).read()
    cfg = cfg.replace("# lynxdev memory map", "# made by games/boing_twist/tools/gen_data.py - do not edit\n"
                      "# Boing Twist: the SDK's lynx.cfg plus the cartridge's data files\n"
                      "# (src/directory.s, src/cart.s).\n#\n# lynxdev memory map")
    cfg = cfg.replace("DIR:    file = %O,               start = $0000, size = 8;",
                      f"DIR:    file = %O,               start = $0000, size = {8 * (len(files) + 1)};")
    mem = "".join(f"    F{seg}: file = %O, start = $0000, size = ${size:04X};\n" for seg, path, dest, size in files)
    cfg = cfg.replace("    MAIN:   file = %O, define = yes, start = $0200, size = $BE38 - __STACKSIZE__;\n",
                      "    MAIN:   file = %O, define = yes, start = $0200, size = $BE38 - __STACKSIZE__;\n" + mem)
    seg = "".join(f"    {sg}: load = F{sg}, type = ro;\n" for sg, path, dest, size in files)
    cfg = cfg.replace("    BSS:       load = MAIN,   type = bss, define = yes;\n",
                      "    BSS:       load = MAIN,   type = bss, define = yes;\n" + seg)
    cfg = cfg.replace("__STACKSIZE__:        type = weak, value = $0800; # 2k stack",
                      "__STACKSIZE__:        type = weak, value = $0400; # 1k stack (static locals: little is used)")
    with open(os.path.join(PROJ, "boing_twist.cfg"), "w") as f:
        f.write(cfg)
    print(f"  cartridge: {len(files)} data files, {off} bytes")


# ---------------------------------------------------------------- main
def main():
    pv = PlayView()
    spr, vrows, even, odd, nruns = pv.tables()
    pbuild, wrows = pv.flip_tables()
    out = ["; made by tools/gen_data.py - do not edit",
           "; the play ball's sprite and the tables view_play (ballasm.s) draws it with",
           "\t.export _ball_spr, _vw_rows, _vw_even, _vw_odd", "",
           '.segment "DATA"',
           f"; {pv.w} x {pv.h}, literal 4 bpp: per line its length, the pixels, a pad byte",
           "_ball_spr:"]
    for i in range(0, len(spr), 24):
        out.append("\t.byte\t" + ",".join(str(b) for b in spr[i:i + 24]))
    out += ["", '.segment "RODATA"',
            "; per line: the first byte's address, the pairs' offsets (even, odd), the pair count,",
            "; masks (1: first byte's left pixel outside, 2: last byte's right pixel outside), the",
            "; run count, then each run: its end (pair index), window row v0, light step",
            "_vw_rows:"]
    for out_off, so, npairs, masks, runs in vrows:
        out.append(f"\t.word\t_ball_spr+{out_off}, _vw_even+{so}, _vw_odd+{so}")
        out.append(f"\t.byte\t{npairs}, {masks}, {len(runs)}, " + ", ".join(f"{e}, {v0}, {sh}" for e, v0, sh in runs))
    out.append("\t.word\t0")
    for name, lst in (("_vw_even", even), ("_vw_odd", odd)):
        out.append(name + ":")
        for i in range(0, len(lst), 24):
            out.append("\t.byte\t" + ",".join(str(b) for b in lst[i:i + 24]))
    with open(os.path.join(PROJ, "src", "view.s"), "w") as f:
        f.write("\n".join(out) + "\n")
    # the flip's tables: a cartridge file loaded into the second screen buffer for play
    fl = bytearray()
    for o in pbuild:
        fl += bytes((o & 255, o >> 8))
    rows_at = len(fl)
    for v, u0, idx in wrows:
        o = v * ROW + u0
        fl += bytes((o & 255, o >> 8, len(idx)))
        fl += bytes(r for r, a in idx) + bytes(a for r, a in idx)
    fl += b"\xff\xff"
    os.makedirs(os.path.join(PROJ, "cart"), exist_ok=True)
    with open(os.path.join(PROJ, "cart", "flip.bin"), "wb") as f:
        f.write(fl)
    print(f"  view tables {len(even) * 2 + sum(9 + 3 * len(r[4]) for r in vrows)} bytes ({len(even)} pairs, "
          f"{nruns} runs), sprite {len(spr)} bytes, flip file {len(fl)} bytes (rows at {rows_at})")
    flip_size, flip_rows_at = len(fl), rows_at

    # ---- pictures
    pics = read_titles()
    os.makedirs(os.path.join(PROJ, "cart"), exist_ok=True)
    recs = []
    for i, (num, name, title, level, room) in enumerate(pics):
        data, cols = picture(os.path.join(ORIG, "images", name + ".jpg"))
        with open(os.path.join(PROJ, "cart", f"pic_{i + 1:02d}.bin"), "wb") as f:
            f.write(data)
        ri = ROOM_ORDER.index(room) if room in ROOM_ORDER else 0
        recs.append((title, level, ri, cols))

    # ---- rooms
    rooms = []
    room_sizes = []
    for name in ROOM_ORDER:
        th = read_theme(name)
        ops, gops, fops, fgrid = room_ops(th)
        pal, wall = room_palette(th)
        stars = []
        if name == "space":
            rng = np.random.default_rng(1984)
            bx0, by0 = proj(-20, 20, ROOM_Z)
            _, by1 = proj(0, ROOM_Y, ROOM_Z)
            for _ in range(40):
                stars.append(("B", int(rng.integers(0, 160)), int(rng.integers(HUD_TOP, int(by1))), 1, 1, 5))
        # side walls and ceiling share the nearer of the background's and the back wall's pens
        def near(c, a, b):
            d = lambda x, y: sum((((x >> k) & 15) - ((y >> k) & 15)) ** 2 for k in (0, 4, 8))
            return 0 if d(c, a) <= d(c, b) else 1
        remap = {6: near(pal[5], pal[0], pal[1]), 7: near(pal[6], pal[0], pal[1])}
        allops = ops + stars + gops + [("S", 0)] + fops + fgrid + [("S", 1)]
        allops = [op[:-1] + (remap.get(op[-1], op[-1]),) if op[0] in "BTLP" else op for op in allops]
        rooms.append((th["name"].upper(), allops, pal, wall))

    # ---- title ball
    subs, tb_box = title_ball()

    lw, lh, ldata = logo()
    SH_N = 32
    hdr = ["/* made by tools/gen_data.py - do not edit */", "#ifndef GFX_DATA_H", "#define GFX_DATA_H",
           "#include <stdint.h>", "",
           f"#define TEX_W {TEX_W}", f"#define TEX_H {TEX_H}", f"#define TEX_ROW {ROW}",
           f"#define NR {NR}", f"#define NA {NA}",
           f"#define FLIP_FILE_SIZE {flip_size}", f"#define FLIP_ROWS_AT {flip_rows_at}",
           f"#define BALL_X0 {pv.x0}   /* the ball sprite's top-left, from the ball's middle */",
           f"#define BALL_Y0 {pv.y0}", f"#define BALL_W {pv.w}", f"#define BALL_H {pv.h}",
           f"#define BALL_R {pv.R}",
           f"#define SCR_CX {CX}", f"#define SCR_CY {CY}",
           f"#define HUD_TOP {HUD_TOP}", f"#define HUD_BOT {HUD_BOT}",
           f"#define FOCAL_X16 {int(round(FOCAL * 16))}   /* pixels per unit at distance 1, x16 */",
           f"#define FLOOR_Y {int(round(proj(0, ROOM_Y, ROOM_Z)[1]))}   /* the back wall meets the floor */",
           "",
           "typedef struct { const char *title; uint8_t level, room; uint16_t col[3]; } Pic;",
           f"#define NPICS {len(recs) + 1}   /* 0: the Boing ball, made by the game */",
           "extern const Pic pics[NPICS];",
           "typedef struct { const char *name; uint16_t pal[7]; uint8_t wall; } Room;",
           "/* each room's shapes are a cartridge file (ROOM_FILE + n), read into room_buf */",
           "#define ROOM_BUF_SIZE @ROOMBUF@",
           "#define FLIP_FILE NPICS", "#define ROOM_FILE (NPICS + 1)", "#define TITLE_FILE (NPICS + 1 + NROOMS)",
           "#define TITLE_SIZE @TITLESIZE@",
           f"#define NROOMS {len(rooms)}",
           "extern const Room rooms[NROOMS];",
           "/* the bands' rows (from the ball sprite's top) down its middle, per grid (4, 6, 8 bands) */",
           "extern const uint8_t band_top[3][8], band_bot[3][8];",
           "/* the title's Boing ball: sub-sprites (palette-cycled, the light in zones) */",
           f"#define TB_N {len(subs)}", f"#define TB_LEVELS {TITLE_LEVELS}", f"#define TB_PHASES {TITLE_PHASES}",
           f"#define TB_X0 {tb_box[0]}", f"#define TB_Y0 {tb_box[1]}", f"#define TB_W {tb_box[2]}", f"#define TB_H {tb_box[3]}",
           "/* the sub-sprites' data is a cartridge file (TITLE_FILE), read into the texture's place */",
           "typedef struct { int8_t dx, dy; uint8_t level; uint16_t off; } TbSub;",
           "extern const TbSub tb_subs[TB_N];   /* sorted by level */",
           "extern const uint16_t tb_red[TB_LEVELS], tb_white[TB_LEVELS];",
           "/* the title's logo (2 bpp) and the shadow's disc (1 bpp, SHADOW_N square) */",
           f"#define LOGO_W {lw}", f"#define LOGO_H {lh}", "extern const uint8_t logo_data[];",
           f"#define SHADOW_N {SH_N}", "extern const uint8_t shadow_data[];",
           "/* shadows: on the wall, (x + 0.85, y - 0.15) on the back wall, radius x16; on the floor,",
           "   the half-axes x16 of the ellipse for a ball 4.4 away and its middle's row */",
           f"#define WALL_SH_DX16 {int(round(FOCAL * 0.85 / -ROOM_Z * 16))}",
           f"#define WALL_SH_DY16 {int(round(FOCAL * 0.15 / -ROOM_Z * 16))}",
           f"#define WALL_SH_R16 {int(round(FOCAL * 1.15 / -ROOM_Z * 16))}",
           f"#define FLOOR_SH_RX16 {int(round(FOCAL / TITLE_DIST * 16))}",
           f"#define FLOOR_SH_RY16 {int(round((proj(0, ROOM_Y, -TITLE_DIST + 0.6)[1] - proj(0, ROOM_Y, -TITLE_DIST - 1.0)[1]) * 8))}",
           f"#define FLOOR_SH_Y {int(round((proj(0, ROOM_Y, -TITLE_DIST + 0.6)[1] + proj(0, ROOM_Y, -TITLE_DIST - 1.0)[1]) / 2))}",
           f"#define BALL_R_TITLE {int(round(FOCAL * math.tan(math.asin(1 / TITLE_DIST))))}",
           "", "#endif", ""]
    src = ["/* made by tools/gen_data.py - do not edit */", '#include "gfx_data.h"', ""]
    src.append("const Pic pics[NPICS] = {")
    src.append('    { "THE BOING BALL", 0, 0, { 0x000, 0xF22, 0xFFF } },')
    for title, level, ri, cols in recs:
        src.append(f'    {{ "{title}", {level}, {ri}, {{ 0x{cols[0]:03X}, 0x{cols[1]:03X}, 0x{cols[2]:03X} }} }},')
    src.append("};")
    # room ops: B x y w h pen | T y h xt(8.8) wt(8.8) dx(8.8 per row, signed) dw(8.8 signed) pen | 0 end
    allops_extra = []
    for i, (name, ops, pal, wall) in enumerate(rooms):
        b = []
        for op in ops:
            if op[0] == "B":
                _, x, y, w, h, pen = op
                x0, y0 = max(0, x), max(HUD_TOP, y)
                x1, y1 = min(160, x + w), min(HUD_BOT, y + h)
                if x1 <= x0 or y1 <= y0:
                    continue
                b += [1, x0, y0, x1 - x0, y1 - y0, pen]
            elif op[0] == "T":
                _, y, h, xt, wt, xb, wb, pen = op
                if h <= 0:
                    continue
                pieces = clip_trap(y, h, xt, wt, xb, wb)
                if len(pieces) != 1 or pieces[0] != (y, h, xt, wt, xb, wb):
                    for q in pieces:
                        allops_extra.append(q)
                    for (y, h, xt, wt, xb, wb) in pieces:
                        dx = (xb - xt) / h
                        dw = (wb - wt) / h
                        b += [2, y, h] + s16(xt) + s16(wt) + s16(dx) + s16(dw) + [pen]
                    continue
                dx = (xb - xt) / h
                dw = (wb - wt) / h
                b += [2, y, h] + s16(xt) + s16(wt) + s16(dx) + s16(dw) + [pen]
            elif op[0] == "S":
                b += [4 + op[1]]
            elif op[0] == "P":
                _, x0, y, h, xs, pen = op
                row = bytearray((max(xs) // 8) + 1)
                for x in xs:
                    row[x // 8] |= 0x80 >> (x % 8)
                data = [len(row) + 2] + list(row) + [0, 0]
                b += [6, x0, y, h, pen, len(data)] + data
            elif op[0] == "L":
                _, x1, y1, x2, y2, pen = op
                seg = clip_line(x1, y1, x2, y2, 0, HUD_TOP, 159, HUD_BOT - 1)
                if seg:
                    b += [3] + [int(round(v)) for v in seg] + [pen]
        b.append(0)
        with open(os.path.join(PROJ, "cart", f"room_{i:02d}.bin"), "wb") as f:
            bad = [(j, v) for j, v in enumerate(b) if not 0 <= v < 256]
            assert not bad, (i, bad[:5], b[max(0, bad[0][0] - 6):bad[0][0] + 6])
            f.write(bytes(b))
        room_sizes.append(len(b))
    src.append("const Room rooms[NROOMS] = {")
    for i, (name, ops, pal, wall) in enumerate(rooms):
        src.append(f'    {{ "{name}", {{ ' + ", ".join(f"0x{c:03X}" for c in pal) + f" }}, {1 if wall else 0} }},")
    src.append("};")
    bt, bb = [], []
    for nb in (4, 6, 8):
        br = pv.band_rows(nb) + [(0, 0)] * (8 - nb)
        bt.append(", ".join(str(a) for a, _ in br))
        bb.append(", ".join(str(b) for _, b in br))
    src.append("const uint8_t band_top[3][8] = { {" + "}, {".join(bt) + "} };")
    src.append("const uint8_t band_bot[3][8] = { {" + "}, {".join(bb) + "} };")
    subs.sort(key=lambda s: s[2])
    tbin = bytearray()
    src.append("const TbSub tb_subs[TB_N] = {")
    for i, s in enumerate(subs):
        src.append(f"    {{ {s[0]}, {s[1]}, {s[2]}, {len(tbin)} }},")
        tbin += s[5]
    src.append("};")
    with open(os.path.join(PROJ, "cart", "title.bin"), "wb") as f:
        f.write(tbin)
    reds = [rgb12(dim((0xFF, 0x22, 0x22), 0.52 + 0.48 * k / (TITLE_LEVELS - 2))) for k in range(TITLE_LEVELS - 1)]
    whites = [rgb12(dim((0xFF, 0xFF, 0xFF), 0.56 + 0.44 * k / (TITLE_LEVELS - 2))) for k in range(TITLE_LEVELS - 1)]
    reds.append(rgb12((0xFF, 0x88, 0x88)))
    whites.append(0xFFF)
    src.append("const uint16_t tb_red[TB_LEVELS] = { " + ", ".join(f"0x{c:03X}" for c in reds) + " };")
    src.append("const uint16_t tb_white[TB_LEVELS] = { " + ", ".join(f"0x{c:03X}" for c in whites) + " };")
    src.append("const uint8_t logo_data[] = { " + ",".join(str(v) for v in ldata) + " };")
    src.append("const uint8_t shadow_data[] = { " + ",".join(str(v) for v in disc(SH_N)) + " };")
    with open(os.path.join(PROJ, "src", "gfx_data.h"), "w") as f:
        f.write("\n".join(hdr).replace("@ROOMBUF@", str(max(room_sizes))).replace("@TITLESIZE@", str(len(tbin))))
    cart_files(len(recs), len(rooms), flip_size)
    with open(os.path.join(PROJ, "src", "gfx_data.c"), "w") as f:
        f.write("\n".join(src) + "\n")
    print(f"  {len(recs)} pictures, {len(rooms)} rooms: ops " +
          ", ".join(str(len(r[1])) for r in rooms))


if __name__ == "__main__":
    main()
