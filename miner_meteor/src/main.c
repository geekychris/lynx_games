/*
 * Miner Meteor (Atari Lynx). Port of amiga_mcp/examples/miner_meteor.
 *
 * game.c is the original's game.cpp (the rules, unchanged; by way of the
 * Atari 8-bit port); this file is the Lynx front end in place of the
 * original's main.cpp and render.cpp: input, Suzy drawing, the title and
 * end screens, the HUD, pause, sound triggers and the log lines test.py
 * reads.
 *
 * port: the view is the original's 320 x 240 cavern (20 x 15 tiles of 16) at half
 * width and 7/16 height, so a tile is 8 x 7. Row 0 (the ceiling, solid in
 * every cavern) and row 14 (a second floor row under row 13) don't fit:
 * rows 1-13 fill lines 0-90 and the HUD the 11 lines below, where the
 * original has its 16-pixel HUD strip.
 *
 * Drawing: the cavern is drawn in full only when it is loaded (and after
 * pause). After that each frame redraws just the cells that need it, then
 * the guardians and the miner, as one chain of Suzy sprites: for each of
 * the two screen buffers a list of dirty cells, the cells its moving
 * sprites covered when that buffer was last drawn plus the cells whose
 * tile changed (a key taken, a crumbling floor, the exit blinking). A dirty
 * cell is a black box and its tile on top.
 *
 * Timing: the original's PAL rate, one tick a frame at 50 Hz.
 */
#include <string.h>
#include <stdlib.h>
#include "lynxgame.h"
#include "game.h"
#include "tmap.h"
#include "art.h"
#include "sound.h"

#pragma static-locals (on)

/* the original's palette indices (render.cpp) */
#define COL_WHITE   1
#define COL_CYAN    4
#define COL_YELLOW  6
#define COL_GREEN   7
#define COL_DKGREY  8
#define COL_RED     9
#define COL_BLUE    11
#define COL_DIM     15
#define COL_BLACK   COL_BLACK2      /* port: a drawable black (art.h) */

#define VIEW_ROWS 13                /* cavern rows 1-13 */
#define NCELL (VIEW_ROWS * GRID_W)
#define HUD_Y 91
#define BAR_X 51
#define BAR_W 60                    /* the original's 120-pixel air bar, halved */

/* ------------------------------------------------------------ log */

#ifndef LYNX_NO_LOG
#define LOG(...) lynx_log(__VA_ARGS__)
static void logu(const char *k, uint16_t v) { lynx_log_s(k); lynx_log_u(v); }
#define LOGS(s) lynx_log_s(s)
#define LOGNL() lynx_log_c('\n')
#else
#define LOG(...) ((void)0)
#define logu(k, v) ((void)0)
#define LOGS(s) ((void)0)
#define LOGNL() ((void)0)
#endif

/* ------------------------------------------------------------ the sprite chain */

#define POOL 300                    /* a full cavern: 260 cells, 4 sprites */
#define MAXD 48                     /* dirty cells a buffer can hold */

static const uint8_t blank_data[] = { 2, 0x00, 0 };
static SCB_REHV_PAL head;           /* loads the pens; the rest reuse them */
static SCB_RENONE pool[POOL];
static SCB_RENONE *bp;

/* per buffer (no [2][n] arrays: cc65 multiplies to index them) */
static uint8_t dmark0[NCELL], dmark1[NCELL];   /* cell already in the buffer's list */
static uint8_t dlc0[MAXD], dlc1[MAXD], dlr0[MAXD], dlr1[MAXD];
static uint8_t dn[2];
static uint8_t *m_mark, *m_c, *m_r;            /* the buffer mark() adds to */
static uint8_t m_b;
static uint8_t need_full[2];        /* redraw the whole cavern in this buffer */
static uint8_t hud_dirty[2];
static uint8_t cur;                 /* the back buffer */
static uint8_t row_of[96];          /* screen line -> cavern row */
static uint16_t row20[GRID_H];      /* row * 20 */
static uint8_t row_y[GRID_H];       /* row -> screen line */
static uint8_t room_stale;         /* the cavern chain (room[]) needs a rebuild */
uint8_t n_sprites, n_dirty;         /* the last frame's (log, tests) */

static void use_buf(uint8_t b)
{
    m_b = b;
    if (b) { m_mark = dmark1; m_c = dlc1; m_r = dlr1; }
    else { m_mark = dmark0; m_c = dlc0; m_r = dlr0; }
}

static void mark(uint8_t c, uint8_t r)
{
    uint8_t *m = m_mark + row20[r - 1] + c;
    uint8_t n;
    if (*m) return;
    n = dn[m_b];
    if (n == MAXD) { need_full[m_b] = 1; return; }
    *m = 1;
    m_c[n] = c;
    m_r[n] = r;
    dn[m_b] = n + 1;
}

static void mark_both(uint8_t c, uint8_t r)
{
    if (r < 1 || r > VIEW_ROWS) return;
    room_stale = 1;
    use_buf(0);
    mark(c, r);
    use_buf(1);
    mark(c, r);
}

static uint8_t exit_open;           /* the open exit is showing (blink phase) */
static uint8_t exit_c, exit_r;      /* the cavern's exit (0, 0: none) */

/* render.cpp draw_tile(): the picture of a cell (0 = empty) */
static const unsigned char *tile_art(uint8_t c, uint8_t r)
{
    uint8_t t;
    switch (TMAP(c, r)) {
    case T_SOLID: return ((c + r) & 1) ? art_solid_a : art_solid_b;
    case T_KEY: return art_key;
    case T_EXIT: return exit_open ? art_exit_open : art_exit_locked;
    case T_COLLAPSE:
        t = game_collapse_timer((uint16_t)(row20[r] + c));
        return (t && t <= COLLAPSE_FRAMES / 2) ? art_collapse_dim : art_collapse;
    case T_CONV_L: return art_conv_l;
    case T_CONV_R: return art_conv_r;
    case T_HAZARD: return art_hazard;
    }
    return 0;
}

static void emit(const unsigned char *data, int16_t x, int16_t y)
{
    bp->sprctl0 = BPP_4 | TYPE_NORMAL;
    bp->sprctl1 = LITERAL | RENONE | REUSEPAL;
    bp->sprcoll = NO_COLLIDE;
    bp->data = (unsigned char *)data;
    bp->hpos = x;
    bp->vpos = y;
    bp->next = (char *)(bp + 1);
    ++bp;
}

/* world y -> screen y (7/16, row 1 at line 0) */
static int16_t sy_of(int16_t wy)
{
    return (((wy << 3) - wy) >> 4) - 7;
}

/* a sprite at (sx, sy): add it and mark the cells it covers in this buffer */
static void emit_obj(const unsigned char *data, int16_t sx, int16_t sy)
{
    uint8_t c0, c1, r0, r1;
    if (sy < 0 || sy > 84) return;
    emit(data, sx, sy);
    c0 = (uint8_t)sx >> 3;
    c1 = (uint8_t)(sx + 7) >> 3;
    if (c1 >= GRID_W) c1 = GRID_W - 1;
    r0 = row_of[sy];
    r1 = row_of[sy + 6];
    mark(c0, r0);
    if (c1 != c0) mark(c1, r0);
    if (r1 != r0) {
        mark(c0, r1);
        if (c1 != c0) mark(c1, r1);
    }
}

/* The whole cavern as a chain (its non-empty cells), built when a cavern is
   loaded or a tile changed and drawn after a clear: on load (both buffers),
   after pause. */
static SCB_RENONE room[NCELL];
static SCB_RENONE *room_end;

/* the tiles whose picture is the kind alone (the others: tile_art) */
static const unsigned char * const kind_art[T_KIND_COUNT] = {
    0, 0, art_key, 0, 0, art_conv_l, art_conv_r, art_hazard, 0
};

static void room_init(void)
{
    uint16_t i;
    for (i = 0; i < NCELL; ++i) {
        room[i].sprctl0 = BPP_4 | TYPE_NORMAL;
        room[i].sprctl1 = LITERAL | RENONE | REUSEPAL;
        room[i].sprcoll = NO_COLLIDE;
    }
}

static void room_build(void)
{
    uint8_t c, r, k, y;
    const uint8_t *p;
    const unsigned char *a;
    SCB_RENONE *q = room;
    for (r = 1; r <= VIEW_ROWS; ++r) {
        p = tmap_row[r];
        y = row_y[r];
        for (c = 0; c < GRID_W; ++c) {
            if (!(k = p[c])) continue;
            if (k == T_SOLID) a = ((c + r) & 1) ? art_solid_a : art_solid_b;
            else if (!(a = kind_art[k]) && !(a = tile_art(c, r))) continue;
            q->data = (unsigned char *)a;
            q->hpos = c << 3;
            q->vpos = y;
            q->next = (char *)(q + 1);
            ++q;
        }
    }
    room_end = q;
    room_stale = 0;
}

static void draw_play(void)
{
    uint8_t i, c, r, n, b = cur;
    const unsigned char *a;
    const Guardian *G;
    const Player *p = &gs.player;

    bp = pool;
    use_buf(b);
    n = dn[b];
    if (need_full[b]) {
        gfx_clear(COL_BLACK);
        if (room_stale) room_build();
        if (room_end != room) {
            room_end[-1].next = 0;
            head.next = (char *)room;
            spr_draw(&head);
        }
        for (i = 0; i < n; ++i) m_mark[row20[m_r[i] - 1] + m_c[i]] = 0;
        need_full[b] = 0;
        hud_dirty[b] = 1;
        n_dirty = 255;
    } else {
        /* black boxes over the dirty cells, then their tiles */
        for (i = 0; i < n; ++i) emit(art_black, m_c[i] << 3, row_y[m_r[i]]);
        for (i = 0; i < n; ++i) {
            c = m_c[i];
            r = m_r[i];
            m_mark[row20[r - 1] + c] = 0;
            if ((a = tile_art(c, r)) != 0) emit(a, c << 3, row_y[r]);
        }
        n_dirty = n;
    }
    dn[b] = 0;

    /* the guardians (render.cpp draw_guardian), then the miner */
    for (i = 0, G = gs.guards; i < gs.num_guards; ++i, ++G) {
        if (!G->alive) continue;
        c = G->colour == 5 ? 1 : G->colour == 6 ? 2 : 0;
        emit_obj(art_guard[c][(G->frame >> 2) & 7], G->x >> 1, sy_of(G->y));
    }
    if (!p->b.on_ground) a = p->b.vy < 0 ? art_player_jump : art_player_fall;
    else a = art_player[p->facing][p->frame & 3];
    emit_obj(a, p->b.x >> 1, sy_of(p->b.y));

    n_sprites = (uint8_t)(bp - pool);
    if (bp != pool) {
        bp[-1].next = 0;
        head.next = (char *)pool;
        spr_draw(&head);
    }
}

/* ------------------------------------------------------------ HUD */

/* One chain built once: the strip, the air bar, then 23 characters of the
   3x5 font whose pictures change with the text (text_small would rebuild
   the chain each time). Drawn into a buffer only when it changed. */
extern const unsigned char font3x5_data[64][16];
#define HUD_CHARS 23                /* "SCORE 000000" + "LV1 K0/4 L3" */
static SCB_REHV_PAL hud_bg, bar_bg, bar_fill, hud_c0;
static SCB_RENONE hud_cs[HUD_CHARS - 1];
static uint8_t hud_fill = 255, hud_low;
static uint32_t hud_score = 0xFFFFFFFFUL;
static uint8_t hud_keys = 255, hud_lives = 255, hud_level = 255;
static char score_str[13] = "SCORE 000000";
static char right_str[12];

static void hud_init(void)
{
    uint8_t i;
    SCB_RENONE *r;
    spr_box(&hud_bg, 0, HUD_Y, 160, 11, COL_DIM);
    spr_box(&bar_bg, BAR_X, HUD_Y + 3, BAR_W, 5, COL_DKGREY);
    spr_box(&bar_fill, BAR_X, HUD_Y + 3, 1, 5, COL_GREEN);
    spr_init(&hud_c0, font3x5_data[0], 1);
    spr_color(&hud_c0, COL_WHITE);
    hud_c0.hpos = 1;
    hud_c0.vpos = HUD_Y + 3;
    spr_link(&hud_bg, &bar_bg);
    spr_link(&bar_bg, &bar_fill);
    spr_link(&bar_fill, &hud_c0);
    spr_link(&hud_c0, &hud_cs[0]);
    for (i = 0, r = hud_cs; i < HUD_CHARS - 1; ++i, ++r) {
        r->sprctl0 = BPP_1 | TYPE_NORMAL;
        r->sprctl1 = LITERAL | RENONE | REUSEPAL;
        r->sprcoll = NO_COLLIDE;
        r->hpos = i < 11 ? 5 + 4 * i : 160 - 4 * 11 + 4 * (i - 11);
        r->vpos = HUD_Y + 3;
        r->next = i < HUD_CHARS - 2 ? (char *)(r + 1) : 0;
    }
}

static void hud_text(void)
{
    uint8_t i;
    hud_c0.data = (unsigned char *)font3x5_data[score_str[0] - 32];
    for (i = 1; i < 12; ++i) hud_cs[i - 1].data = (unsigned char *)font3x5_data[score_str[i] - 32];
    for (i = 0; i < 11; ++i) hud_cs[11 + i].data = (unsigned char *)font3x5_data[right_str[i] - 32];
}

/* "000000" from a score (it changes on events only). port: 16 bits are
   enough: six caverns score at most ~13,000 */
static void six_digits(char *out, uint32_t v)
{
    char buf[8];
    uint8_t n, i;
    utoa((uint16_t)v, buf, 10);
    n = (uint8_t)strlen(buf);
    for (i = 0; i < 6; ++i) out[i] = i + n < 6 ? '0' : buf[i + n - 6];
}

/* the HUD changed? (both buffers redraw it) */
static uint16_t hud_air = 0xFFFF;
static void hud_check(void)
{
    uint8_t keys;
    if ((uint16_t)gs.air != hud_air) {
        uint8_t fill, low;
        hud_air = gs.air;
        fill = (uint8_t)((uint16_t)gs.air * 3 / 50);   /* air * 60 / 1000 */
        low = gs.air < AIR_MAX / 4;
        if (fill != hud_fill || low != hud_low) {
            hud_fill = fill;
            hud_low = low;
            spr_box_size(&bar_fill, fill ? fill : 1, 5);
            bar_fill.penpal[0] = fill ? (low ? COL_RED : COL_GREEN) : COL_DKGREY;
            hud_dirty[0] = hud_dirty[1] = 1;
        }
    }
    keys = gs.keys_total - gs.keys_remaining;
    if (gs.score == hud_score && keys == hud_keys && gs.lives == hud_lives && gs.level_index == hud_level) return;
    if (gs.score != hud_score) six_digits(score_str + 6, gs.score);
    hud_score = gs.score;
    hud_keys = keys;
    hud_lives = gs.lives;
    hud_level = gs.level_index;
    /* render.cpp: "LV%ld K%ld/%ld L%ld" */
    right_str[0] = 'L'; right_str[1] = 'V'; right_str[2] = '1' + gs.level_index;
    right_str[3] = ' '; right_str[4] = 'K'; right_str[5] = '0' + keys; right_str[6] = '/';
    right_str[7] = '0' + gs.keys_total; right_str[8] = ' '; right_str[9] = 'L';
    right_str[10] = '0' + gs.lives; right_str[11] = 0;
    hud_text();
    hud_dirty[0] = hud_dirty[1] = 1;
}

static void draw_hud(void)
{
    if (!hud_dirty[cur]) return;
    hud_dirty[cur] = 0;
    spr_draw(&hud_bg);
}

/* ------------------------------------------------------------ title, end screens */

/* The title and end screens are drawn once into each buffer (text_small is
   slow: the whole title takes ~3 frames); after that only the blinking line
   is drawn or boxed out, so the tick (and the blink) keep the frame rate. */
static uint8_t scr_full[2], scr_blink[2];

static void blink_line(uint8_t on, uint8_t y, uint8_t c, const char *s)
{
    if (scr_blink[cur] == on + 1) return;
    scr_blink[cur] = on + 1;
    if (on) text_small_centre(y, c, s);
    else gfx_rect(0, y, 160, 6, COL_BLACK);
}

static void draw_title(void)
{
    /* render.cpp render_title() */
    if (scr_full[cur]) {
        scr_full[cur] = 0;
        scr_blink[cur] = 0;
        gfx_clear(COL_BLACK);
        gfx_rect(0, 10, 160, 18, COL_BLUE);
        gfx_text_centre(15, COL_WHITE, "MINER METEOR");
        text_small_centre(36, COL_YELLOW, "A CLEAN-ROOM PLATFORMER");
        text_small_centre(44, COL_WHITE, "INSPIRED BY 8-BIT CLASSICS");
        text_small(24, 58, COL_GREEN, "LEFT / RIGHT :  MOVE");
        text_small(24, 66, COL_GREEN, "A / B / UP   :  JUMP");
        text_small(24, 74, COL_GREEN, "PAUSE        :  PAUSE");
    }
    blink_line((gs.tick / 15) & 1, 90, COL_RED, "PRESS  A  TO  BEGIN");
}

static void draw_end(void)
{
    /* render.cpp render_endscreen() */
    if (scr_full[cur]) {
        scr_full[cur] = 0;
        scr_blink[cur] = 0;
        gfx_clear(COL_BLACK);
        if (gs.mode == GM_WIN) {
            gfx_rect(0, 26, 160, 16, COL_GREEN);
            gfx_text_centre(30, COL_BLACK, "ALL CAVERNS CLEARED!");
        } else {
            gfx_rect(0, 26, 160, 16, COL_RED);
            gfx_text_centre(30, COL_BLACK, "GAME OVER");
        }
        text_small_centre(56, COL_WHITE, "FINAL SCORE");
        gfx_text_centre(64, COL_WHITE, score_str + 6);
    }
    blink_line(gs.state_timer == 0 && ((gs.tick / 15) & 1), 86, COL_YELLOW, "PRESS  A  TO  RETURN");
}

/* ------------------------------------------------------------ main */

static uint8_t paused, prev_mode = 255;
static uint16_t fps_vbl, fps_loops, late, last_vbl;
static uint8_t busy_max;

static void log_score(void)
{
    logu(" score=", (uint16_t)gs.score);
}

/* a cavern was (re)loaded: draw it all in both buffers */
static void cavern_loaded(void)
{
    need_full[0] = need_full[1] = 1;
    room_stale = 1;
    exit_open = 0;
    if (!tmap_find(T_EXIT, &exit_c, &exit_r)) exit_r = 0;
    hud_check();
    logu("mm: level=", gs.level_index + 1);
    logu(" lives=", gs.lives);
    logu(" keys=", gs.keys_total);
    log_score();
    LOGNL();
}

/* what one tick did: sounds, dirty cells, log lines */
static void after_tick(void)
{
    uint8_t ev = gs.events, i;
    uint16_t cell;
    for (i = 0; i < gs.num_changed; ++i) {
        cell = gs.changed[i];
        mark_both((uint8_t)(cell % GRID_W), (uint8_t)(cell / GRID_W));
    }
    if (!ev) return;
    if (ev & EV_JUMP) { sound_jump(); LOGS("mm: jump\n"); }
    if (ev & EV_KEY) {
        sound_key();
        logu("mm: key left=", gs.keys_remaining);
        log_score();
        LOGNL();
    }
    if (ev & EV_DIED) {
        sound_death();
        logu("mm: died lives=", gs.lives);
        logu(" level=", gs.level_index + 1);
        LOGNL();
    }
    if (ev & EV_LEVEL) {
        if (ev & EV_WON) sound_win(); else sound_levelwin();
        logu("mm: cleared level=", gs.level_index);
        log_score();
        logu(" air=", gs.air);
        logu(" ticks=", gs.level_ticks);
        LOGNL();
    }
    if (ev & EV_LOADED) cavern_loaded();
    if (ev & EV_GAMEOVER) { sound_gameover(); LOGS("mm: gameover"); log_score(); LOGNL(); }
    if (ev & EV_WON) { LOGS("mm: won"); log_score(); LOGNL(); }
}

int main(void)
{
    uint8_t inp, busy, i, open;
    uint16_t now;

    gfx_init();
    gfx_set_rate(50);                       /* the original's PAL rate: one tick a frame */
    gfx_palette(art_palette);
    sound_init();
    spr_init(&head, blank_data, 4);
    hud_init();
    room_init();
    for (i = 0; i < sizeof(row_of); ++i) row_of[i] = i / 7 + 1;
    for (i = 0; i < GRID_H; ++i) { row20[i] = i * GRID_W; row_y[i] = i ? (i - 1) * 7 : 0; }
    game_init();
    LOG("mm: ready hz=50\n");
    last_vbl = frame_count;

    for (;;) {
        input_update();

        if (gs.mode == GM_PLAYING || gs.mode == GM_LEVELWIN) {
            if (input_new & IN_PAUSE) {             /* port: pause (as the other Lynx ports) */
                paused ^= 1;
                if (paused) snd_all_off();
                else need_full[0] = need_full[1] = 1;
                logu("mm: paused=", paused);
                LOGNL();
            }
            if (paused && (input_new & IN_OPT1)) {   /* PAUSE, OPTION 1: give up (the original's ESC quits) */
                LOGS("mm: give up\n");
                game_init();
                continue;
            }
        }

        /* --- one tick (main.cpp: INPUT_START on the rising edge only) --- */
        if (!paused) {
            inp = 0;
            if (input_held & IN_LEFT) inp |= INPUT_LEFT;
            if (input_held & IN_RIGHT) inp |= INPUT_RIGHT;
            if (input_held & (IN_A | IN_B | IN_UP)) inp |= INPUT_JUMP;   /* port: SPACE / UP */
            if (input_new & (IN_A | IN_B)) inp |= INPUT_START;
            game_tick(inp);
            after_tick();
        }

        if (gs.mode != prev_mode) {
            if (gs.mode == GM_TITLE) { LOGS("mm: title\n"); sound_all_off(); paused = 0; }
            if (gs.mode == GM_PLAYING && prev_mode == GM_TITLE) {
                LOGS("mm: start\n");
                fps_vbl = frame_count;
                fps_loops = late = busy_max = 0;
            }
            prev_mode = (uint8_t)gs.mode;
            scr_full[0] = scr_full[1] = 1;
            if (gs.mode == GM_WIN || gs.mode == GM_LOSE) six_digits(score_str + 6, gs.score);
        }

        /* --- draw --- */
        if (gs.mode == GM_TITLE) {
            draw_title();
        } else if (gs.mode == GM_WIN || gs.mode == GM_LOSE) {
            draw_end();
            need_full[0] = need_full[1] = 1;
            hud_score = 0xFFFFFFFFUL;
            hud_air = 0xFFFF;
            hud_fill = 255;
        } else {
            /* the exit blinks open every 6 ticks once the last key is taken */
            open = gs.keys_remaining == 0 && ((gs.tick / 6) & 1);
            if (open != exit_open) {
                exit_open = open;
                if (exit_r) mark_both(exit_c, exit_r);
            }
            hud_check();
            draw_play();
            draw_hud();
            if (paused) {
                gfx_text_centre(40, COL_YELLOW, "PAUSED");
                need_full[cur ^ 1] = 1;
            }
        }

        busy = (uint8_t)(MIKEY.timer2.reload - MIKEY.timer2.count);
        if (busy > busy_max) busy_max = busy;
        gfx_present();
        cur ^= 1;
        if (!paused) sound_tick();

        /* --- frame rate: loops per vertical blank, logged every 2 s in play --- */
        now = frame_count;
        if ((uint16_t)(now - last_vbl) > 1) late += (uint16_t)(now - last_vbl) - 1;
        last_vbl = now;
        ++fps_loops;
        if ((uint16_t)(now - fps_vbl) >= 100) {
            if (gs.mode == GM_PLAYING && !paused) {
                logu("mm: fps=", (uint16_t)(fps_loops * 50u / (uint16_t)(now - fps_vbl)));
                logu(" late=", late);
                logu(" busy=", busy_max);
                logu(" sprites=", n_sprites);
                logu(" level=", gs.level_index + 1);
                LOGNL();
            }
            fps_vbl = now;
            fps_loops = late = 0;
            busy_max = 0;
        }
    }
}
