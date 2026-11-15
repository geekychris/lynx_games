"""Orb Hunter's player for the test (test.py), through the game's probe
(src/main.c orb_probe) and the joypad (the Atari 8-bit port's bot, FIRE = A).

Its game: from the start room it runs right, down the gap into the
caverns, on to the Spring Boots in room 3,3; it shoots the creatures down
facing them, jumping when it's stuck. When it has scored enough it stops
fighting and walks into the creatures until they wear its energy down."""
import struct

FMT = "<BBBBhhhlBBBBBhhBBBB"
FIELDS = ("state", "room_x", "room_y", "alive", "hx", "hy", "health", "score", "enemies", "rolled",
          "facing", "on_ground", "invuln", "ex", "ey", "has_roll", "has_bombs", "has_missiles",
          "missiles")
PLAYING, DEAD, GAMEOVER, ITEM_GET = 1, 2, 3, 4


def probe(emu):
    return dict(zip(FIELDS, struct.unpack(FMT, emu.read_mem("orb_probe", struct.calcsize(FMT)))))


class Bot:
    def __init__(self, target=200):
        self.target = target
        self.t = 0
        self.last_x, self.stuck = None, 0
        self.phase = "fight"
        self.item = False

    def keys(self, p):
        """the joystick this frame ('RIGHT+UP+A' ...)"""
        self.t += 1
        if p["state"] == ITEM_GET:
            self.item = True
            return "A" if self.t % 8 < 4 else ""            # (the box closes on a press)
        if p["state"] != PLAYING:
            return ""
        if p["score"] >= self.target and self.item:
            self.phase = "give up"
        if p["hx"] == self.last_x:
            self.stuck += 1
        else:
            self.stuck = 0
        self.last_x = p["hx"]
        jump = self.stuck > 12 and self.t % 24 < 8
        if not self.item and (p["room_x"], p["room_y"]) == (3, 3) and not p["enemies"]:
            # the Spring Boots: tile 5,12 (an orb a tile above the floor), jump to it
            if p["hx"] > 88:
                return "LEFT"
            if p["hx"] < 76:
                return "RIGHT"
            return "UP" if self.t % 40 < 20 else ""
        if self.phase == "give up" and (p["room_x"], p["room_y"]) == (3, 3):
            # back left into 2,3: its creatures come back when a room is entered
            return "+".join(["LEFT"] + (["UP"] if jump else []))
        if p["enemies"] and self.phase == "give up":
            dx = p["ex"] - p["hx"]
            if abs(p["ey"] - p["hy"]) > 6 and abs(dx) < 20:          # off this ledge first
                go = "RIGHT" if self.t // 120 % 2 else "LEFT"
            else:
                go = "LEFT" if dx < 0 else "RIGHT"                   # into it
            return "+".join([go] + (["UP"] if jump else []))
        if p["enemies"] and (self.item or (p["room_x"], p["room_y"]) != (3, 3) or p["ex"] > p["hx"]):
            dx = p["ex"] - p["hx"]
            toward = "LEFT" if dx < 0 else "RIGHT"
            facing = "LEFT" if p["facing"] else "RIGHT"
            if abs(p["ey"] - p["hy"]) > 6:                          # get level with it:
                go = toward if abs(dx) > 20 else ("RIGHT" if self.t // 120 % 2 else "LEFT")
                return "+".join([go] + (["UP"] if jump else []))
            if abs(dx) > 110:                                        # in range (shots go 150)
                return "+".join([toward] + (["UP"] if jump else []))
            if facing != toward:
                return toward                                        # turn to it
            return "A" if self.t % 2 == 0 else ""                 # and shoot
        if (p["room_x"], p["room_y"]) == (3, 3) and not self.item:
            return "+".join(["LEFT" if p["hx"] > 88 else "RIGHT"] + (["UP"] if jump else []))
        return "+".join(["RIGHT"] + (["UP"] if jump else []))

def step(emu, bot, held):
    """one frame: the bot's keys held"""
    p = probe(emu)
    want = bot.keys(p)
    if want != held[0]:
        emu.release()
        if want:
            emu.hold(want)
        held[0] = want
    emu.run(1)
    return p
