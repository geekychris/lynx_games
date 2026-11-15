/*
 * Nova Defense (Atari Lynx). Port of amiga_mcp/examples/nova_defense.
 *
 * game.c is the original's logic; this file is the Lynx front end: Suzy
 * sprites for everything in play (one chain of 65 SCBs a frame: the three
 * shields, 48 aliens, the comet, the gun, the bullets, the explosions), the
 * status line, the title and game-over screens in the original's 5x7 font
 * (Suzy-scaled), Mikey sound for the original's effects, march and
 * arpeggio, and the log lines the test reads. It runs at the original's
 * 50 Hz, one update a frame.
 */
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "art.h"

#pragma static-locals (on)

/* ------------------------------------------------------------ palette */

/* The original's colours (0xRGB). port: pen 0 is transparent on the Lynx,
   so the background is pen 1 and white is pen 2 (the original's green,
   which it never drew). */
static const uint16_t palette[16] = {
    0x000, 0x000, 0xFFF, 0x080, 0xE22, 0xFA0, 0x0EE, 0xE2E,
    0xFF0, 0x44F, 0x88F, 0xF64, 0x4F4, 0x888, 0xF80, 0xAAA,
};

/* ------------------------------------------------------------ sprites */

/* Two sets of SCBs, one per screen buffer. port (speed): the screen isn't
   cleared every frame (a full-screen Suzy fill is a third of a frame).
   Before a set is updated, it is drawn again with every pen mapped to the
   background (erase_pass): that erases exactly what this buffer shows from
   two frames ago. The shields are background-type sprites that redraw their
   whole rectangle (so a hit shows), the status line is drawn only when it
   changes. Frames with text over the play (wave clear, pause) clear in
   full. */
#define S_SHIELD 0
#define S_ALIEN  (S_SHIELD + SHIELD_COUNT)
#define S_UFO    (S_ALIEN + ALIEN_ROWS * ALIEN_COLS)
#define S_PLAYER (S_UFO + 1)
#define S_BUL    (S_PLAYER + 1)
#define S_EX     (S_BUL + 1 + MAX_ALIEN_BULLETS)
#define N_SCB    (S_EX + MAX_EXPLOSIONS)          /* 65 */
SCB_REHV_PAL scbs[2][N_SCB];
SCB_REHV_PAL *cs;                                  /* the current set (objs.s too) */
/* objs.s */
void aliens_scbs(void);
void reusepal_on(void);
void reusepal_off(void);
static uint8_t cur, full_clear, hud_dirty;
static SCB_REHV_PAL erase_head;
static unsigned char shield_data[SHIELD_COUNT][SHIELD_PH * 6 + 1];

#define HUD_LINE_Y 94           /* the original's ground line */
#define HUD_TEXT_Y 96

/* colours per alien type: body, accent (the original's) */
static const uint8_t alien_colors[3][2] = {
    { COL_MAGENTA, COL_YELLOW },  /* type C, top rows */
    { COL_BRGREEN, COL_WHITE },   /* type B */
    { COL_LTBLUE,  COL_ORANGE },  /* type A */
};

static void pens2(SCB_REHV_PAL *s, uint8_t body, uint8_t accent)
{
    s->penpal[0] = body;                            /* pen 0 transparent, pen 1 body */
    s->penpal[1] = (uint8_t)(accent << 4);          /* pen 2 accent */
}

static void sprites_init(void)
{
    uint8_t i, set, t;
    SCB_REHV_PAL *s;
    for (set = 0; set < 2; ++set) {
        for (i = 0; i < N_SCB; ++i) {
            s = &scbs[set][i];
            if (i < S_ALIEN) {
                spr_init(s, shield_data[i], 2);
                s->sprctl0 = BPP_2 | TYPE_BACKNONCOLL;   /* pen 0 drawn: the background */
                s->penpal[0] = (COL_BG << 4) | COL_CYAN;
                s->penpal[1] = (uint8_t)(COL_WHITE << 4);
                s->vpos = SHIELD_Y / 2;
            } else if (i < S_UFO) {
                t = (uint8_t)((i - S_ALIEN) >> 4);       /* two rows (16) per type */
                spr_init(s, alien_frames[t * 2], 2);
                pens2(s, alien_colors[t][0], alien_colors[t][1]);
            } else if (i == S_UFO) {
                spr_init(s, comet_frames[0], 2);
                pens2(s, COL_DKORANGE, COL_WHITE);
                s->vpos = UFO_Y / 2;
            } else if (i == S_PLAYER) {
                spr_init(s, player_frames[0], 2);
                pens2(s, COL_YELLOW, COL_CYAN);
                s->vpos = PLAYER_Y / 2;
            } else if (i == S_BUL) {
                spr_init(s, pbullet_frames[0], 2);
                pens2(s, COL_CYAN, COL_WHITE);
            } else if (i < S_EX) {
                spr_init(s, abullet_frames[0], 1);
                spr_color(s, COL_MAGENTA);
            } else {
                spr_init(s, boom_frames[0], 1);
            }
            if (i >= S_ALIEN) spr_hide(s, 1);
            if (i) spr_link(s - 1, s);
        }
    }
    cs = scbs[0];
    /* the erase pass's head: an off-screen pixel whose pen palette (every
       pen but 0 -> the background) the rest of the chain reuses */
    spr_box(&erase_head, -16, -16, 1, 1, COL_BG);
    memset(erase_head.penpal, (COL_BG << 4) | COL_BG, 8);
    erase_head.penpal[0] = COL_BG;          /* but pen 0 stays transparent (it is the
                                               mapped pen that Suzy skips) */
}

/* draw the current set again in the background colour: everything but the
   shields sets REUSEPAL (no pen palette of its own), so Suzy keeps the
   head's */
static void erase_pass(void)
{
    reusepal_on();
    erase_head.next = (char *)&cs[S_ALIEN];
    spr_draw(&erase_head);
    reusepal_off();
}

/* a shield's sprite from its pixels: 2 bpp literal lines of 16 pixels */
static void shield_make(uint8_t i)
{
    unsigned char *d = shield_data[i];
    const uint8_t *p = shield_px[i][0];
    uint8_t y, b;
    for (y = 0; y < SHIELD_PH; ++y) {
        *d++ = 6;
        for (b = 0; b < 4; ++b, p += 4)
            *d++ = (uint8_t)((p[0] << 6) | (p[1] << 4) | (p[2] << 2) | p[3]);
        *d++ = 0;
    }
    *d = 0;
}

static void sprites_update(void)
{
    static SCB_REHV_PAL *s;
    static uint8_t i;

    if (shield_dirty) {
        for (i = 0; i < SHIELD_COUNT; ++i)
            if (shield_dirty & (1 << i)) shield_make(i);
        shield_dirty = 0;
    }
    for (i = 0; i < SHIELD_COUNT; ++i) cs[S_SHIELD + i].hpos = shield_x[i] >> 1;

    aliens_scbs();                          /* objs.s: the swarm */

    s = &cs[S_UFO];
    spr_hide(s, !ufo_active);
    s->hpos = ufo_x >> 1;

    /* the gun (blinks while dying, as the original) */
    s = &cs[S_PLAYER];
    spr_hide(s, state == STATE_DYING && !(state_timer & 4));
    s->hpos = player_x >> 1;

    s = &cs[S_BUL];
    spr_hide(s, !bul_active[0]);
    s->hpos = (bul_x[0] >> 1) - 1;
    s->vpos = bul_y[0] >> 1;
    for (i = 1; i <= MAX_ALIEN_BULLETS; ++i) {
        ++s;
        spr_hide(s, !bul_active[i]);
        s->hpos = (bul_x[i] >> 1) - 1;
        s->vpos = (bul_y[i] >> 1) - 1;
        s->data = (unsigned char *)abullet_frames[(bul_y[i] & 4) ? 0 : 1];
    }

    for (i = 0; i < MAX_EXPLOSIONS; ++i) {
        ++s;
        spr_hide(s, !ex_active[i]);
        if (ex_active[i]) {
            s->hpos = (ex_x[i] >> 1) - 5;
            s->vpos = (ex_y[i] >> 1) - 5;
            s->data = (unsigned char *)boom_frames[10 - ex_timer[i]];
            s->penpal[0] = ex_color[i];
        }
    }
}

/* ------------------------------------------------------------ text */

/* The original's 5x7 font as Suzy sprites, scaled 1x, 2x... */
static SCB_REHV_PAL big[16];

static void draw_big(uint8_t x, uint8_t y, uint8_t col, const char *t, uint8_t scale)
{
    SCB_REHV_PAL *s = big, *prev = 0;
    char ch;
    uint8_t g;
    for (; (ch = *t) != 0; ++t, x += 6 * scale) {
        if (ch >= '0' && ch <= '9') g = ch - '0';
        else if (ch >= 'A' && ch <= 'Z') g = ch - 'A' + 10;
        else continue;
        spr_init(s, glyph_frames[g], 1);
        spr_color(s, col);
        s->hpos = x;
        s->vpos = y;
        s->hsize = s->vsize = (uint16_t)scale << 8;
        if (prev) spr_link(prev, s);
        prev = s++;
    }
    if (prev) spr_draw(big);
}

static void draw_big_centre(uint8_t y, uint8_t col, const char *t, uint8_t scale)
{
    draw_big((uint8_t)((160 - (strlen(t) * 6 - 1) * scale) / 2), y, col, t, scale);
}

static SCB_REHV_PAL icon;
static char wave_str[3];

/* port (speed): the points and best digits are a fixed chain of 12 SCBs
   whose pictures change (text_small builds its chain in C: ~5k cycles for
   12 digits) */
extern const unsigned char font3x5_data[64][16];
static SCB_REHV_PAL dig_head;
static SCB_RENONE dig[11];
static uint8_t hud_nums;                /* frames left to redraw the numbers in */

static void digits_init(void)
{
    uint8_t i;
    SCB_RENONE *d;
    spr_init(&dig_head, font3x5_data[0], 1);
    spr_color(&dig_head, COL_YELLOW);
    dig_head.hpos = 14;
    dig_head.vpos = HUD_TEXT_Y;
    dig_head.next = (char *)&dig[0];
    for (i = 0; i < 11; ++i) {
        d = &dig[i];
        d->sprctl0 = BPP_1 | TYPE_NORMAL;
        d->sprctl1 = LITERAL | RENONE | REUSEPAL;
        d->sprcoll = NO_COLLIDE;
        d->next = i < 10 ? (char *)&dig[i + 1] : 0;
        d->hpos = i < 5 ? 18 + 4 * i : 59 + 4 * (i - 5);
        d->vpos = HUD_TEXT_Y;
    }
}

static void hud_numbers(void)
{
    uint8_t i;
    dig_head.data = (unsigned char *)font3x5_data[score_str[0] - 32];
    for (i = 0; i < 5; ++i) dig[i].data = (unsigned char *)font3x5_data[score_str[i + 1] - 32];
    for (i = 0; i < 6; ++i) dig[i + 5].data = (unsigned char *)font3x5_data[hi_str[i] - 32];
    gfx_rect(14, HUD_TEXT_Y, 24, 5, COL_BG);
    gfx_rect(59, HUD_TEXT_Y, 24, 5, COL_BG);
    spr_draw(&dig_head);
}

/* the status line: the original's PTS / BEST / reserve ships along the
   bottom; port: WAVE (top right in the original) joins them */
static void hud_draw(void)
{
    uint8_t i;
    gfx_rect(0, HUD_LINE_Y, 160, 102 - HUD_LINE_Y, COL_BG);
    gfx_rect(0, HUD_LINE_Y, 160, 1, COL_CYAN);
    text_small(1, HUD_TEXT_Y, COL_GRAY, "PTS");
    text_small(42, HUD_TEXT_Y, COL_GRAY, "BEST");
    text_small(87, HUD_TEXT_Y, COL_GRAY, "WAVE");
    wave_str[0] = wave >= 10 ? '0' + wave / 10 % 10 : ' ';
    wave_str[1] = '0' + wave % 10;
    text_small(104, HUD_TEXT_Y, COL_WHITE, wave_str);
    icon.data = (unsigned char *)player_frames[0];
    pens2(&icon, COL_YELLOW, COL_CYAN);
    icon.vpos = HUD_TEXT_Y;
    for (i = 0; lives > 1 && i < (uint8_t)(lives - 1) && i < 4; ++i) {
        icon.hpos = 152 - i * 9;
        spr_draw(&icon);
    }
    hud_numbers();
}

/* ------------------------------------------------------------ sound */

/* Mikey channels as the original's Paula channels: 0 the march, 1 shots and
   explosions, 2 the comet, 3 the arpeggio. port: the original played
   32-sample waveforms (square, sine, noise, a pulse); Mikey plays square
   waves and noise at the same pitches (Paula period p over 32 samples is
   3546895 / (32 p) Hz) and lengths. */
#define PAULA(p) (3546895UL / (32UL * (p)))
#pragma warn (const-comparison, push, off)    /* SND_PERIOD's range tests on constants */
static const uint16_t march_period[3] = {
    SND_PERIOD(PAULA(640)), SND_PERIOD(PAULA(538)), SND_PERIOD(PAULA(452)) };
static const uint16_t melody_period[16] = {
    SND_PERIOD(PAULA(504)), SND_PERIOD(PAULA(423)), SND_PERIOD(PAULA(336)), SND_PERIOD(PAULA(423)),
    SND_PERIOD(PAULA(504)), SND_PERIOD(PAULA(566)), SND_PERIOD(PAULA(504)), SND_PERIOD(PAULA(423)),
    SND_PERIOD(PAULA(377)), SND_PERIOD(PAULA(336)), SND_PERIOD(PAULA(283)), SND_PERIOD(PAULA(336)),
    SND_PERIOD(PAULA(377)), SND_PERIOD(PAULA(423)), SND_PERIOD(PAULA(504)), SND_PERIOD(PAULA(566)) };
/* the comet's whoosh: period 420 - 7 * phase, phase 0..31 */
#define CP(i) SND_PERIOD(PAULA(420 - 7 * (i)))
static const uint16_t comet_period[32] = {
    CP(0), CP(1), CP(2), CP(3), CP(4), CP(5), CP(6), CP(7), CP(8), CP(9), CP(10), CP(11),
    CP(12), CP(13), CP(14), CP(15), CP(16), CP(17), CP(18), CP(19), CP(20), CP(21), CP(22),
    CP(23), CP(24), CP(25), CP(26), CP(27), CP(28), CP(29), CP(30), CP(31) };
#pragma warn (const-comparison, pop)
#define MELODY_SPEED 12

static uint8_t ch_timer[4];
static uint8_t ufo_sound, ufo_phase, melody_pos, melody_timer;

/* Mikey volume 0..127: twice the original's Paula 0..64 */
static void sound_march(uint8_t note) { snd_tone_p(0, march_period[note], 100, 0); ch_timer[0] = 3; }
#pragma warn (const-comparison, push, off)
static void sound_shoot(void)         { snd_tone_p(1, SND_PERIOD(PAULA(200)), 80, 0); ch_timer[1] = 4; }
#pragma warn (const-comparison, pop)
static void sound_alien_explode(void) { snd_noise(1, 3, 96, 0); ch_timer[1] = 8; }
static void sound_player_explode(void){ snd_noise(1, 5, 120, 0); ch_timer[1] = 25; }

static void sound_ufo(uint8_t on)
{
    if (on && !ufo_sound) {
        ufo_sound = 1;
        ufo_phase = 0;
        snd_tone_p(2, comet_period[0], 64, 0);
    } else if (!on && ufo_sound) {
        ufo_sound = 0;
        snd_off(2);
    }
}

static void sound_update(void)
{
    uint8_t i;
    for (i = 0; i < 4; ++i)
        if (ch_timer[i] && --ch_timer[i] == 0) snd_off(i);
    if (ufo_sound) {
        ufo_phase = (ufo_phase + 1) & 31;
        snd_freq_p(2, comet_period[ufo_phase]);
    }
    if (++melody_timer >= MELODY_SPEED) {
        melody_timer = 0;
        snd_tone_p(3, melody_period[melody_pos], 40, 0);
        ch_timer[3] = MELODY_SPEED - 2;
        melody_pos = (melody_pos + 1) & 15;
    }
}

/* ------------------------------------------------------------ log */

#ifndef LYNX_NO_LOG
#define LOG(...) lynx_log(__VA_ARGS__)
/* a byte 0..255 without lynx_log_u's divides (it runs in play) */
static void logb(const char *k, uint8_t v)
{
    uint8_t h = 0, t = 0;
    lynx_log_s(k);
    while (v >= 100) { v -= 100; ++h; }
    while (v >= 10) { v -= 10; ++t; }
    if (h) lynx_log_c('0' + h);
    if (h || t) lynx_log_c('0' + t);
    lynx_log_c('0' + v);
}
#else
#define LOG(...) ((void)0)
#define logb(k, v) ((void)0)
#endif

/* ------------------------------------------------------------ screens */

static void draw_title(void)
{
    uint8_t f = (title_blink >> 4) & 1;
    draw_big_centre(3, COL_MAGENTA, "NOVA", 2);
    draw_big_centre(19, COL_CYAN, "DEFENSE", 2);
    text_small_centre(37, COL_GRAY, "KNOW YOUR ENEMY");
    /* the invaders in a line, their points underneath */
    icon.data = (unsigned char *)alien_frames[0 + f]; pens2(&icon, alien_colors[0][0], alien_colors[0][1]);
    spr_pos(&icon, 28, 46); spr_draw(&icon);
    icon.data = (unsigned char *)alien_frames[2 + f]; pens2(&icon, alien_colors[1][0], alien_colors[1][1]);
    spr_pos(&icon, 58, 46); spr_draw(&icon);
    icon.data = (unsigned char *)alien_frames[4 + f]; pens2(&icon, alien_colors[2][0], alien_colors[2][1]);
    spr_pos(&icon, 88, 46); spr_draw(&icon);
    icon.data = (unsigned char *)comet_frames[0]; pens2(&icon, COL_DKORANGE, COL_WHITE);
    spr_pos(&icon, 118, 47); spr_draw(&icon);
    text_small(28, 54, COL_WHITE, "30");
    text_small(58, 54, COL_WHITE, "20");
    text_small(88, 54, COL_WHITE, "10");
    text_small(112, 54, COL_WHITE, "COMET");
    icon.data = (unsigned char *)player_frames[0]; pens2(&icon, COL_YELLOW, COL_CYAN);
    spr_pos(&icon, 77, 64); spr_draw(&icon);
    if (hiscore > 0) {
        text_small_centre(72, COL_GRAY, "BEST");
        text_small_centre(79, COL_YELLOW, hi_str);
    }
    if ((title_blink >> 3) & 1)
        text_small_centre(88, COL_WHITE, "PRESS A TO START");
    text_small_centre(96, COL_GRAY, "D-PAD AND A");     /* port: was MOUSE AND KEYBOARD */
}

static void draw_gameover(void)
{
    draw_big_centre(18, COL_RED, "GAME OVER", 2);
    draw_big_centre(42, COL_WHITE, "SCORE", 1);
    draw_big_centre(53, COL_YELLOW, score_str, 1);
    text_small_centre(68, COL_GRAY, "WAVE");
    text_small_centre(75, COL_WHITE, wave_str[0] == ' ' ? wave_str + 1 : wave_str);
    if (state_timer & 8) text_small_centre(88, COL_WHITE, "PRESS A");
}

/* ------------------------------------------------------------ main */

static uint8_t paused;
static uint16_t fps_vbl, fps_loops, late, last_vbl;
static uint8_t busy_max;

static void log_wave(void)
{
    lynx_log_s("nd: wave=");
    logb("", wave);
    logb(" lives=", (uint8_t)lives);
    lynx_log_c('\n');
}

int main(void)
{
    uint8_t prev_state, prev_lives, startup = 10, busy, fire, game_screen;
    uint16_t now;

    gfx_init();
    gfx_set_rate(50);                       /* the original's PAL frame rate */
    gfx_palette(palette);
    snd_init();
    sprites_init();
    spr_init(&icon, player_frames[0], 2);
    pens2(&icon, COL_YELLOW, COL_CYAN);
    digits_init();
    game_init();
    LOG("nd: ready hz=50\n");
    LOG("nd: title hi=0\n");

    for (;;) {
        input_update();
        fire = (input_new & (IN_A | IN_B)) != 0;
        if (startup) { --startup; fire = 0; }  /* the original ignored fire for 10 frames */

        if (state != STATE_TITLE && state != STATE_GAMEOVER && (input_new & IN_PAUSE)) {
            paused ^= 1;
            if (paused) { snd_all_off(); ufo_sound = 0; memset(ch_timer, 0, 4); }
            fps_vbl = frame_count;          /* the frame rate counts play only */
            fps_loops = late = 0;
            LOG("nd: paused=%u\n", (unsigned)paused);
        }
        if (paused && (input_new & IN_OPT1)) {
            /* the Lynx way: PAUSE, then OPTION 1 = restart (here: the title) */
            paused = 0;
            state = STATE_TITLE;
            LOG("nd: title hi=%lu\n", hiscore);
        }

        if (!paused) {
            prev_state = state;
            prev_lives = (uint8_t)lives;
            if (state == STATE_TITLE && fire) rnd_seed(frame_count | 1);   /* the original seeded from the beam */
            game_update((input_held & IN_LEFT) != 0, (input_held & IN_RIGHT) != 0, fire);

            if (ev_shoot) sound_shoot();
            if (ev_alien_hit) sound_alien_explode();
            if (ev_player_hit) sound_player_explode();
            if (ev_ufo_hit) sound_alien_explode();
            if (ev_march) sound_march(march_note);
            sound_ufo(ufo_active && state != STATE_TITLE);
            sound_update();

            if (ev_alien_hit) {
                logb("nd: hit row=", hit_row);
                lynx_log_s(" score=");
                lynx_log_s(score_str);
                logb(" alive=", alive_count);
                lynx_log_c('\n');
            }
            if (ev_ufo_hit) {
                lynx_log_s("nd: comet score=");
                lynx_log_s(score_str);
                lynx_log_c('\n');
            }
            if (ev_player_hit) lynx_log_s("nd: hit player\n");
            if (ev_alien_hit || ev_ufo_hit) hud_nums = 2;
            if ((uint8_t)lives != prev_lives) hud_dirty = 2;
            if ((uint8_t)lives != prev_lives) { logb("nd: lives=", (uint8_t)lives); lynx_log_c('\n'); }
            if (state != prev_state) {
                switch (state) {
                case STATE_PLAYING:
                    if (prev_state != STATE_DYING) {
                        log_wave();
                        full_clear = 2;
                        fps_vbl = frame_count;
                        fps_loops = late = 0;
                        busy_max = 0;
                    }
                    break;
                case STATE_WAVE_CLEAR:
                    lynx_log_s("nd: waveclear wave=");
                    logb("", wave);
                    lynx_log_s(" score=");
                    lynx_log_s(score_str);
                    lynx_log_c('\n');
                    break;
                case STATE_GAMEOVER:
                    snd_off(2);
                    ufo_sound = 0;
                    LOG("nd: gameover score=%lu wave=%u hi=%lu invaded=%u\n", score, wave, hiscore,
                        (unsigned)(prev_state == STATE_PLAYING));
                    break;
                case STATE_TITLE:
                    LOG("nd: title hi=%lu\n", hiscore);
                    break;
                }
            }
        }

        /* --- draw --- */
        game_screen = state == STATE_PLAYING || state == STATE_DYING || state == STATE_WAVE_CLEAR;
        if (game_screen) {
            if (full_clear || paused || state == STATE_WAVE_CLEAR) {
                gfx_clear(COL_BG);
                hud_draw();
                if (full_clear) --full_clear;
            } else {
                erase_pass();
                if (hud_dirty) { hud_draw(); --hud_dirty; }
                else if (hud_nums) { hud_numbers(); --hud_nums; }
            }
            sprites_update();
            spr_draw(cs);
            if (state == STATE_WAVE_CLEAR) draw_big_centre(60, COL_YELLOW, "WAVE CLEAR", 1);
            if (paused) draw_big_centre(60, COL_YELLOW, "PAUSED", 1);
            if (state == STATE_WAVE_CLEAR || paused) full_clear = 2;
        } else {
            gfx_clear(COL_BG);
            if (state == STATE_TITLE) draw_title();
            else draw_gameover();
            full_clear = 2;
        }

        busy = (uint8_t)(MIKEY.timer2.reload - MIKEY.timer2.count);
        if (busy > busy_max) busy_max = busy;

        gfx_present();
        cur ^= 1;
        cs = scbs[cur];

        /* frame rate: loops per vertical blank, logged every 2 s in play */
        now = frame_count;
        if ((uint16_t)(now - last_vbl) > 1) late += (uint16_t)(now - last_vbl) - 1;
        last_vbl = now;
        ++fps_loops;
        if ((uint16_t)(now - fps_vbl) >= 100) {
            if (state == STATE_PLAYING && !paused) {
                logb("nd: fps=", (uint8_t)(fps_loops * 50u / (uint16_t)(now - fps_vbl)));
                logb(" late=", (uint8_t)late);
                logb(" busy=", busy_max);
                logb("/", MIKEY.timer2.reload + 1);
                logb(" alive=", alive_count);
                lynx_log_c('\n');
            }
            fps_vbl = now;
            fps_loops = late = 0;
            busy_max = 0;
        }
    }
}
