/*
 * Bullion Dash (Atari Lynx). Port of amiga_mcp/examples/bullion_dash (the
 * repaired levels and the fixed guards), by way of the Atari 8-bit port:
 * player.c, enemy.c, level.c and play.c are the original's rules; game_tick()
 * below is its main loop's states. This file is the Lynx front end: title,
 * status line, message boxes, pause, the high score, Mikey music and effects,
 * the 50 Hz loop and the log lines the test reads.
 */
#pragma static-locals (on)
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "level.h"
#include "play.h"
#include "render.h"
#include "sound.h"

GameState gs;
uint8_t sfx_pending;

/* port: the original has a bridge hook, goto_level, that starts any level;
   here the test writes the first level into this before pressing A (the
   title itself starts at level 1, as the original's) */
uint16_t start_level = 1;
/* port: a high score, kept while the Lynx is on (the original has none) */
uint32_t hiscore = 2000;

static int16_t transition_frames;
static uint8_t paused, blink_t, new_hi;


/* ------------------------------------------------------------ log */

#ifndef LYNX_NO_LOG
static void logu(const char *k, uint16_t v) { lynx_log_s(k); lynx_log_u(v); }
static void logscore(uint32_t v)
{
    char *s = render_num(v, 7);
    while (*s == '0' && s[1]) ++s;
    lynx_log_s(s);
}
#define LOGNL() lynx_log_c('\n')
#else
#define logu(k, v) ((void)0)
#define logscore(v) ((void)0)
#define LOGNL() ((void)0)
#endif

#ifndef LYNX_NO_LOG
static const char *const state_names[5] = { "title", "playing", "dying", "level_done", "gameover" };
#endif

/* what changed since the last frame: a log line, and the status line */
static void watch_state(void)
{
    static uint8_t last_state = 255;
    static int16_t last_lives = -1, last_level = -1, last_gold = -1, last_total = -1;
    static uint32_t last_score = 0xFFFFFFFFUL;
    uint8_t changed = 0;
    if (gs.state != last_state) {
        lynx_log_s("bd: state=");
        lynx_log_s(state_names[gs.state]);
        logu(" level=", gs.level_num);
        logu(" lives=", gs.lives);
        lynx_log_s(" score=");
        logscore(gs.score);
        LOGNL();
        last_state = gs.state;
    }
    if (gs.score != last_score) {
        lynx_log_s("bd: score=");
        logscore(gs.score);
        LOGNL();
        last_score = gs.score;
        changed = 1;
    }
    if (gs.lives != last_lives) { logu("bd: lives=", gs.lives); LOGNL(); last_lives = gs.lives; changed = 1; }
    if (gs.level_num != last_level) { logu("bd: level=", gs.level_num); LOGNL(); last_level = gs.level_num; changed = 1; }
    if (gs.gold_collected != last_gold || gs.gold_total != last_total) {
        logu("bd: gold=", gs.gold_collected);
        logu("/", gs.gold_total);
        LOGNL();
        last_gold = gs.gold_collected;
        last_total = gs.gold_total;
        changed = 1;
    }
    if (changed) render_status_changed();
}

/* ------------------------------------------------------------ the game (the original's main loop) */

static void start_level_play(void)
{
    level_load(gs.level_num);
    gs.state = STATE_PLAYING;
    render_room();
}

static void die(const char *why)
{
    (void)why;                  /* (release builds: no log) */
    lynx_log_s("bd: death ");
    lynx_log_s(why);
    logu(" at ", gs.player.gx);
    logu(",", gs.player.gy);
    LOGNL();
    gs.state = STATE_DYING;
    gs.player.state = PS_DEAD;
    transition_frames = 30;
    sound_sfx(SFX_DEATH);
}

static void to_title(void)
{
    gs.state = STATE_TITLE;
    blink_t = 0;
    render_title_new();
    lynx_log_s("bd: title\n");
}

/* one 50 Hz frame of the original's loop */
static void game_tick(uint16_t held, uint16_t pressed)
{
    switch (gs.state) {
    case STATE_TITLE:
        if (pressed & (IN_A | IN_B)) {
            gs.score = 0;
            gs.lives = 5;
            gs.level_num = start_level;
            new_hi = 0;
            rnd_seed(frame_count | 1);
            start_level_play();
            logu("bd: game started at level ", start_level);
            LOGNL();
        }
        break;

    case STATE_PLAYING:
        switch (play_tick((uint8_t)held, (pressed & IN_OPT1) != 0)) {   /* OPTION 1: the original's R */
        case PLAY_CAUGHT:  die("caught"); break;
        case PLAY_BURIED:  die("buried"); break;
        case PLAY_GAVE_UP: die("gave up"); break;
        case PLAY_WON:
            gs.state = STATE_LEVEL_DONE;
            transition_frames = 60;
            sound_sfx(SFX_LEVEL);
            logu("bd: level ", gs.level_num);
            lynx_log_s(" complete\n");
            break;
        }
        break;

    case STATE_DYING:
        if (--transition_frames > 0) break;
        gs.lives--;
        if (gs.lives <= 0) {
            gs.state = STATE_GAMEOVER;
            transition_frames = 30;
            new_hi = gs.score > hiscore;
            if (new_hi) hiscore = gs.score;
            lynx_log_s("bd: gameover score=");
            logscore(gs.score);
            logu(" level=", gs.level_num);
            lynx_log_s(" hi=");
            logscore(hiscore);
            logu(" new=", new_hi);
            LOGNL();
        } else {
            start_level_play();     /* reload the current level */
        }
        break;

    case STATE_LEVEL_DONE:
        if (--transition_frames > 0) break;
        gs.score += 1000;
        gs.level_num++;
        if (gs.level_num > MAX_LEVELS) gs.level_num = 1;
        start_level_play();
        break;

    case STATE_GAMEOVER:
        if (transition_frames > 0)
            transition_frames--;
        else if (pressed & (IN_A | IN_B))
            to_title();
        break;
    }
}

/* the game over box's lines */
static char go1[24], go2[26];
static void gameover_text(void)
{
    strcpy(go1, "SCORE ");
    strcat(go1, render_num(gs.score, 6));
    strcat(go1, "  LEVEL ");
    strcat(go1, render_num(gs.level_num, 3));
    strcpy(go2, new_hi ? "NEW HIGH SCORE! PRESS A" : "PRESS A TO CONTINUE");
}

/* frame rate: display frames per 100 drawn in play (50 Hz: 100 when none
   is late) */
static uint8_t perf_n;
static uint16_t perf_vbl, last_vbl;

int main(void)
{
    static uint8_t prev_state;
    static uint16_t held, pressed, last_fc;
    static uint8_t ticks, t;

    render_init();
    level_init();
    sound_init();
    gs.lives = 5;
    gs.level_num = 1;
    gs.state = STATE_TITLE;
    lynx_log_s("bd: ready\n");
    to_title();

    for (;;) {
        held = input_update();
        pressed = input_new;
        prev_state = gs.state;

        if (paused) {               /* port: the Lynx's PAUSE (the original has none) */
            if (pressed & IN_PAUSE) {
                paused = 0;
                render_room();
                last_fc = frame_count;
                lynx_log_s("bd: paused=0\n");
            } else if (pressed & IN_OPT1) {     /* PAUSE + OPTION 1: back to the title */
                paused = 0;
                lynx_log_s("bd: paused=0\n");
                to_title();
            } else {
                render_frame(1);
                render_msg("PAUSED", 13, "PAUSE: GO ON", "OPTION 1: TITLE", 9);
                render_present();
                continue;
            }
        } else if ((pressed & IN_PAUSE) && gs.state == STATE_PLAYING) {
            paused = 1;
            snd_all_off();
            lynx_log_s("bd: paused=1\n");
            continue;
        }

        /* port: a fixed timestep, as the Atari 8-bit port: the rules and the
           music run once per 50 Hz display frame; after a frame that took
           longer (a guard's path search) they catch up, at most 3 */
        ticks = (uint8_t)(frame_count - last_fc);
        last_fc = frame_count;
        if (ticks == 0) ticks = 1;
        if (ticks > 3) ticks = 3;
        for (t = 0; t < ticks; ++t) {
            game_tick(held, pressed);
            pressed = 0;            /* an edge counts once */
            gs.frame++;
            watch_state();
            if (sfx_pending) {
                if (sfx_pending == SFX_DIG) {
                    lynx_log_s(gs.player.dir == DIR_LEFT ? "bd: dig left at " : "bd: dig right at ");
                    logu("", gs.player.gx);
                    logu(",", gs.player.gy);
                    LOGNL();
                } else if (sfx_pending == SFX_TRAP) {
                    lynx_log_s("bd: guard trapped\n");
                }
                sound_sfx(sfx_pending);
                sfx_pending = 0;
            }
            sound_tick();
            if (gs.state != prev_state) break;
        }

        switch (gs.state) {
        case STATE_TITLE:
            if (++blink_t == 50) blink_t = 0;
            render_title(blink_t >= 25, hiscore);       /* the original's blink (frame / 25) */
            break;
        case STATE_PLAYING:
            render_frame(1);
            if (++perf_n == 100) {
                perf_vbl = frame_count - last_vbl;
                last_vbl = frame_count;
                logu("bd: perf frames=100 vbl=", perf_vbl);
                logu(" guards=", gs.num_enemies);
                LOGNL();
                perf_n = 0;
            }
            break;
        case STATE_DYING:
            render_frame((transition_frames & 3) < 2);  /* the original's blink */
            break;
        case STATE_LEVEL_DONE:
            render_frame(1);
            render_msg("LEVEL COMPLETE!", 7, "+1000 BONUS!", 0, 9);
            break;
        case STATE_GAMEOVER:
            if (prev_state != STATE_GAMEOVER) gameover_text();
            render_frame(0);
            render_msg("GAME OVER", 11, go1, go2, 13);
            break;
        }
        if (gs.state == STATE_PLAYING && prev_state != STATE_PLAYING) {
            perf_n = 0;
            last_vbl = frame_count;
        }
        render_present();
    }
}
