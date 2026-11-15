/*
 * Lunar Rider (Atari Lynx). Port of amiga_mcp/examples/lunar_rider.
 *
 * game.c is the original's logic; this file is the Lynx front end (the
 * original's main.c, draw.c and sound.c): one chain of Suzy sprites a frame
 * for the stars, the two parallax mountain ranges, the ground and its
 * craters, hills, rocks and mines, the bombs, UFOs, meteors, bullets, the
 * buggy, the explosions and the HUD; the title, checkpoint and game-over
 * screens in the original's 5x7 font (Suzy-scaled); Mikey sound for the
 * original's effects and its bouncy bass; the log lines the test reads.
 * It runs at the original's 50 Hz, one update a frame.
 *
 * The view: the original's 320 x 256 screen at half width and 2/5 height
 * (Lynx x = x / 2, Lynx y = y * 2 / 5: the ground line at y 200 is Lynx
 * line 80). The rules keep the original's coordinates.
 */
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "art.h"

#pragma static-locals (on)

/* The original's palette (0xRGB), index for index. Black (0) is drawn
   with background-type sprites, Suzy's normal sprites skip colour 0. */
static const uint16_t palette[16] = {
    0x001, 0xFFF, 0x214, 0x426, 0x840, 0x520, 0xFD0, 0xAAA,
    0xF00, 0xF80, 0x0EF, 0x0F0, 0x44F, 0xFF0, 0x555, 0xF8F,
};

/* the original's y -> Lynx line: y * 2 / 5 (rounded down), for
   y = -32..287 (objs.s) */
int8_t ytab[320];

/* ------------------------------------------------------------ sprites */

#define N_STAR  30
#define N_FEAT  32          /* objs.s: feature i uses SCB i & 31 */
#define N_HUDG  32          /* HUD glyphs */

/* port (speed): no clear (a full-screen Suzy fill took 22,000 cycles a
   frame): the screen is covered once by its bands, top to bottom: the sky
   (black box), the far mountains (a background-type strip: black between
   the peaks; the lines above them black), their solid foot, the near
   range's foot, the gap (black),
   the ground, the bottom (black). The near peaks, stars and everything
   else are drawn over them. */
static SCB_REHV_PAL s_sky;
static SCB_REHV_PAL s_star[N_STAR];
static SCB_REHV_PAL s_far_box, s_far[2], s_near_box, s_near[2];
static SCB_REHV_PAL s_gap, s_ground, s_dirt, s_bottom, s_hudbg, s_band;
/* port (speed): the sky (32 lines) isn't redrawn every frame either: each
   screen buffer has a set of black boxes over what was drawn in its sky
   two frames ago (the bombs, enemies, upward bullets, explosions:
   objs.s erase_save), drawn first; the stars and the HUD are drawn every
   frame after them. The whole sky is drawn when text was over it
   (sky_full frames, one per buffer; the lines under the ground, y
   232..255, only need painting black then too), the HUD's lines when the
   score, lives or zone changed (hud_dirty). */
#define N_ERS 24
static SCB_REHV_PAL ers[2][N_ERS];
SCB_REHV_PAL *ers_set;                  /* objs.s: the set erase_save fills */
void erase_save(void);
static uint8_t cur, sky_full, hud_dirty;
/* objs.s writes these */
SCB_REHV_PAL s_feat[N_FEAT];
SCB_REHV_PAL feat_tmpl[4];              /* crater, rock, hill, mine */
SCB_REHV_PAL s_bomb[MAX_BOMBS];
SCB_REHV_PAL s_enemy[MAX_ENEMIES];
SCB_REHV_PAL s_bullet[MAX_FWD_BULLETS + MAX_UP_BULLETS];
SCB_REHV_PAL s_buggy;
SCB_REHV_PAL s_ex[MAX_EXPLOSIONS * 2];
void objects_update(void);
void features_update(void);
void features_reset(void);
static SCB_REHV_PAL s_hudg[N_HUDG];
static SCB_REHV_PAL s_life[5], s_bar, s_bar_fill;

static void link(void *a, void *b) { ((SCB_REHV_PAL *)a)->next = (char *)b; }

/* a black box: background type draws colour 0 (normal sprites skip it) */
static void black_box(SCB_REHV_PAL *s, int16_t x, int16_t y, uint8_t w, uint8_t h)
{
    spr_box(s, x, y, w, h, 0);
    s->sprctl0 = BPP_1 | TYPE_BACKNONCOLL;
    s->penpal[0] = 0x00;
}

/* HUD glyph slots: one SCB per character (3x5 font) */
extern const unsigned char font3x5_data[64][16];

static void hud_glyph(uint8_t i, int16_t x, int16_t y, uint8_t col)
{
    SCB_REHV_PAL *s = &s_hudg[i];
    spr_init(s, font3x5_data[0], 1);
    spr_color(s, col);
    spr_pos(s, x, y);
}

static void hud_set(uint8_t i, uint8_t n, const char *t)
{
    static SCB_REHV_PAL *s;
    static uint8_t ch;
    for (s = &s_hudg[i]; n; --n, ++s) {
        ch = (uint8_t)*t;
        if (ch) ++t;
        else ch = ' ';
        spr_hide(s, ch == ' ');
        s->data = (unsigned char *)font3x5_data[ch - 32];
    }
}

/* HUD slots */
#define H_SCORE_L 0         /* "SCORE" */
#define H_SCORE   5         /* 7 digits */
#define H_ZONE_L  12        /* "ZONE" */
#define H_ZONE    16        /* 2 digits */
#define H_HI_L    18        /* "HI" */
#define H_HI      20        /* 7 digits */
#define H_LIVES_L 27        /* "LIVES" */

static void sprites_init(void)
{
    uint8_t i;
    void *prev;

    black_box(&s_sky, 0, 0, 160, NEAR_TOP);
    for (cur = 0; cur < 2; ++cur)
        for (i = 0; i < N_ERS; ++i) {
            black_box(&ers[cur][i], 0, 0, 1, 1);
            spr_hide(&ers[cur][i], 1);
            link(&ers[cur][i], i + 1 < N_ERS ? (void *)&ers[cur][i + 1] : (void *)&s_star[0]);
        }
    cur = 0;
    black_box(&s_hudbg, 0, 0, 160, 12);
    link(&s_sky, &s_hudbg);
    link(&s_hudbg, ers[0]);
    prev = 0;
    for (i = 0; i < N_STAR; ++i) {
        spr_box(&s_star[i], star_x[i], star_y[i], 1, 1, COL_WHITE);
        if (prev) link(prev, &s_star[i]);
        prev = &s_star[i];
    }
    /* the mountains: the far peaks two 256-wide background-type strips
       (pen 0 black), the solid feet boxes, the near peaks two 256-wide
       strips over them */
    /* the lines between the sky and the far peaks, where the near peaks
       reach up: black every frame */
    black_box(&s_band, 0, NEAR_TOP, 160, FAR_TOP - NEAR_TOP);
    link(prev, &s_band); prev = &s_band;
    for (i = 0; i < 2; ++i) {
        spr_init(&s_far[i], far_data, 1);
        s_far[i].sprctl0 = BPP_1 | TYPE_BACKNONCOLL;
        spr_color(&s_far[i], COL_DKPURPLE);
        s_far[i].vpos = FAR_TOP;
        link(prev, &s_far[i]); prev = &s_far[i];
    }
    spr_box(&s_far_box, 0, FAR_SOLID, 160, NEAR_SOLID - FAR_SOLID, COL_DKPURPLE);
    link(prev, &s_far_box); prev = &s_far_box;
    spr_box(&s_near_box, 0, NEAR_SOLID, 160, NEAR_BOTTOM - NEAR_SOLID + 1, COL_PURPLE);
    link(prev, &s_near_box); prev = &s_near_box;
    black_box(&s_gap, 0, NEAR_BOTTOM + 1, 160, 80 - NEAR_BOTTOM - 1);
    link(prev, &s_gap); prev = &s_gap;
    for (i = 0; i < 2; ++i) {
        spr_init(&s_near[i], near_data, 1);
        spr_color(&s_near[i], COL_PURPLE);
        s_near[i].vpos = NEAR_TOP;
        link(prev, &s_near[i]); prev = &s_near[i];
    }
    /* the ground: the brown surface (y 200..202) and the dark brown below
       it down to y 231; black below */
    spr_box(&s_ground, 0, 80, 160, 1, COL_BROWN);
    link(prev, &s_ground); prev = &s_ground;
    spr_box(&s_dirt, 0, 81, 160, 12, COL_DKBROWN);
    link(prev, &s_dirt); prev = &s_dirt;
    black_box(&s_bottom, 0, 93, 160, 9);
    link(prev, &s_bottom); prev = &s_bottom;
    /* the terrain's features: templates objs.s copies, one SCB each */
    black_box(&feat_tmpl[0], 0, 80, 1, 7);              /* a crater: black, y 200..215 */
    spr_init(&feat_tmpl[1], rock_data, 4);              /* rock: y 190..199 */
    feat_tmpl[1].vpos = 76;
    spr_init(&feat_tmpl[2], hill_frames[11], 4);
    feat_tmpl[2].vpos = HILL_TOP;
    spr_init(&feat_tmpl[3], mine_data, 4);              /* mine: y 196..199 */
    feat_tmpl[3].vpos = 78;
    for (i = 0; i < N_FEAT; ++i) {
        spr_box(&s_feat[i], 0, 0, 1, 1, COL_RED);
        spr_hide(&s_feat[i], 1);
        link(prev, &s_feat[i]); prev = &s_feat[i];
    }
    for (i = 0; i < MAX_BOMBS; ++i) {
        spr_init(&s_bomb[i], bomb_data, 4);
        spr_hide(&s_bomb[i], 1);
        link(prev, &s_bomb[i]); prev = &s_bomb[i];
    }
    for (i = 0; i < MAX_ENEMIES; ++i) {
        spr_init(&s_enemy[i], meteor_data, 4);
        spr_hide(&s_enemy[i], 1);
        link(prev, &s_enemy[i]); prev = &s_enemy[i];
    }
    for (i = 0; i < 4; ++i) {
        if (i < MAX_FWD_BULLETS) spr_box(&s_bullet[i], 0, 0, 2, 1, COL_ORANGE);
        else spr_box(&s_bullet[i], 0, 0, 1, 2, COL_ORANGE);
        spr_hide(&s_bullet[i], 1);
        link(prev, &s_bullet[i]); prev = &s_bullet[i];
    }
    spr_init(&s_buggy, buggy_frames[0], 4);
    s_buggy.hpos = BUGGY_X / 2;
    link(prev, &s_buggy); prev = &s_buggy;
    for (i = 0; i < MAX_EXPLOSIONS * 2; ++i) {
        spr_box(&s_ex[i], 0, 0, 1, 1, COL_WHITE);
        spr_hide(&s_ex[i], 1);
        link(prev, &s_ex[i]); prev = &s_ex[i];
    }
    /* HUD (draw_hud): SCORE, ZONE nn, HI on the first line, LIVES and the
       checkpoint bar on the second */
    for (i = 0; i < 5; ++i) hud_glyph(H_SCORE_L + i, 2 + 4 * i, 1, COL_BRYELLOW);
    for (i = 0; i < 7; ++i) hud_glyph(H_SCORE + i, 24 + 4 * i, 1, COL_BRYELLOW);
    for (i = 0; i < 4; ++i) hud_glyph(H_ZONE_L + i, 66 + 4 * i, 1, COL_GREEN);
    for (i = 0; i < 2; ++i) hud_glyph(H_ZONE + i, 86 + 4 * i, 1, COL_GREEN);
    for (i = 0; i < 2; ++i) hud_glyph(H_HI_L + i, 122 + 4 * i, 1, COL_WHITE);
    for (i = 0; i < 7; ++i) hud_glyph(H_HI + i, 132 + 4 * i, 1, COL_WHITE);
    for (i = 0; i < 5; ++i) hud_glyph(H_LIVES_L + i, 2 + 4 * i, 7, COL_WHITE);
    hud_set(H_SCORE_L, 5, "SCORE");
    hud_set(H_ZONE_L, 4, "ZONE");
    hud_set(H_HI_L, 2, "HI");
    hud_set(H_LIVES_L, 5, "LIVES");
    for (i = 0; i < N_HUDG; ++i) { link(prev, &s_hudg[i]); prev = &s_hudg[i]; }
    for (i = 0; i < 5; ++i) {
        spr_box(&s_life[i], 24 + 5 * i, 8, 4, 3, COL_YELLOW);
        link(prev, &s_life[i]); prev = &s_life[i];
    }
    spr_box(&s_bar, 65, 8, 31, 2, COL_DKGRAY);
    link(prev, &s_bar);
    spr_box(&s_bar_fill, 65, 8, 1, 2, COL_GREEN);
    link(&s_bar, &s_bar_fill);
}

/* score_h as the original's long_to_str(score). port (speed): kept as
   decimal digits (of hundreds) that the score's steps are added to (cc65's
   divisions by 10 cost thousands of cycles) */
static char score_str[8];
static uint8_t sdig[5];                 /* score_h's decimal digits, units first */
static uint16_t sdig_val;               /* the score they hold */

static void make_score_str(void)
{
    static uint8_t n, k;
    if (score_h < sdig_val) { memset(sdig, 0, sizeof sdig); sdig_val = 0; }
    while (sdig_val != score_h) {
        ++sdig_val;
        for (k = 0; k < 5 && ++sdig[k] == 10; ++k) sdig[k] = 0;
    }
    if (!sdig_val) { score_str[0] = '0'; score_str[1] = 0; return; }
    n = 5;
    while (!sdig[n - 1]) --n;
    for (k = 0; n; ) score_str[k++] = '0' + sdig[--n];
    score_str[k++] = '0';
    score_str[k++] = '0';
    score_str[k] = 0;
}

static uint16_t hud_score = 0xFFFF;
static uint8_t hud_lives = 0xFF, hud_zone = 0xFF, hud_bar = 0xFF;

/* returns 1 when the score, lives or zone changed */
static uint8_t hud_update(void)
{
    static char z[3];
    static uint8_t i, p, ch;
    static int16_t d;
    ch = 0;
    if (score_h != hud_score) {
        ch = 1;
        hud_score = score_h;
        make_score_str();
        hud_set(H_SCORE, 7, score_str);
        /* port: as the original, HI shows the current score (its
           "placeholder"; it keeps no high score) */
        hud_set(H_HI, 7, score_str);
    }
    if (lives != hud_lives) {
        ch = 1;
        hud_lives = lives;
        for (i = 0; i < 5; ++i) spr_hide(&s_life[i], i >= lives);
    }
    if (checkpoint_cur != hud_zone) {
        ch = 1;
        hud_zone = checkpoint_cur;
        p = checkpoint_cur + 1;
        z[0] = '0' + p / 10;
        z[1] = '0' + p % 10;
        z[2] = 0;
        hud_set(H_ZONE, 2, z);
    }
    /* the checkpoint bar: 60 pixels in the original, 30 here (+1: its
       RectFill includes both ends) */
    d = checkpoint_dist;
    if (d < 0) d = 0;
    p = (uint8_t)(30 - (uint8_t)(((uint16_t)d * 3) / 50));
    if (p > 30) p = 30;
    if (p != hud_bar) {
        hud_bar = p;
        spr_hide(&s_bar_fill, p == 0);
        s_bar_fill.hsize = (uint16_t)(p + 1) << 8;
    }
    return ch;
}

/* the mountains' scroll: the original moved the far range at 1/4 and the
   near range at 1/2 of the ground's speed (scroll_x / 400 and / 200).
   port: it wrapped those offsets at 640, which made the ranges jump every
   2560 pixels; here they scroll on (by Lynx pixels: 1/8 and 1/4 of
   scroll_x / 100). */
static uint8_t far_off, near_off;       /* Lynx pixels, mod 256 (the strips' period) */
static uint8_t far_acc, near_acc;       /* world pixels towards the next Lynx pixel */
static uint16_t mount_px;               /* scroll_px at the last update */

static void mountains_update(void)
{
    static uint8_t d;
    static int16_t x;
    d = (uint8_t)(scroll_px - mount_px);
    mount_px = scroll_px;
    far_acc += d;
    while (far_acc >= 8) { far_acc -= 8; ++far_off; }
    near_acc += d;
    while (near_acc >= 4) { near_acc -= 4; ++near_off; }
    x = -(int16_t)far_off;
    s_far[0].hpos = x;
    s_far[1].hpos = x + 256;
    x = -(int16_t)near_off;
    s_near[0].hpos = x;
    s_near[1].hpos = x + 256;
}

/* ------------------------------------------------------------ text */

/* The original's 5x7 font as Suzy sprites, scaled 1x, 2x: a line of text
   is a chain of glyph SCBs. port (speed): setting them up costs ~2,000
   cycles a glyph in C, so the lines shown during play (checkpoint, game
   over, pause) and the title's prompt are built once and then only drawn. */
typedef struct {
    SCB_REHV_PAL *buf;
    uint8_t n, max;
} TextChain;

/* append a line centred at y (string_pixel_width: len * 6 * scale - scale) */
static void text_add(TextChain *c, uint8_t y, uint8_t col, const char *t, uint8_t scale)
{
    SCB_REHV_PAL *s;
    int16_t x = (int16_t)(160 - (strlen(t) * 6 - 1) * scale) / 2;
    char ch;
    uint8_t g;
    static SCB_REHV_PAL proto;
    spr_init(&proto, glyph_frames[0], 1);
    spr_color(&proto, col);
    proto.vpos = y;
    proto.hsize = proto.vsize = (uint16_t)scale << 8;
    for (; (ch = *t) != 0 && c->n < c->max; ++t, x += 6 * scale) {
        if (ch >= '0' && ch <= '9') g = ch - '0';
        else if (ch >= 'A' && ch <= 'Z') g = ch - 'A' + 10;
        else continue;
        s = &c->buf[c->n];
        memcpy(s, &proto, sizeof proto);
        s->data = (unsigned char *)glyph_frames[g];
        s->hpos = x;
        if (c->n) spr_link(s - 1, s);
        ++c->n;
    }
}

static void text_draw(TextChain *c)
{
    if (c->n) spr_draw(c->buf);
}

static SCB_REHV_PAL ov_buf[20], ovc_buf[16], pz_buf[6], tp_buf[16], tt_buf[24];
static TextChain ov = { ov_buf, 0, 20 };        /* game over */
static TextChain ovc = { ovc_buf, 0, 16 };      /* checkpoint */
static TextChain pz = { pz_buf, 0, 6 };         /* PAUSED */
static TextChain tp = { tp_buf, 0, 16 };        /* the title's prompt */
static TextChain tt = { tt_buf, 0, 24 };        /* the title's other lines */

static void draw_big_centre(uint8_t y, uint8_t col, const char *t, uint8_t scale)
{
    tt.n = 0;
    text_add(&tt, y, col, t, scale);
    text_draw(&tt);
}

/* ------------------------------------------------------------ screens */

static void draw_stars_only(void)
{
    s_sky.vsize = 102 << 8;
    spr_hide(&s_sky, 0);
    s_star[N_STAR - 1].next = 0;
    spr_draw(&s_sky);
    s_star[N_STAR - 1].next = (char *)&s_band;
    s_sky.vsize = NEAR_TOP << 8;
}

static SCB_REHV_PAL title_buggy, title_ground, title_prompt_bg;
static uint8_t title_full;              /* frames left to draw the whole title (one per buffer) */

/* port (speed): the title is static but for its blinking prompt; it is
   drawn whole into both screen buffers, then only the prompt's line is
   redrawn (drawn whole every frame it took four frames a loop) */
static void draw_title(void)
{
    if (title_full) {
        --title_full;
        draw_stars_only();
        draw_big_centre(6, COL_CYAN, "LUNAR RIDER", 2);
        draw_big_centre(24, COL_PURPLE, "LUNAR DEFENSE FORCE", 1);
        spr_draw(&title_buggy);
        draw_big_centre(49, COL_WHITE, "CONTROLS", 1);
        /* port: the Lynx's controls (the original listed keys and port 2) */
        text_small(30, 59, COL_GREEN, "LEFT/RIGHT  SPEED");
        text_small(30, 65, COL_GREEN, "UP OR B     JUMP");
        text_small(30, 71, COL_GREEN, "A           SHOOT");
        text_small(30, 77, COL_GREEN, "PAUSE       PAUSE");
    } else {
        spr_draw(&title_prompt_bg);
    }
    if ((frame / 20) & 1) text_draw(&tp);
}

/* draw_checkpoint: "ZONE nn" and "1000 BONUS", built once; a checkpoint
   only sets the zone's two digits (ovc.buf[4], [5]) */
static void build_checkpoint(void)
{
    static uint8_t z;
    z = checkpoint_cur + 1;
    ovc.buf[4].data = (unsigned char *)glyph_frames[z / 10];
    ovc.buf[5].data = (unsigned char *)glyph_frames[z % 10];
}

/* draw_gameover: built when the game ends */
static void build_gameover(void)
{
    ov.n = 0;
    text_add(&ov, 34, COL_RED, "GAME OVER", 2);
    text_add(&ov, 53, COL_BRYELLOW, score_str, 2);
}

/* ------------------------------------------------------------ sound */

/* Mikey channels as the original's Paula channels: 0 shots, 1 explosions
   and death, 2 jump and checkpoint, 3 the bass. port: the original looped
   short sampled waveforms; Mikey plays square waves and noise at their
   pitches (a Paula period p over an n-sample cycle is 3546895 / (p n) Hz),
   for the same number of frames, at twice their volume (Mikey 0..127,
   Paula 0..64). */
#define PAULA(p, n) (3546895UL / ((unsigned long)(p) * (n)))
#pragma warn (const-comparison, push, off)
static const uint16_t bass_period[16] = {   /* C3 C3 G3 G3 A3 A3 F3 F3 E3 E3 G3 G3 C3 D3 E3 F3 */
    SND_PERIOD(PAULA(428, 64)), SND_PERIOD(PAULA(428, 64)), SND_PERIOD(PAULA(285, 64)), SND_PERIOD(PAULA(285, 64)),
    SND_PERIOD(PAULA(254, 64)), SND_PERIOD(PAULA(254, 64)), SND_PERIOD(PAULA(320, 64)), SND_PERIOD(PAULA(320, 64)),
    SND_PERIOD(PAULA(339, 64)), SND_PERIOD(PAULA(339, 64)), SND_PERIOD(PAULA(285, 64)), SND_PERIOD(PAULA(285, 64)),
    SND_PERIOD(PAULA(428, 64)), SND_PERIOD(PAULA(381, 64)), SND_PERIOD(PAULA(339, 64)), SND_PERIOD(PAULA(320, 64)),
};
/* the checkpoint's rising arpeggio: 20, 16, 12, 10-sample squares at period 200 */
static const uint16_t chk_period[4] = {
    SND_PERIOD(PAULA(200, 20)), SND_PERIOD(PAULA(200, 16)), SND_PERIOD(PAULA(200, 12)), SND_PERIOD(PAULA(200, 10)),
};
static const uint16_t shoot_period = SND_PERIOD(PAULA(180, 6));
#pragma warn (const-comparison, pop)
#define BASS_TEMPO 7

static uint8_t sfx_timer[3];
static uint8_t bass_pos, bass_timer, chk_step;

static void sound_shoot(void)      { snd_tone_p(0, shoot_period, 80, 24); sfx_timer[0] = 3; }
static void sound_explode(void)    { snd_noise(1, 2, 110, 10); sfx_timer[1] = 10; }
static void sound_death(void)      { snd_noise(1, 5, 110, 7); sfx_timer[1] = 15; }
/* the jump: a square sweeping up from ~590 to ~1970 Hz */
static void sound_jump(void)       { snd_tone(2, 591, 90, 8); snd_sweep(2, 172); sfx_timer[2] = 8; chk_step = 0; }
/* port: the arpeggio's four notes in turn over its 20 frames (the
   original looped all four every 29 ms) */
static void sound_checkpoint(void) { snd_tone_p(2, chk_period[0], 100, 0); sfx_timer[2] = 20; chk_step = 20; }

static void sound_update(void)
{
    static uint8_t i;
    for (i = 0; i < 3; ++i)
        if (sfx_timer[i] && --sfx_timer[i] == 0) snd_off(i);
    if (chk_step) {
        --chk_step;
        if (chk_step && (chk_step % 5) == 0) snd_freq_p(2, chk_period[(20 - chk_step) / 5]);
    }
    /* Music sequencer: bouncy bass on channel 3 */
    if (bass_timer == 0 || --bass_timer == 0) {
        bass_timer = BASS_TEMPO;
        snd_tone_p(3, bass_period[bass_pos], 60, 0);
        bass_pos = (bass_pos + 1) & 15;
    }
}

/* ------------------------------------------------------------ log */

#ifndef LYNX_NO_LOG
#define LOG(...) lynx_log(__VA_ARGS__)
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
    lynx_log_u(score_h);
    if (score_h) lynx_log_s("00");
}
static const char * const cause_name[6] = { "?", "crater", "rock", "mine", "bomb", "enemy" };
static const char * const why_name[14] = {
    "?", "checkpoint", "?", "rock", "?", "?", "?", "?", "?", "?", "?", "ufo_sm", "ufo_lg", "meteor" };
#else
#define LOG(...) ((void)0)
#define logb(k, v) ((void)0)
#define log_score() ((void)0)
#endif

/* ------------------------------------------------------------ main */

static uint8_t paused;
static uint16_t fps_vbl, fps_loops, late, last_vbl;
static uint8_t busy_max;

int main(void)
{
    static uint8_t prev_state, prev_lives, startup, busy, inp, start;
    static uint16_t prev_score, now, held, nw, k;

    for (k = 0; k < 320; ++k) ytab[k] = (int8_t)((k * 2 + 1) / 5 - 13);   /* (k - 32) * 2 / 5 rounded down */

    gfx_init();
    gfx_set_rate(50);                       /* the original's PAL frame rate */
    gfx_palette(palette);
    snd_init();
    sprites_init();
    spr_init(&title_buggy, buggy_frames[0], 4);
    spr_pos(&title_buggy, 67, 31);
    spr_scale(&title_buggy, SPR_SCALE_2, SPR_SCALE_2);   /* Suzy's scaling */
    spr_box(&title_ground, 0, 45, 160, 1, COL_BROWN);
    spr_link(&title_buggy, &title_ground);
    black_box(&title_prompt_bg, 0, 88, 160, 7);
    text_add(&tp, 88, COL_BRYELLOW, "PRESS A TO START", 1);
    text_add(&pz, 40, COL_BRYELLOW, "PAUSED", 1);
    text_add(&ovc, 12, COL_GREEN, "ZONE 00", 2);
    text_add(&ovc, 29, COL_BRYELLOW, "1000 BONUS", 1);
    title_full = 2;
    game_init();
    startup = 30;                           /* the original ignored input for 30 frames */
    LOG("lr: ready hz=50\n");
    LOG("lr: title\n");

    for (;;) {
        input_update();
        held = input_held;
        nw = input_new;
        if (startup) { --startup; held = nw = 0; }
        inp = 0;
        if (held & IN_LEFT) inp |= INP_LEFT;
        if (held & IN_RIGHT) inp |= INP_RIGHT;
        if (held & (IN_UP | IN_B)) inp |= INP_JUMP;
        if (held & IN_A) inp |= INP_FIRE;
        /* port: original bug - the original left the title and GAME OVER
           while fire or up was held, so a player still holding fire when
           the last life went never saw GAME OVER (and started a new game
           at once); here they take a new press */
        start = (nw & (IN_A | IN_B | IN_UP)) != 0;

        if ((state == STATE_PLAYING || state == STATE_CHECKPOINT || state == STATE_DYING) &&
            (nw & IN_PAUSE)) {
            paused ^= 1;
            snd_all_off();
            memset(sfx_timer, 0, sizeof sfx_timer);
            chk_step = 0;
            fps_vbl = frame_count;
            fps_loops = late = 0;
            LOG("lr: paused=%u\n", (unsigned)paused);
        }
        if (paused && (nw & IN_OPT1)) {
            /* the Lynx way: PAUSE, then OPTION 1 = give up (the original's ESC) */
            paused = 0;
            state = STATE_TITLE;
            title_full = 2;
            LOG("lr: title\n");
        }

        if (!paused) {
            prev_state = state;
            prev_lives = lives;
            prev_score = score_h;
            switch (state) {
            case STATE_TITLE:
                ++frame;
                if (start) {
                    game_init();
                    state = STATE_PLAYING;
                }
                break;
            case STATE_PLAYING:
            case STATE_CHECKPOINT:
                game_update(inp);
                if (ev_shoot) sound_shoot();
                if (ev_explode) sound_explode();
                if (ev_jump) sound_jump();
                if (ev_checkpoint) sound_checkpoint();
                if (ev_death) sound_death();
                break;
            case STATE_DYING:
                game_update(inp);
                if (ev_explode) sound_explode();
                break;
            case STATE_GAMEOVER:
                game_update(inp);
                if (start) state = STATE_TITLE;
                break;
            }
            sound_update();

            if (ev_jump) lynx_log_s("lr: jump\n");
            if (score_h != prev_score) {
                lynx_log_s("lr: score+");
                logb("", (uint8_t)(score_h - prev_score));
                lynx_log_s("00 why=");
                lynx_log_s(why_name[score_why]);
                log_score();
                lynx_log_c('\n');
            }
            if (ev_death) {
                lynx_log_s("lr: death cause=");
                lynx_log_s(cause_name[death_cause]);
                logb(" lives=", lives);
                logb(" zone=", checkpoint_cur + 1);
                lynx_log_c('\n');
            }
            if (ev_checkpoint) {
                build_checkpoint();
                logb("lr: checkpoint zone=", checkpoint_cur + 1);
                logb(" difficulty=", difficulty);
                log_score();
                lynx_log_c('\n');
            }
            if (lives != prev_lives && state == STATE_PLAYING) {
                logb("lr: respawn lives=", lives);
                lynx_log_c('\n');
            }
            if (state != prev_state) {
                switch (state) {
                case STATE_PLAYING:
                    if (prev_state == STATE_TITLE) {
                        LOG("lr: state=playing lives=%u\n", (unsigned)lives);
                        fps_vbl = frame_count;
                        fps_loops = late = 0;
                        busy_max = 0;
                        features_reset();
                        mount_px = 0;
                        sky_full = 2;
                    }
                    break;
                case STATE_GAMEOVER:
                    make_score_str();
                    build_gameover();
                    lynx_log_s("lr: gameover");
                    log_score();
                    logb(" zone=", checkpoint_cur + 1);
                    lynx_log_c('\n');
                    break;
                case STATE_TITLE:
                    title_full = 2;
                    LOG("lr: title\n");
                    break;
                }
            }
        }

        /* --- draw (draw_game: back to front, one chain) --- */
        if (state == STATE_TITLE) {
            draw_title();
        } else {
            mountains_update();
            features_update();
            objects_update();
            if (state == STATE_CHECKPOINT || state == STATE_GAMEOVER || paused) sky_full = 3;
            if (hud_update()) hud_dirty = 2;
            spr_hide(&s_sky, !sky_full);
            spr_hide(&s_bottom, !sky_full);
            spr_hide(&s_hudbg, !hud_dirty || sky_full);
            if (sky_full) --sky_full;
            if (hud_dirty) --hud_dirty;
            s_hudbg.next = (char *)ers[cur];
            spr_draw(&s_sky);
            if (state == STATE_CHECKPOINT) text_draw(&ovc);
            if (state == STATE_GAMEOVER) text_draw(&ov);
            if (paused) text_draw(&pz);
            ers_set = ers[cur];
            erase_save();
            cur ^= 1;
        }

        busy = (uint8_t)(MIKEY.timer2.reload - MIKEY.timer2.count);
        if (busy > busy_max) busy_max = busy;

        gfx_present();

        /* frame rate: loops per vertical blank, logged every 2 s in play */
        now = frame_count;
        if ((uint16_t)(now - last_vbl) > 1) late += (uint16_t)(now - last_vbl) - 1;
        last_vbl = now;
        ++fps_loops;
        if ((uint16_t)(now - fps_vbl) >= 100) {
            if ((state == STATE_PLAYING || state == STATE_CHECKPOINT) && !paused) {
                logb("lr: fps=", (uint8_t)(fps_loops * 50u / (uint16_t)(now - fps_vbl)));
                logb(" late=", (uint8_t)late);
                logb(" busy=", busy_max);
                logb("/", MIKEY.timer2.reload + 1);
                lynx_log_c('\n');
            }
            fps_vbl = now;
            fps_loops = late = 0;
            busy_max = 0;
        }
    }
}
