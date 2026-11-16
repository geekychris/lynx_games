/*
 * Orb Hunter (Atari Lynx). Port of amiga_mcp/examples/orb_hunter (the
 * amiga_games repo), by way of the Atari 8-bit port: game.c is the
 * original's rules, world.c its rooms; render.c draws them, sound.c plays
 * them. This is the original's main loop (main.c): the title, play, the
 * item box, the death and GAME OVER, plus the Lynx's PAUSE, the 50 Hz
 * timestep, and the log lines and probe the test reads.
 *
 * Controls (the original's joystick): LEFT/RIGHT run, UP jump (and aim up:
 * UP + A fires a rocket once found), DOWN roll (once found; UP stands up),
 * A or B fire (rolled: a bomb). A starts, PAUSE pauses, PAUSE + OPTION 1
 * goes back to the title.
 *
 * Log: "oh: ready", "oh: title", "oh: start room=2,2", "oh: room X,Y",
 * "oh: item NAME", "oh: score=N", "oh: hit health=N", "oh: rolled=N",
 * "oh: door open", "oh: dead", "oh: gameover score=N", "oh: paused=N",
 * "oh: perf frames=100 vbl=N ticks=N full=N objs=N room=N" (full: rooms painted).
 */
#pragma static-locals (on)
#include <string.h>
#include <stdlib.h>
#include "lynxgame.h"
#include "game.h"
#include "render.h"
#include "sound.h"

/* for the test's bot (emu.read_mem("orb_probe", ...)): the a8 port's */
typedef struct {
    uint8_t state, room_x, room_y, alive;
    int16_t hx, hy, health;
    int32_t score;
    uint8_t enemies, rolled, facing, on_ground, invuln;
    int16_t ex, ey;
    uint8_t has_roll, has_bombs, has_missiles, missiles;
} probe_t;
probe_t orb_probe;

static uint8_t paused, blink_t;
static char score_str[20];

/* ------------------------------------------------------------ log */

#ifndef LYNX_NO_LOG
static void logu(const char *k, uint16_t v) { lynx_log_s(k); lynx_log_u(v); }
static void logl(const char *k, long v)
{
    char b[12];
    lynx_log_s(k);
    ltoa(v, b, 10);
    lynx_log_s(b);
}
#define LOGNL() lynx_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logl(k, v) ((void)0)
#define LOGNL() ((void)0)
#endif

static void probe(void)
{
#ifndef LYNX_NO_LOG
    static uint8_t i, n;
    n = 0;
    orb_probe.state = (uint8_t)gs.state;
    orb_probe.room_x = (uint8_t)gs.room_x;
    orb_probe.room_y = (uint8_t)gs.room_y;
    orb_probe.alive = h_alive;
    orb_probe.hx = FIX_INT(h_x);
    orb_probe.hy = FIX_INT(h_y);
    orb_probe.health = h_health;
    orb_probe.score = gs.score;
    orb_probe.rolled = h_rolled;
    orb_probe.facing = h_facing;
    orb_probe.on_ground = h_on_ground;
    orb_probe.invuln = h_invuln_timer;
    orb_probe.has_roll = h_has_roll_form;
    orb_probe.has_bombs = h_has_bombs;
    orb_probe.has_missiles = h_has_missiles;
    orb_probe.missiles = (uint8_t)h_missiles;
    for (i = 0; i < MAX_ENEMIES; ++i)
        if (e_on[i]) {
            if (!n) {
                orb_probe.ex = FIX_INT(e_x[i]);
                orb_probe.ey = FIX_INT(e_y[i]);
            }
            ++n;
        }
    orb_probe.enemies = n;
#endif
}

/* what changed in a step: log lines */
static int8_t last_rx, last_ry;
static int32_t last_score;
static WORD last_health;
static uint8_t last_rolled;

static void watch(WORD prev_state)
{
    if (gs.state != prev_state) {
        switch (gs.state) {
        case STATE_ITEM_GET:
            lynx_log_s("oh: item ");
            lynx_log_s(gs.item_name);
            LOGNL();
            break;
        case STATE_DEAD:
            lynx_log_s("oh: dead\n");
            break;
        case STATE_GAMEOVER:
            logl("oh: gameover score=", gs.score);
            LOGNL();
            break;
        }
    }
    if (gs.room_changed) {
        gs.room_changed = 0;
        render_room();
        if (gs.room_x != last_rx || gs.room_y != last_ry) {
            last_rx = gs.room_x;
            last_ry = gs.room_y;
            logu("oh: room ", gs.room_x);
            logu(",", gs.room_y);
            LOGNL();
        }
    }
    if (gs.score != last_score) {
        last_score = gs.score;
        logl("oh: score=", gs.score);
        LOGNL();
    }
    if (h_health < last_health) {
        logl("oh: hit health=", h_health);
        LOGNL();
    }
    last_health = h_health;
    if (h_rolled != last_rolled) {
        last_rolled = h_rolled;
        logu("oh: rolled=", h_rolled);
        LOGNL();
    }
}

static void to_title(void)
{
    gs.state = STATE_TITLE;
    blink_t = 0;
    render_screen_new();
    lynx_log_s("oh: title\n");
}

/* ------------------------------------------------------------ a 50 Hz step (the original's loop) */

static void tick(uint16_t held, uint16_t pressed)
{
    WORD prev = gs.state;
    switch (gs.state) {
    case STATE_TITLE:
        /* port: original bug: the original starts on fire *held*, so fire
           held through GAME OVER starts the next game at once; here a press */
        if (pressed & (IN_A | IN_B)) {
            game_init();
            last_rx = gs.room_x;
            last_ry = gs.room_y;
            last_score = 0;
            last_health = h_health;
            last_rolled = 0;
            lynx_log_s("oh: start room=2,2\n");
        }
        break;

    case STATE_PLAYING:
    case STATE_ROOM_TRANSITION:
    case STATE_DEAD:
        game_update((held & IN_LEFT) != 0, (held & IN_RIGHT) != 0, (held & IN_UP) != 0,
                    (held & IN_DOWN) != 0, (held & IN_UP) != 0, (held & (IN_A | IN_B)) != 0);
        break;

    case STATE_ITEM_GET:
        /* port: original bug (fixed in amiga_games #50): the box closed on
           fire held, so the shot that was firing as the hunter touched the
           orb closed it at once, and game.c's 90-frame timer was never used
           (the loop didn't call game_update here). Here, as in the fixed
           original, it closes on a press or after the 90 frames */
        game_update(0, 0, 0, 0, 0, 0);
        if (pressed & (IN_A | IN_B)) gs.state = STATE_PLAYING;
        if (gs.state == STATE_PLAYING) render_room();
        break;

    case STATE_GAMEOVER:
        /* a press (see the title), or game.c's 180-frame timer (the fixed
           original runs it; it never called game_update here) */
        game_update(0, 0, 0, 0, 0, 0);
        if (gs.state == STATE_TITLE || (pressed & (IN_A | IN_B))) to_title();
        break;
    }
    /* port: original bug (fixed in amiga_games #50): its loop counted
       gs.frame too, twice a loop with game_update's, so its drawing always
       saw it odd: the creatures were never drawn and the invulnerable hunter
       vanished. game_update's count alone now, as in the fixed original */
    watch(prev);
}

static void score_text(void)
{
    strcpy(score_str, "SCORE ");
    ltoa(gs.score, score_str + 6, 10);
}

/* frame rate: display frames per 100 drawn in play (100 at 50 fps) */
static uint8_t perf_n;
static uint16_t perf_vbl, last_vbl, perf_ticks;

int main(void)
{
    static uint16_t held, pressed, last_fc;
    static uint8_t ticks, t;
    static WORD prev_state;

    render_init();
    sound_init();
    gs.state = STATE_TITLE;
    lynx_log_s("oh: ready\n");
    to_title();
    last_fc = frame_count;

    for (;;) {
        held = input_update();
        pressed = input_new;

        if (paused) {               /* port: the Lynx's PAUSE (the original has none) */
            if (pressed & (IN_PAUSE | IN_OPT1)) {
                paused = 0;
                sound_pause(0);
                lynx_log_s("oh: paused=0\n");
                last_fc = frame_count;
                perf_n = 0;                         /* (the pause isn't play) */
                perf_ticks = 0;
                last_vbl = frame_count;
                if (pressed & IN_OPT1) to_title();    /* PAUSE + OPTION 1: the title */
                else render_room();
            } else {
                render_paused();
                render_present();
                continue;
            }
        } else if ((pressed & IN_PAUSE) && gs.state != STATE_TITLE && gs.state != STATE_GAMEOVER) {
            paused = 1;
            sound_pause(1);
            lynx_log_s("oh: paused=1\n");
            render_paused();
            render_present();
            continue;
        }

        /* port: a fixed timestep: the rules (and the tune) run once per
           50 Hz display frame, as the original's WaitTOF loop on a PAL Amiga;
           after a frame that took longer they catch up, at most 3 */
        ticks = (uint8_t)(frame_count - last_fc);
        last_fc = frame_count;
        if (ticks == 0) ticks = 1;
        if (ticks > 3) ticks = 3;
        prev_state = gs.state;
        for (t = 0; t < ticks; ++t) {
            tick(held, pressed);
            pressed = 0;            /* an edge counts once */
            sound_tick();
            ++perf_ticks;
            if (gs.state != prev_state) break;
        }
        probe();

        switch (gs.state) {
        case STATE_TITLE:
            if (++blink_t == 40) blink_t = 0;
            render_title(blink_t >= 20);              /* the original's (g_frame / 20) & 1 */
            break;
        case STATE_ITEM_GET:
            render_play();
            render_item_box(gs.item_name, ((uint8_t)gs.frame / 15) & 1);
            break;
        case STATE_GAMEOVER:
            if (prev_state != STATE_GAMEOVER) {
                score_text();
                blink_t = 0;
                render_screen_new();
            }
            if (++blink_t == 40) blink_t = 0;
            render_gameover(blink_t >= 20, score_str);
            break;
        default:
            render_play();
            if (gs.state == STATE_PLAYING) {
                if (++perf_n == 100) {
                    perf_vbl = frame_count - last_vbl;
                    last_vbl = frame_count;
                    logu("oh: perf frames=100 vbl=", perf_vbl);
                    logu(" ticks=", perf_ticks);
                    logu(" full=", rd_full);
                    logu(" objs=", rd_objs);
                    logu(" room=", gs.room_y * WORLD_W + gs.room_x);
                    LOGNL();
                    perf_n = 0;
                    perf_ticks = 0;
                    rd_full = 0;
                }
            }
            break;
        }
        if (gs.state == STATE_PLAYING && prev_state != STATE_PLAYING) {
            perf_n = 0;
            perf_ticks = 0;
            rd_full = 0;
            last_vbl = frame_count;
        }
        render_present();
    }
}
