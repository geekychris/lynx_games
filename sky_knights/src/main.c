/*
 * Sky Knights (Atari Lynx). Port of amiga_mcp/examples/sky_knights.
 *
 * game.c + physics.s are the original's rules (by way of the Atari 8-bit
 * port's 6502 speed-ups); this file is the Lynx front end: the original's
 * screens redrawn for 160 x 102 (its 320 x 256 world at x / 2, y * 2 / 5),
 * every knight and pod a Suzy sprite in one chain a frame, the status line
 * in the SDK's 3x5 font, titles in the original's 5x7 font (Suzy-scaled),
 * Mikey sound for the original's effects and tune, and the log lines the
 * test reads. It runs at the original's 50 Hz, one update a frame.
 */
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "art.h"

#pragma static-locals (on)

/* ------------------------------------------------------------ palette */

/* The original's 16 colours (main.c), same indices. port: a pen that maps
   to colour 0 is transparent in a Suzy sprite, so the background is colour 6
   (the original's player 2 cyan: one player on the Lynx). */
#define COL_WHITE       1
#define COL_PLATFORM    2
#define COL_PLAT_SHADOW 3
#define COL_P1_MOUNT    4
#define COL_P1_KNIGHT   5
#define COL_BG          6
#define COL_P2_KNIGHT   7       /* red */
#define COL_SWOOPER     8
#define COL_RAIDER      9
#define COL_WRAITH      10
#define COL_EGG         11
#define COL_SCORE       12
#define COL_GROUND      13
#define COL_EGG_HATCH   14
#define COL_LTGRAY      15

static const uint16_t palette[16] = {
    0x000, 0xFFF, 0x840, 0x520, 0xFC0, 0x06F, 0x000, 0xF22,
    0xF80, 0xAAA, 0x22A, 0x0E0, 0xFF0, 0xCA8, 0xF8F, 0xCCC,
};

/* ------------------------------------------------------------ sprites */

/* Play is one chain a frame in the original's drawing order: the ledges,
   the pods, the enemies, the knight; then the status line. The ground is
   drawn once (nothing in play reaches its lines).
   port (speed): the screen isn't cleared every frame (a full-screen Suzy
   fill is ~25k cycles, a third of a 50 Hz frame). There are two sets of the
   moving objects' SCBs, one per screen buffer; before a set is updated it
   is drawn again with every pen mapped to the background (erase_pass), which
   erases exactly what that buffer shows from two frames ago; then the
   ledges are redrawn (a knight may have crossed one) and the new set on top.
   Screens that change entirely (wave intro, pause, title) clear in full. */
#define O_POD     0
#define O_ENEMY   (O_POD + MAX_EGGS)
#define O_PLAYER  (O_ENEMY + MAX_ENEMIES)
#define N_OBJ     (O_PLAYER + 1)
static SCB_REHV_PAL sets[2][N_OBJ];
static SCB_REHV_PAL *cs;                /* the set of the buffer being drawn */
static uint8_t cur;
static SCB_REHV_PAL ledge[12];          /* 6 ledges: top + underside boxes */
static SCB_REHV_PAL ground[2];          /* the ground and its notches */
static SCB_REHV_PAL erase_head;
static SCB_REHV_PAL lives_box[5];

#define GROUND_LINE 94                  /* ytab[GROUND_Y] */

/* the ground's dark notches every 12 world pixels (6 here), 1 bpp, 160 wide */
static unsigned char stripe_data[2 + 20 + 1];

static void knight_pens(SCB_REHV_PAL *s, uint8_t body, uint8_t rider)
{
    s->penpal[0] = body;                            /* pen 0 transparent, pen 1 mount */
    s->penpal[1] = (uint8_t)((rider << 4) | COL_WHITE);   /* 2 rider, 3 lance */
    s->penpal[2] = (uint8_t)(COL_SCORE << 4);       /* 4 the beak (the original's yellow) */
}

static void sprites_init(void)
{
    uint8_t i, j, set;
    SCB_REHV_PAL *s;

    stripe_data[0] = 22;                            /* literal line: 21 bytes after the count */
    for (i = 0; i < 160; i += 6) stripe_data[1 + (i >> 3)] |= (uint8_t)(0x80 >> (i & 7));
    spr_box(&ground[0], 0, GROUND_LINE, 160, 102 - GROUND_LINE, COL_GROUND);
    spr_init(&ground[1], stripe_data, 1);
    spr_color(&ground[1], COL_PLAT_SHADOW);
    ground[1].vpos = GROUND_LINE;
    spr_link(&ground[0], &ground[1]);
    for (i = 0; i < 12; ++i) {
        s = &ledge[i];
        j = (uint8_t)(1 + (i >> 1));                /* platform 1..6 */
        if (i & 1)
            spr_box(s, plat_x[j] >> 1, ytab[plat_y[j]] + 1, (uint8_t)(plat_w[j] >> 1), 2, COL_PLAT_SHADOW);
        else
            spr_box(s, plat_x[j] >> 1, ytab[plat_y[j]], (uint8_t)(plat_w[j] >> 1), 1, COL_PLATFORM);
        if (i) spr_link(s - 1, s);
    }
    for (set = 0; set < 2; ++set) {
        for (i = 0; i < N_OBJ; ++i) {
            s = &sets[set][i];
            if (i < O_ENEMY) {
                spr_init(s, pod_frames[0], 1);
                spr_color(s, COL_EGG);
            } else {
                spr_init(s, knight_frames[0], 3);
                knight_pens(s, COL_P1_MOUNT, COL_P1_KNIGHT);
            }
            spr_hide(s, 1);
            if (i) spr_link(s - 1, s);
        }
    }
    cs = sets[0];
    /* the erase pass's head: an off-screen pixel whose pen palette (every
       pen but 0 -> the background) the rest of the chain reuses */
    spr_box(&erase_head, -16, -16, 1, 1, COL_BG);
    memset(erase_head.penpal, (COL_BG << 4) | COL_BG, 8);
    erase_head.penpal[0] = COL_BG;
    for (i = 0; i < 5; ++i) {
        spr_box(&lives_box[i], 2 + i * 4, 7, 3, 2, COL_P1_MOUNT);
        if (i) spr_link(&lives_box[i - 1], &lives_box[i]);
    }
}

/* draw the current set again in the background colour: REUSEPAL on every
   SCB, so Suzy keeps the head's pen palette */
static void erase_pass(void)
{
    static SCB_REHV_PAL *s;
    static uint8_t i;
    for (s = cs, i = N_OBJ; i; --i, ++s) s->sprctl1 |= REUSEPAL;
    erase_head.next = (char *)cs;
    spr_draw(&erase_head);
    for (s = cs, i = N_OBJ; i; --i, ++s) s->sprctl1 &= ~REUSEPAL;
}

/* a knight's SCB: the original's draw_rider at x / 2, y * 2 / 5. Row 0 of the
   picture is the rider's head (the original's y - 2). port: original bug -
   the original drew the lance and the mount's head on the left for facing 0
   (moving right) and on the right for facing 1; here they point the way the
   knight flies. */
static void knight_scb(SCB_REHV_PAL *s, uint8_t b, uint8_t frame)
{
    static int16_t x;
    x = b_px[b] >> 1;
    s->data = (unsigned char *)knight_frames[frame];
    s->vpos = ytab[b_py[b]] - 1;
    if (b_facing[b]) {                              /* left: Suzy mirrors around hpos */
        s->sprctl0 = BPP_3 | TYPE_NORMAL | HFLIP;
        s->hpos = x + KNIGHT_W - 1;
    } else {
        s->sprctl0 = BPP_3 | TYPE_NORMAL;
        s->hpos = x;
    }
    s->sprctl1 &= ~SKIP;
}

/* enemy colours by type: mount, rider (the original's draw_playing) */
static const uint8_t enemy_body[3] = { COL_SWOOPER, COL_RAIDER, COL_WRAITH };
static const uint8_t enemy_rider[3] = { (COL_P2_KNIGHT << 4) | COL_WHITE, (COL_LTGRAY << 4) | COL_WHITE,
                                        (COL_P1_KNIGHT << 4) | COL_WHITE };

static void sprites_update(void)
{
    static SCB_REHV_PAL *s;
    static uint8_t i, b, t;
    static int16_t x;
    static WORD tm;

    /* pods: green, flashing pink from 240 frames left, shaking in the last 60 */
    s = cs;
    for (i = 0; i < MAX_EGGS; ++i, ++s) {
        if (!g_active[i]) { s->sprctl1 |= SKIP; continue; }
        s->sprctl1 &= ~SKIP;
        x = FIX_INT(g_x[i]);
        tm = g_timer[i];
        s->penpal[0] = COL_EGG;
        if (tm < EGG_WOBBLE_TIME) {
            if (tm & 4) s->penpal[0] = COL_EGG_HATCH;
            if (tm < 60) x += (tm & 2) ? 1 : -1;
        }
        s->hpos = x >> 1;
        s->vpos = ytab[(uint8_t)FIX_INT(g_y[i])];
    }
    /* enemies (their wings and legs both follow anim_frame, as the original) */
    for (i = 0; i < MAX_ENEMIES; ++i, ++s) {
        if (!e_active[i]) { s->sprctl1 |= SKIP; continue; }
        b = (uint8_t)(EB + i);
        t = e_type[i];
        s->penpal[0] = enemy_body[t];
        s->penpal[1] = enemy_rider[t];
        knight_scb(s, b, b_anim_frame[b] ? 3 : 0);
    }
    /* the knight (not drawn while waiting to respawn) */
    if (p_alive[0]) knight_scb(s, 0, (uint8_t)(p_flapping[0] * 2 + b_anim_frame[0]));
    else s->sprctl1 |= SKIP;
}

/* the ledges and the current set */
static void draw_play(void)
{
    ledge[11].next = (char *)cs;
    spr_draw(ledge);
}

/* ------------------------------------------------------------ text */

/* The original's 5x7 font as Suzy sprites, scaled */
static SCB_REHV_PAL big[16];

static void draw_big(uint8_t x, uint8_t y, uint8_t col, const char *t, uint8_t scale)
{
    SCB_REHV_PAL *s = big, *prev = 0;
    char ch;
    uint8_t g;
    for (; (ch = *t) != 0; ++t, x += 6 * scale) {
        if (ch >= '0' && ch <= '9') g = ch - '0';
        else if (ch >= 'A' && ch <= 'Z') g = ch - 'A' + 10;
        else if (ch == '!') g = 36;
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

/* numbers without printf: wave (1..99) and the score without leading zeros */
static char wave_str[3];
static void make_wave_str(void)
{
    uint8_t w = (uint8_t)gs.wave, t = 0;
    while (w >= 10) { w -= 10; ++t; }
    if (t) { wave_str[0] = (char)('0' + t); wave_str[1] = (char)('0' + w); wave_str[2] = 0; }
    else { wave_str[0] = (char)('0' + w); wave_str[1] = 0; }
}
static const char *score_short(void)
{
    const char *p = score_str;
    while (*p == '0' && p[1]) ++p;
    return p;
}

static char line[24];
static void cat2(const char *a, const char *b)
{
    strcpy(line, a);
    strcat(line, b);
}

/* the status line (the original's draw_hud): P1 and the score top left,
   lives under it, the wave in the middle. port: no P2 (one player).
   port (speed): a fixed chain of sprites in the SDK's 3x5 font whose
   pictures change with the score (text_small builds its chain in C, ~1.3k
   cycles a character): "P1" + 7 digits in yellow, "WAVE nn" in white, the
   lives boxes. */
extern const unsigned char font3x5_data[64][16];
#define HUD_DIGITS 9                    /* "1" + 7 digits after the head's "P" */
#define HUD_WAVE 6                      /* "AVE" " " nn after the head's "W" */
static SCB_REHV_PAL hud_head, wave_head;
static uint8_t hud_dirty;
static SCB_RENONE hud_ch[HUD_DIGITS], wave_ch[HUD_WAVE];

static void glyph_scb(SCB_RENONE *d, int16_t x, void *next)
{
    d->sprctl0 = BPP_1 | TYPE_NORMAL;
    d->sprctl1 = LITERAL | RENONE | REUSEPAL;
    d->sprcoll = NO_COLLIDE;
    d->next = (char *)next;
    d->hpos = x;
    d->vpos = 1;
}

#define FONT(ch) ((unsigned char *)font3x5_data[(ch) - 32])

static void hud_init(void)
{
    uint8_t i;
    spr_init(&hud_head, FONT('P'), 1);
    spr_color(&hud_head, COL_SCORE);
    spr_pos(&hud_head, 2, 1);
    hud_head.next = (char *)&hud_ch[0];
    glyph_scb(&hud_ch[0], 6, &hud_ch[1]);
    for (i = 1; i < HUD_DIGITS; ++i)
        glyph_scb(&hud_ch[i], 6 + 4 * i, i + 1 < HUD_DIGITS ? (void *)&hud_ch[i + 1] : (void *)&wave_head);
    hud_ch[0].data = FONT('1');
    hud_ch[1].data = FONT(' ');
    spr_init(&wave_head, FONT('W'), 1);
    spr_color(&wave_head, COL_WHITE);
    wave_head.vpos = 1;
    wave_head.next = (char *)&wave_ch[0];
    for (i = 0; i < HUD_WAVE; ++i) {
        glyph_scb(&wave_ch[i], 0, i + 1 < HUD_WAVE ? (void *)&wave_ch[i + 1] : (void *)&lives_box[0]);
        wave_ch[i].data = FONT("AVE   "[i]);
    }
}

/* after a score, wave or lives change */
static void hud_update(void)
{
    uint8_t i, n, x;
    for (i = 0; i < 7; ++i) hud_ch[i + 2].data = FONT(score_str[i]);
    /* "WAVE n" centred, as the original */
    n = wave_str[1] ? 7 : 6;
    x = (uint8_t)((160 - (n * 4 - 1)) / 2);
    wave_head.hpos = x;
    for (i = 0; i < HUD_WAVE; ++i) wave_ch[i].hpos = x + 4 + 4 * i;
    wave_ch[4].data = FONT(wave_str[0]);
    wave_ch[5].data = FONT(wave_str[1] ? wave_str[1] : ' ');
    n = (uint8_t)p_lives[0];
    for (i = 0; i < 5; ++i) spr_hide(&lives_box[i], i >= n);
    hud_dirty = 2;                      /* clear the old digits in both buffers */
}

static void draw_hud(void)
{
    spr_draw(&hud_head);
}

/* every ledge, the ground drawn as one too (the original's title and wave
   screens draw all seven platforms alike) */
static SCB_REHV_PAL ground_plat[2];
static void draw_platforms(void)
{
    ledge[11].next = (char *)&ground_plat[0];
    spr_draw(ledge);
}

/* ------------------------------------------------------------ sound */

/* Mikey channels as the original's Paula channels: 0 the wing beat, 1 the
   joust and a lost knight, 2 a pod and the wave fanfare, 3 the tune.
   port: the original played two samples (flap.raw, smash.raw) and 32- or
   48-sample waves; Mikey plays noise and square waves at the same pitches
   (a 32-sample wave at Paula period p is 3546895 / 32p Hz) and lengths,
   volumes doubled to Mikey's 0-127. */
#define PAULA32(p) (3546895UL / (32UL * (p)))
#pragma warn (const-comparison, push, off)    /* SND_PERIOD's range tests on constants */
#define NP(p) SND_PERIOD(PAULA32(p))
static const uint16_t melody[32] = {          /* the original's fanfare, Paula periods */
    NP(428), NP(339), NP(285), NP(214), NP(285), NP(339), NP(428), NP(570),
    NP(508), NP(428), NP(339), NP(285), NP(320), NP(339), NP(381), NP(428),
    NP(428), NP(381), NP(339), NP(285), NP(254), NP(285), NP(339), NP(428),
    NP(570), NP(508), NP(428), NP(339), NP(381), NP(428), NP(570), NP(428) };
static const uint16_t wave_period = NP(428);
/* the pod's 48-sample sweep wave at period 300: ~246 Hz, its tone an octave up */
static const uint16_t pod_period = SND_PERIOD(3546895UL / (48UL * 300UL) * 2);
#pragma warn (const-comparison, pop)
#define MELODY_TEMPO 9                  /* frames a note */

static uint8_t ch_timer[4], melody_pos, melody_timer, flap_beat;

/* the wing beat: the original restarted its flap sample every frame the
   button was held, so it looped while flapping (a loop is 8 frames) and
   stopped up to 8 frames after; here a noise beat every 8 frames while held */
static void sound_flap(void)
{
    if (!flap_beat) { snd_noise(0, 6, 100, 12); flap_beat = 8; }
    ch_timer[0] = 8;
}
static void sound_kill(void) { snd_noise(1, 3, 120, 12); ch_timer[1] = 10; }
static void sound_die(void)  { snd_noise(1, 5, 110, 10); ch_timer[1] = 10; }
static void sound_egg(void)  { snd_tone_p(2, pod_period, 80, 0); ch_timer[2] = 5; }
static void sound_wave(void) { snd_tone_p(2, wave_period, 90, 0); ch_timer[2] = 15; }

static void sound_update(void)
{
    uint8_t i;
    if (flap_beat) --flap_beat;
    for (i = 0; i < 3; ++i)
        if (ch_timer[i] && --ch_timer[i] == 0) snd_off(i);
    if (melody_timer) --melody_timer;
    if (!melody_timer) {
        melody_timer = MELODY_TEMPO;
        snd_tone_p(3, melody[melody_pos], 44, 0);
        melody_pos = (melody_pos + 1) & 31;
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
static void log_score(void)
{
    lynx_log_s(" score=");
    lynx_log_s(score_short());
}
static uint8_t count_active(const uint8_t *a)
{
    uint8_t i, n = 0;
    for (i = 0; i < 8; ++i) n += a[i];
    return n;
}
#else
#define LOG(...) ((void)0)
#define logb(k, v) ((void)0)
#define log_score() ((void)0)
#define count_active(a) 0
#endif

/* ------------------------------------------------------------ screens */

/* the title (drawn into both buffers when it comes up; then only the
   knight is redrawn) */
static void draw_title(void)
{
    draw_platforms();
    draw_big_centre(6, COL_SCORE, "SKY KNIGHTS", 2);
    text_small_centre(25, COL_WHITE, "AN ARCADE CLASSIC");
    text_small_centre(60, COL_SCORE, "1 PLAYER - PRESS A");      /* port: was F1 (and F2: 2 players) */
    text_small_centre(78, COL_LTGRAY, "D-PAD FLY  A,B,UP FLAP");  /* port: was P1 ARROWS + SPACE */
    text_small_centre(85, COL_LTGRAY, "PAUSE TO PAUSE");          /* port: was ESC TO QUIT */
}

/* the decorative knight, wings beating, on the middle ledge (port: at
   twice the size, Suzy-scaled) */
static SCB_REHV_PAL deco;
#define DECO_X 70
#define DECO_Y (56 - 2 * KNIGHT_H)
static void draw_title_knight(void)
{
    uint8_t f = (uint8_t)gs.frame;
    gfx_rect(DECO_X, DECO_Y, 2 * KNIGHT_W, 2 * KNIGHT_H, COL_BG);
    deco.data = (unsigned char *)knight_frames[(((f >> 3) & 1) << 1) | ((f >> 4) & 1)];
    spr_draw(&deco);
}

static void draw_wave_intro(void)
{
    draw_platforms();
    cat2("WAVE ", wave_str);
    draw_big_centre(30, COL_SCORE, line, 2);
    draw_big_centre(46, COL_WHITE, "GET READY!", 1);
    draw_hud();
}

static void draw_gameover(void)
{
    draw_big_centre(28, COL_P2_KNIGHT, "GAME OVER", 2);
    cat2("P1 SCORE: ", score_short());
    text_small_centre(48, COL_SCORE, line);
    cat2("WAVE REACHED: ", wave_str);
    text_small_centre(64, COL_WHITE, line);
    text_small_centre(80, COL_LTGRAY, "RETURNING TO TITLE...");
}

/* ------------------------------------------------------------ main */

static uint8_t paused;
static uint16_t fps_vbl, fps_loops, late, last_vbl;
static uint8_t busy_max;

static void fps_reset(void)
{
    fps_vbl = frame_count;
    fps_loops = late = 0;
    busy_max = 0;
}

int main(void)
{
    uint8_t prev_state, prev_lives, shown_wave = 0, startup = 30, busy, in1, start;
    uint16_t now;
    uint8_t i, ev, kind, last_kind = 255, from_kind = 255, redraw = 0;
#ifndef LYNX_NO_LOG
    uint8_t en_before;
#endif

    gfx_init();
    gfx_set_rate(50);                       /* the original's PAL frame rate */
    gfx_palette(palette);
    snd_init();
    sprites_init();
    spr_box(&ground_plat[0], 0, GROUND_LINE, 160, 1, COL_PLATFORM);
    spr_box(&ground_plat[1], 0, GROUND_LINE + 1, 160, 2, COL_PLAT_SHADOW);
    spr_link(&ground_plat[0], &ground_plat[1]);
    hud_init();
    spr_init(&deco, knight_frames[0], 3);
    knight_pens(&deco, COL_P1_MOUNT, COL_P1_KNIGHT);
    spr_pos(&deco, DECO_X, DECO_Y);
    deco.hsize = deco.vsize = 0x200;
    game_init();
    make_wave_str();
    LOG("sk: ready hz=50\n");
    LOG("sk: title\n");

    for (;;) {
        input_update();
        in1 = 0;
        if (input_held & IN_LEFT) in1 |= INP_LEFT;
        if (input_held & IN_RIGHT) in1 |= INP_RIGHT;
        if (input_held & (IN_A | IN_B | IN_UP)) in1 |= INP_FLAP;
        start = (input_new & (IN_A | IN_B)) != 0;
        if (startup) { --startup; in1 = 0; start = 0; }  /* the original ignored input for 30 frames */

        if ((gs.state == STATE_PLAYING || gs.state == STATE_WAVE_INTRO) && (input_new & IN_PAUSE)) {
            paused ^= 1;
            if (paused) { snd_all_off(); memset(ch_timer, 0, 4); melody_timer = flap_beat = 0; }
            fps_reset();
            LOG("sk: paused=%u\n", (unsigned)paused);
        }
        if (paused && (input_new & IN_OPT1)) {
            /* the Lynx way: PAUSE, then OPTION 1 = restart (here: the title) */
            paused = 0;
            game_init();
            LOG("sk: title\n");
        }

        if (!paused) {
            prev_state = (uint8_t)gs.state;
            prev_lives = (uint8_t)p_lives[0];
            if (gs.state == STATE_TITLE) {
                ++gs.frame;
                if (start) {
                    /* port: the original's F1 (one player); no second joypad */
                    gs.num_players = 1;
                    game_start();
                    LOG("sk: start players=1\n");
                }
            } else {
#ifndef LYNX_NO_LOG
                en_before = count_active(e_active);
#endif
                game_update(in1, 0);
#ifndef LYNX_NO_LOG
                /* a pod cracked open: a new enemy in play (not a new wave) */
                if (gs.state == STATE_PLAYING && prev_state == STATE_PLAYING &&
                    count_active(e_active) > en_before) {
                    logb("sk: hatch enemies=", count_active(e_active));
                    lynx_log_c('\n');
                }
#endif
            }
            if ((uint8_t)gs.wave != shown_wave) { shown_wave = (uint8_t)gs.wave; make_wave_str(); hud_update(); }

            ev = (uint8_t)gs.ev_flags;
            gs.ev_flags = 0;
            if ((ev & ~EV_FLAP) || gs.state != prev_state) hud_update();
            if (ev & EV_FLAP) sound_flap();
            if (ev & EV_KILL) sound_kill();
            if (ev & EV_EGG)  sound_egg();
            if (ev & EV_DIE)  sound_die();
            if (ev & EV_WAVE) sound_wave();
            sound_update();

            if (ev & (EV_KILL | EV_EGG)) {
                /* one line a frame: each joust won (its points), the pods */
                lynx_log_s("sk: score");
                for (i = 0; i < ev_nkill; ++i) {
                    lynx_log_s(" kill=");
                    lynx_log_s(ev_kill_pts[i] == SCORE_SWOOPER ? "500" :
                               ev_kill_pts[i] == SCORE_RAIDER ? "750" : "1000");
                }
                if (ev_npod) logb(" pods=", ev_npod);
                log_score();
                logb(" left=", (uint8_t)(count_active(e_active) + count_active(g_active)));
                lynx_log_c('\n');
            }
            ev_nkill = ev_npod = 0;
            if ((uint8_t)p_lives[0] != prev_lives && gs.state != STATE_TITLE) {
                logb("sk: died lives=", (uint8_t)p_lives[0]);
                lynx_log_c('\n');
            }
            if (gs.state != prev_state) {
                switch (gs.state) {
                case STATE_PLAYING:
                    logb("sk: wave=", (uint8_t)gs.wave);
                    logb(" enemies=", count_active(e_active));
                    lynx_log_c('\n');
                    fps_reset();
                    break;
                case STATE_WAVE_INTRO:
                    if (prev_state == STATE_PLAYING) {
                        logb("sk: waveclear wave=", (uint8_t)(gs.wave - 1));
                        log_score();
                        lynx_log_c('\n');
                    }
                    break;
                case STATE_GAMEOVER:
                    lynx_log_s("sk: gameover");
                    log_score();
                    logb(" wave=", (uint8_t)gs.wave);
                    lynx_log_c('\n');
                    break;
                case STATE_TITLE:
                    LOG("sk: title\n");
                    break;
                }
            }
        }

        /* --- draw --- port (speed): the title, wave and game-over screens
           don't move (but the title's knight), so they are drawn into both
           buffers when they come up; play is redrawn every frame */
        kind = paused ? 9 : (uint8_t)gs.state;
        if (kind != last_kind) { redraw = 2; from_kind = last_kind; last_kind = kind; }
        switch (gs.state) {
        case STATE_TITLE:
            if (redraw) { gfx_clear(COL_BG); draw_title(); }
            draw_title_knight();
            break;
        case STATE_WAVE_INTRO:
            if (redraw) { gfx_clear(COL_BG); draw_wave_intro(); }
            break;
        case STATE_PLAYING:
            if (redraw) {
                /* from the wave intro (the same ledges and status line): only
                   its message goes (port (speed): the wave's first frame also
                   places its eight enemies; a full clear made it late) */
                if (from_kind == STATE_WAVE_INTRO) gfx_rect(16, 30, 128, 24, COL_BG);
                else gfx_clear(COL_BG);
                spr_draw(ground);
            } else if (!paused) {
                erase_pass();
                if (hud_dirty) { gfx_rect(0, 0, 160, 9, COL_BG); --hud_dirty; }
            }
            if (redraw || !paused) {
                sprites_update();
                draw_play();
                draw_hud();
            }
            break;
        case STATE_GAMEOVER:
            if (redraw) { gfx_clear(COL_BG); draw_gameover(); }
            break;
        }
        if (paused && redraw) draw_big_centre(44, COL_SCORE, "PAUSED", 1);
        if (redraw) --redraw;

        busy = (uint8_t)(MIKEY.timer2.reload - MIKEY.timer2.count);
        if (busy > busy_max) busy_max = busy;

        gfx_present();
        cur ^= 1;
        cs = sets[cur];

        /* frame rate: loops per vertical blank, logged every 2 s in play */
        now = frame_count;
        if ((uint16_t)(now - last_vbl) > 1) late += (uint16_t)(now - last_vbl) - 1;
        last_vbl = now;
        ++fps_loops;
        if ((uint16_t)(now - fps_vbl) >= 100) {
            if (gs.state == STATE_PLAYING && !paused) {
                logb("sk: fps=", (uint8_t)(fps_loops * 50u / (uint16_t)(now - fps_vbl)));
                logb(" late=", (uint8_t)late);
                logb(" busy=", busy_max);
                logb("/", MIKEY.timer2.reload + 1);
                logb(" en=", count_active(e_active));
                logb(" pods=", count_active(g_active));
                lynx_log_c('\n');
            }
            fps_reset();
        }
    }
}
