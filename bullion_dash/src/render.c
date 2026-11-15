/*
 * Bullion Dash - the Lynx screen (see render.h). Replaces the original's
 * render.c: the same tiles, people, status line, title and message boxes,
 * drawn at half width and 6/14 height with Suzy sprites.
 *
 * port (speed): the original redraws the whole screen every frame. Here a
 * full repaint (gfx_clear + every tile) happens only when a level starts;
 * otherwise each screen buffer has a list of cells to repaint before it is
 * drawn again: the cells the people covered when this buffer was last drawn
 * (two frames ago) and every tile that changed since. A tile sprite paints
 * its whole 5 x 6 cell (black is pen 15, not the transparent pen 0).
 */
#pragma static-locals (on)
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "coords.h"
#include "render.h"
#include "spr_tiles.h"
#include "spr_man.h"

/* the original's palette (game.h's COL_*), pen 15 (its COL_TRAP, the
   brick colour again) is the background black that sprites draw */
#define C_BG      15
#define C_BRICK    1
#define C_BRICKHI  2
#define C_LADDER   5
#define C_GOLD     7
#define C_PLAYER   9
#define C_PLAYERHI 10
#define C_ENEMY   11
#define C_ENEMYHI 12
#define C_TEXT    13
#define C_HUDBG   14
static const uint16_t palette[16] = {
    0x000, 0xA52, 0xC73, 0x888, 0xAAA, 0x0AA, 0x088, 0xEE0,
    0xFF8, 0x0D0, 0x0F4, 0xD00, 0xF80, 0xFFF, 0x113, 0x000,
};

/* tile id -> art (hidden ladders and the editor's markers look empty) */
static const unsigned char *tile_art[NUM_TILES];
static const uint8_t tile_frame[NUM_TILES] = { 0, 1, 2, 3, 4, 5, 0, 0, 0, 1 };

#define MAN_STAND 0
#define MAN_RUN1  1
#define MAN_RUN2  2
#define MAN_CLIMB 3
#define MAN_HANG  4
#define MAN_DIG   5
#define MAN_TRAP  6

/* screen y of each original y (6/14), cell of a playfield x / y */
static uint8_t scr_y[COORD_Y_SIZE];
static uint8_t div5[144], div6[108];
static uint8_t x5[GRID_COLS], y6[GRID_ROWS];

/* ------------------------------------------------------------ cells */

#define POOL 24
static SCB_REHV_PAL pool[POOL];
static uint8_t pool_n;

static void pool_flush(void)
{
    if (!pool_n) return;
    pool[pool_n - 1].next = 0;
    spr_draw(pool);
    pool[pool_n - 1].next = (char *)&pool[pool_n];
    pool_n = 0;
}

static void pool_cell(uint8_t x, uint8_t y)
{
    SCB_REHV_PAL *s = &pool[pool_n];
    s->data = (unsigned char *)tile_art[TILE(x, y)];
    s->hpos = PF_X + x5[x];
    s->vpos = y6[y];
    if (++pool_n == POOL) pool_flush();
}

/* tiles changed since each buffer was last drawn */
#define DL_MAX 48
static uint8_t dl_x[2][DL_MAX], dl_y[2][DL_MAX], dl_n[2];
static uint8_t cur;                 /* the back buffer's list */
static uint8_t full;                /* frames of full repaint left */
static uint8_t hud_dirty;

static void dl_add(uint8_t p, uint8_t x, uint8_t y)
{
    uint8_t n = dl_n[p];
    if (n == DL_MAX) {              /* (never seen) repaint everything */
        full = 2;
        return;
    }
    dl_x[p][n] = x;
    dl_y[p][n] = y;
    dl_n[p] = n + 1;
}

/* this frame's cells to repaint: a mark per cell (== stamp) and a list */
static uint8_t mark[GRID_ROWS * GRID_COLS], stamp;
static uint8_t rp_x[64], rp_y[64], rp_n;
static uint8_t row28[GRID_ROWS];

/* mark a cell; returns 1 if it was marked already */
#define CELL_I(x, y) (row28[y] + (x))
static void rp_add(uint8_t x, uint8_t y)
{
    uint8_t *m = &mark[CELL_I(x, y)];
    if (*m == stamp || rp_n == sizeof rp_x) return;
    *m = stamp;
    rp_x[rp_n] = x;
    rp_y[rp_n] = y;
    ++rp_n;
}

void render_tile_changed(uint8_t x, uint8_t y)
{
    dl_add(0, x, y);
    dl_add(1, x, y);
}

void render_status_changed(void)
{
    hud_dirty = 2;
}

/* ------------------------------------------------------------ init */

static void people_init(void);
static void hud_init(void);

void render_init(void)
{
    uint8_t i;
    gfx_init();
    gfx_set_rate(50);               /* the original's 50 Hz */
    gfx_palette(palette);
    for (i = 0; i < NUM_TILES; ++i) tile_art[i] = tiles_frames[tile_frame[i]];
    for (i = 0; i < COORD_Y_SIZE; ++i) scr_y[i] = (uint8_t)((uint16_t)i * 3 / 7);
    for (i = 0; i < sizeof div5; ++i) div5[i] = i / 5;
    for (i = 0; i < sizeof div6; ++i) div6[i] = i / 6;
    for (i = 0; i < GRID_COLS; ++i) x5[i] = i * 5;
    for (i = 0; i < GRID_ROWS; ++i) y6[i] = i * 6, row28[i] = i * GRID_COLS;
    for (i = 0; i < POOL; ++i) {
        spr_init(&pool[i], tiles_frames[0], 4);
        if (i) spr_link(&pool[i - 1], &pool[i]);
    }
    people_init();
    hud_init();
}

/* ------------------------------------------------------------ people */

/* port (speed): the people are redrawn only when they change. Each buffer
   remembers what it shows of each one (k_*); a person who looks the same
   and sits where he did when this buffer was last drawn is already there,
   unless a cell under him is repainted. A person who changed has the cells
   under his old picture repainted, and is drawn again; so is anyone over a
   repainted cell, or under someone drawn before him (the runner stays on
   top of the guards, as in the original's drawing order). */
#define N_ENT (MAX_ENEMIES + 1)     /* guards, then the runner on top */
static SCB_REHV_PAL ent[N_ENT];
static uint8_t k_vis[2][N_ENT], k_x[2][N_ENT], k_y[2][N_ENT], k_f[2][N_ENT], k_c[2][N_ENT];
static uint8_t n_vis[N_ENT], n_x[N_ENT], n_y[N_ENT], n_f[N_ENT], n_c[N_ENT];

#define C_FLIP 0x80                 /* in n_c: hflip; low nibble: belt colour */

/* the people as they are now: n_* */
static void people_now(uint8_t entities)
{
    static uint8_t i, f, c;
    static Enemy *e;
    Player *p = &gs.player;

    for (i = 0, e = gs.enemies; i < MAX_ENEMIES; ++i, ++e) {
        n_vis[i] = 0;
        if (!entities || i >= gs.num_enemies || !e->active || e->state == ES_DEAD
            || e->py >= GRID_ROWS * TILE_H)
            continue;
        c = e->has_gold ? C_GOLD : C_ENEMY;              /* the gold he carries */
        switch (e->state) {
        case ES_TRAPPED:  f = MAN_TRAP; break;
        case ES_RUNNING:  f = (e->anim & 4) ? MAN_RUN1 : MAN_RUN2; break;
        case ES_HANGING:  f = MAN_HANG; break;
        case ES_CLIMBING: f = MAN_CLIMB; if (e->anim & 4) c |= C_FLIP; break;
        default:          f = MAN_STAND; break;
        }
        n_vis[i] = 1;
        n_x[i] = (uint8_t)((uint16_t)e->px >> 1);
        n_y[i] = scr_y[e->py];
        n_f[i] = f;
        n_c[i] = c;
    }
    /* the runner (the original: blinks while dead) */
    n_vis[i] = 0;
    if (!entities || p->py >= GRID_ROWS * TILE_H || (p->state == PS_DEAD && (gs.frame & 4)))
        return;
    c = C_PLAYER;
    switch (p->state) {
    case PS_RUNNING:  f = (p->anim & 4) ? MAN_RUN1 : MAN_RUN2; break;
    case PS_CLIMBING: f = MAN_CLIMB; if (p->anim & 4) c |= C_FLIP; break;
    case PS_HANGING:  f = MAN_HANG; break;
    case PS_DIGGING:  f = MAN_DIG; if (p->dir == DIR_LEFT) c |= C_FLIP; break;
    default:          f = MAN_STAND; break;
    }
    n_vis[i] = 1;
    n_x[i] = (uint8_t)((uint16_t)p->px >> 1);
    n_y[i] = scr_y[p->py];
    n_f[i] = f;
    n_c[i] = c;
}

/* the cells a 5 x 6 picture at playfield (x, y) covers */
static uint8_t cx0, cx1, cy0, cy1;
static void cells_of(uint8_t x, uint8_t y)
{
    cx0 = div5[x];
    cx1 = div5[x + 4];
    cy0 = div6[y];
    cy1 = div6[y + 5];
    if (cy1 >= GRID_ROWS) {
        cy1 = GRID_ROWS - 1;
        hud_dirty = 2;          /* (it hangs into the status line) */
    }
}

static void mark_cells(void)
{
    rp_add(cx0, cy0);
    if (cx1 != cx0) rp_add(cx1, cy0);
    if (cy1 != cy0) {
        rp_add(cx0, cy1);
        if (cx1 != cx0) rp_add(cx1, cy1);
    }
}

static uint8_t cells_marked(void)
{
    return mark[CELL_I(cx0, cy0)] == stamp || mark[CELL_I(cx1, cy0)] == stamp
        || mark[CELL_I(cx0, cy1)] == stamp || mark[CELL_I(cx1, cy1)] == stamp;
}

/* what to repaint: the tiles that changed, the old pictures of the
   people who changed (all of them after a full repaint: nothing shows) */
static void people_old(void)
{
    static uint8_t i, *vis;
    vis = k_vis[cur];
    for (i = 0; i < N_ENT; ++i) {
        if (!vis[i]) continue;
        if (n_vis[i] && n_x[i] == k_x[cur][i] && n_y[i] == k_y[cur][i]
            && n_f[i] == k_f[cur][i] && n_c[i] == k_c[cur][i])
            continue;
        cells_of(k_x[cur][i], k_y[cur][i]);
        mark_cells();
        vis[i] = 0;             /* (gone from this buffer once repainted) */
    }
}

/* draw who must be drawn (after the repaint) */
static void people_draw(void)
{
    static uint8_t i, any, c;
    static SCB_REHV_PAL *s;
    any = 0;
    for (i = 0, s = ent; i < N_ENT; ++i, ++s) {
        s->sprctl1 |= SKIP;
        if (!n_vis[i]) continue;
        cells_of(n_x[i], n_y[i]);
        if (k_vis[cur][i] && !cells_marked())
            continue;           /* already there, untouched */
        mark_cells();           /* (whoever comes later and overlaps: redrawn too) */
        c = n_c[i];
        s->sprctl1 &= ~SKIP;
        s->data = (unsigned char *)man_frames[n_f[i]];
        if (c & C_FLIP) {
            s->sprctl0 = BPP_4 | TYPE_NORMAL | HFLIP;
            s->hpos = PF_X + 4 + n_x[i];
        } else {
            s->sprctl0 = BPP_4 | TYPE_NORMAL;
            s->hpos = PF_X + n_x[i];
        }
        s->vpos = n_y[i];
        s->penpal[1] = 0x20 | (c & 15);                 /* pen 3: the belt */
        k_vis[cur][i] = 1;
        k_x[cur][i] = n_x[i];
        k_y[cur][i] = n_y[i];
        k_f[cur][i] = n_f[i];
        k_c[cur][i] = c;
        any = 1;
    }
    if (any) spr_draw(ent);
}

static void people_init(void)
{
    uint8_t i;
    for (i = 0; i < N_ENT; ++i) {
        spr_init(&ent[i], man_frames[0], 4);
        if (i < MAX_ENEMIES) {
            spr_pen(&ent[i], 9, C_ENEMY);
            spr_pen(&ent[i], 10, C_ENEMYHI);
        }
        if (i) spr_link(&ent[i - 1], &ent[i]);
    }
}

/* ------------------------------------------------------------ status line */

char *render_num(uint32_t v, uint8_t digits)
{
    static const uint32_t pow10[7] = { 1000000UL, 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
    static char buf[8];
    uint8_t i, d;
    for (i = 0; i < 7; ++i) {
        for (d = '0'; v >= pow10[i]; ++d) v -= pow10[i];
        buf[i] = d;
    }
    buf[7] = 0;
    return buf + 7 - digits;
}

/* The original's status bar: SCORE, LV, GOLD (yellow), LIVES.
   port (speed): one 2-bpp sprite for the whole line (Suzy's cost is mostly
   per sprite: a 3x5 font character each would take ~1.5k cycles apiece).
   The CPU writes the characters into its data, a byte (4 pixels) each:
   pen 0 the status bar's blue (drawn: a background sprite), 1 white,
   2 yellow. */
extern const unsigned char font3x5_data[64][16];
#define HUD_COLS 40
#define HUD_LINE (1 + HUD_COLS + 1)             /* length byte, data, pad */
static uint8_t hud_data[6 * HUD_LINE + 1];
static SCB_REHV_PAL hud_scb;
/* a glyph row's 3 bits (bits 7-5) as 4 pixels of pen 1 */
static uint8_t hud_exp[8];

static void hud_put(uint8_t col, const char *s, uint8_t pen)
{
    static uint8_t r, ch, *d;
    static const unsigned char *g;
    for (; *s; ++s, ++col) {
        ch = (uint8_t)*s - 32;
        g = font3x5_data[ch] + 1;
        d = hud_data + HUD_LINE + 1 + col;      /* (line 0 stays blank) */
        for (r = 0; r < 5; ++r, g += 3, d += HUD_LINE)
            *d = pen == 1 ? hud_exp[*g >> 5] : hud_exp[*g >> 5] << 1;
    }
}

static void hud_init(void)
{
    uint8_t i, b;
    for (i = 0; i < 8; ++i) {
        b = 0;
        if (i & 4) b |= 0x40;
        if (i & 2) b |= 0x10;
        if (i & 1) b |= 0x04;
        hud_exp[i] = b;
    }
    for (i = 0; i < 6; ++i) hud_data[i * HUD_LINE] = HUD_LINE;  /* (counts itself) */
    spr_init(&hud_scb, hud_data, 2);
    hud_scb.sprctl0 = BPP_2 | TYPE_BACKNONCOLL;
    hud_scb.penpal[0] = (C_HUDBG << 4) | C_TEXT;
    hud_scb.penpal[1] = C_GOLD << 4;
    spr_pos(&hud_scb, 0, HUD_Y);
    hud_put(0, "SCORE", 1);
    hud_put(13, "LV", 1);
    hud_put(20, "GOLD", 2);
    hud_put(27, "/", 2);
    hud_put(32, "LIVES", 1);
}

static void status_line(void)
{
    static uint32_t score = 0xFFFFFFFFUL;
    static int16_t level = -1, gold = -1, total = -1, lives = -1;
    if (gs.score != score) hud_put(6, render_num(score = gs.score, 6), 1);
    if (gs.level_num != level) hud_put(16, render_num(level = gs.level_num, 3), 1);
    if (gs.gold_collected != gold) hud_put(25, render_num(gold = gs.gold_collected, 2), 2);
    if (gs.gold_total != total) hud_put(28, render_num(total = gs.gold_total, 2), 2);
    if (gs.lives != lives) hud_put(38, render_num((lives = gs.lives) < 0 ? 0 : lives, 1), 1);
    spr_draw(&hud_scb);
}

/* ------------------------------------------------------------ frames */

void render_room(void)
{
    full = 2;
    dl_n[0] = dl_n[1] = 0;
    hud_dirty = 2;
}

static void repaint_all(void)
{
    static uint8_t x, y;
    static SCB_REHV_PAL b;
    gfx_clear(C_BG);
    spr_box(&b, 0, 0, PF_X, HUD_Y, C_HUDBG);            /* the side borders */
    spr_draw(&b);
    spr_box(&b, PF_X + GRID_COLS * 5, 0, 160 - PF_X - GRID_COLS * 5, HUD_Y, C_HUDBG);
    spr_draw(&b);
    for (y = 0; y < GRID_ROWS; ++y)
        for (x = 0; x < GRID_COLS; ++x)
            if (tile_frame[TILE(x, y)]) pool_cell(x, y);
    pool_flush();
}

void render_frame(uint8_t entities)
{
    static uint8_t i, n, *px, *py;
    if (++stamp == 0) {             /* (every 255 frames: clear the marks) */
        memset(mark, 0, sizeof mark);
        stamp = 1;
    }
    rp_n = 0;
    people_now(entities);
    if (full) {
        --full;
        repaint_all();
        dl_n[cur] = 0;
        memset(k_vis[cur], 0, N_ENT);
        hud_dirty = 2;
    } else {
        n = dl_n[cur];
        px = dl_x[cur];
        py = dl_y[cur];
        for (i = 0; i < n; ++i) rp_add(px[i], py[i]);
        dl_n[cur] = 0;
        people_old();
        for (i = 0; i < rp_n; ++i) pool_cell(rp_x[i], rp_y[i]);
        pool_flush();
    }
    people_draw();
    if (hud_dirty) {
        --hud_dirty;
        status_line();
    }
}

void render_present(void)
{
    gfx_present();
    cur ^= 1;
}

void render_msg(const char *big, uint8_t big_col, const char *l1, const char *l2, uint8_t col2)
{
    static SCB_REHV_PAL b;
    spr_box(&b, 14, 28, 132, 38, big_col);              /* the frame */
    spr_draw(&b);
    spr_box(&b, 15, 29, 130, 36, C_BG);
    spr_draw(&b);
    gfx_text_centre(33, big_col, big);
    if (l1) text_small_centre(46, col2, l1);
    if (l2) text_small_centre(55, col2, l2);
    /* the playfield under the box is repainted afterwards: the caller
       starts a level or resumes with render_room() */
}

/* ------------------------------------------------------------ title */

static uint8_t title_full;

void render_title_new(void)
{
    title_full = 2;
}

/* the whole title into the back buffer for its first two frames (both
   buffers), then only the blinking line */
void render_title(uint8_t blink, uint32_t hiscore)
{
    static SCB_REHV_PAL b;
    static SCB_REHV_PAL t;
    if (!title_full) {
        spr_box(&b, 0, 29, 160, 7, C_BG);
        spr_draw(&b);
        if (blink) text_small_centre(30, C_TEXT, "PRESS A TO START");
        return;
    }
    --title_full;
    gfx_clear(C_BG);
    gfx_text_centre(4, C_GOLD, "BULLION DASH");
    spr_box(&b, 20, 14, 120, 3, C_BRICK);               /* the brick line */
    spr_draw(&b);
    spr_box(&b, 20, 14, 120, 1, C_BRICKHI);
    spr_draw(&b);
    text_small_centre(20, C_LADDER, "FOR ATARI LYNX");
    if (blink) text_small_centre(30, C_TEXT, "PRESS A TO START");
    text_small(8, 40, C_PLAYER, "PAD         RUN, CLIMB");
    text_small(8, 47, C_PLAYER, "A/B+LEFT/RIGHT  DIG");
    text_small(8, 54, C_PLAYER, "OPTION 1    GIVE UP A LIFE");
    text_small(8, 61, C_PLAYER, "PAUSE       PAUSE");
    /* the original's tile previews */
    spr_init(&t, tiles_frames[1], 4);
    spr_pos(&t, 22, 72);
    spr_draw(&t);
    text_small(30, 73, C_TEXT, "BRICK");
    spr_data(&t, tiles_frames[3]);
    spr_pos(&t, 62, 72);
    spr_draw(&t);
    text_small(70, 73, C_TEXT, "LADDER");
    spr_data(&t, tiles_frames[5]);
    spr_pos(&t, 106, 72);
    spr_draw(&t);
    text_small(114, 73, C_TEXT, "GOLD");
    text_small(36, 88, C_GOLD, "HIGH SCORE");
    text_small(80, 88, C_GOLD, render_num(hiscore, 6));
}
