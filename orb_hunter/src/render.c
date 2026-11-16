/*
 * Orb Hunter - the Lynx screen. Replaces the original's draw.c: the same
 * room tiles, hunter, creatures, shots, sparks, orbs, HUD, title, item box
 * and GAME OVER, drawn with Suzy sprites at x / 2 and y * 3 / 8: the
 * original's 320 x 256 room (20 x 16 tiles of 16 x 16) is 160 x 96 here,
 * under a 6-row HUD (the original's HUD covers the room's top 16 rows; here
 * the whole room shows).
 *
 * port (speed): the original clears the screen and redraws every tile every
 * frame. Here a room is painted in full when it is entered (both screen
 * buffers); then each buffer repaints only the cells that what it showed
 * last time (two frames ago) covered, the lava and save point when their
 * animation steps, and tiles that changed (an opened door). A tile sprite
 * paints its whole 8 x 6 cell (its black is palette entry 10, COL_BGD). The
 * moving things are one sprite chain.
 */
#pragma static-locals (on)
#include <string.h>
#include "lynxgame.h"
#include "game.h"
#include "render.h"
#include "spr_tiles.h"
#include "spr_hero.h"
#include "spr_roll.h"
#include "spr_enemies.h"
#include "spr_beast.h"
#include "spr_drake.h"
#include "spr_logo.h"
#include "spr_gameover.h"

/* the original's palette (main.c), in its order (draw.c COL_*) */
#define COL_BG       0
#define COL_WHITE    1
#define COL_HUNTER   2
#define COL_HUNTER2  3
#define COL_ROCK     4
#define COL_ROCK2    5
#define COL_METAL    6
#define COL_METAL2   7
#define COL_LAVA     8
#define COL_LAVA2    9
/* port: entry 10 is the background colour again, drawable by sprites (Suzy
   treats a pixel mapped to colour 0 as transparent in a normal sprite); the
   original's 10 is a yellow (0xFF0) for the rockets and the HUD's M, here
   its other yellow (3, 0xFC0) */
#define COL_BGD      10
#define COL_MISSILE  COL_HUNTER2
#define COL_ENEMY    11
#define COL_ENEMY2   12
#define COL_DOOR     13
#define COL_ITEM     14
#define COL_BEAM     15
static const uint16_t palette[16] = {
    0x001, 0xFFF, 0x0A9, 0xFC0, 0x141, 0x262, 0x668, 0x99A,
    0xF30, 0xF80, 0x001, 0xA0F, 0xF0A, 0x06F, 0x0FF, 0x0F0,
};

#define PF_Y   6                    /* the room's top row on screen */

/* enemies.txt frames */
#define F_CRAWLER 0
#define F_FLYER   1
#define F_HOPPER  3
#define F_TURRET  4
#define F_ORB     6
#define F_ROCKET  8
#define F_BEAM    9

uint8_t rd_cells, rd_objs, rd_full;

static uint8_t cur;                 /* the back buffer: 0 or 1 */
static uint8_t full;                /* bit n: buffer n repaints everything */
static uint8_t hud_dirty;           /* bit n: buffer n redraws the HUD */

/* ------------------------------------------------------------ tiles */

/* tile -> tiles.txt frame (lava 5-8 and the save point 11-12 animate) */
const uint8_t tile_frame[NUM_TILE_TYPES] = { 0, 1, 2, 3, 4, 5, 9, 0, 10, 11 };
uint8_t lava_phase, save_phase;

/* the tiles being repainted: a pool of SCBs drawn as one chain
   (render_asm.s: paint_list, paint_all, pool_flush) */
#define POOL 20
SCB_REHV_PAL pool[POOL];
uint8_t cell_y[ROOM_H];                 /* a cell row's screen y */
extern uint8_t *pl_x, *pl_y, pl_n;
void paint_list(void);
void paint_all(void);
void put_cells(void);

/* cells to repaint in each buffer before it is drawn again */
#define DL_MAX 120
static uint8_t dl0_x[DL_MAX], dl0_y[DL_MAX], dl1_x[DL_MAX], dl1_y[DL_MAX], dl_n[2];

void render_tile_changed(uint8_t tx, uint8_t ty)
{
    if (dl_n[0] < DL_MAX) { dl0_x[dl_n[0]] = tx; dl0_y[dl_n[0]] = ty; ++dl_n[0]; } else full |= 1;
    if (dl_n[1] < DL_MAX) { dl1_x[dl_n[1]] = tx; dl1_y[dl_n[1]] = ty; ++dl_n[1]; } else full |= 2;
}

/* the lava and the save point: their cells, repainted when they step */
#define ANIM_MAX 48
static uint8_t an_x[ANIM_MAX], an_y[ANIM_MAX], an_n;
/* the orbs in this room: their item index and screen position */
static uint8_t ri_i[4], ri_x[4], ri_y[4], ri_n;

void render_room(void)
{
    static uint8_t x, y, t, i;
    full = 3;
    hud_dirty = 3;
    dl_n[0] = dl_n[1] = 0;
    an_n = 0;
    for (y = 0; y < ROOM_H; ++y)
        for (x = 0; x < ROOM_W; ++x) {
            t = room[y][x];
            if ((t == TILE_LAVA || t == TILE_SAVE_POINT) && an_n < ANIM_MAX) {
                an_x[an_n] = x;
                an_y[an_n] = y;
                ++an_n;
            }
        }
    ri_n = 0;
    for (i = 0; i < g_num_items && ri_n < 4; ++i)
        if (g_items[i].room_x == gs.room_x && g_items[i].room_y == gs.room_y) {
            ri_i[ri_n] = i;
            ri_x[ri_n] = g_items[i].tile_x << 3;
            ri_y[ri_n] = cell_y[g_items[i].tile_y];
            ++ri_n;
        }
}

/* screen row (0..95 of the room) -> cell row; world y (0..255) -> screen y */
uint8_t row6[96];
uint8_t sy_tab[256];
/* each cell repainted once a frame: a mark per cell (== stamp) */
uint8_t mark[ROOM_H * ROOM_W], stamp;

static void repaint(void)
{
    if (++stamp == 0) {
        memset(mark, 0, sizeof mark);
        stamp = 1;
    }
    rd_cells = 0;
    if (full & (1 << cur)) {
        /* the whole room: a clear (one Suzy fill) and the tiles that aren't
           empty */
        full &= (uint8_t)~(1 << cur);
        gfx_clear(COL_BG);
        paint_all();
        dl_n[cur] = 0;
        hud_dirty |= (uint8_t)(1 << cur);
        rd_cells = 255;
        ++rd_full;
        return;
    }
    /* this buffer's list, each cell once */
    if (cur) { pl_x = dl1_x; pl_y = dl1_y; } else { pl_x = dl0_x; pl_y = dl0_y; }
    pl_n = dl_n[cur];
    paint_list();
    dl_n[cur] = 0;
}

/* ------------------------------------------------------------ the moving things */

static SCB_REHV_PAL hunter_s, enemy_s[MAX_ENEMIES], item_s[4], bullet_s[MAX_BULLETS];
#define NBOX (MAX_BOMBS * 2 + MAX_ENEMY_BULLETS)
static SCB_REHV_PAL box_s[NBOX];
SCB_REHV_PAL spark_s[MAX_PARTICLES];        /* (render_asm.s draw_sparks) */
SCB_REHV_PAL *olist[1 + MAX_ENEMIES + 4 + MAX_BULLETS + NBOX + MAX_PARTICLES];
uint8_t on;
static SCB_REHV_PAL *bp;                    /* the next free box */
/* (pointer tables: &x[i] with a 23-byte SCB is a multiply in cc65) */
static SCB_REHV_PAL * const enemy_p[MAX_ENEMIES] = { &enemy_s[0], &enemy_s[1], &enemy_s[2], &enemy_s[3] };
static SCB_REHV_PAL * const item_p[4] = { &item_s[0], &item_s[1], &item_s[2], &item_s[3] };
static SCB_REHV_PAL * const bullet_p[MAX_BULLETS] = {
    &bullet_s[0], &bullet_s[1], &bullet_s[2], &bullet_s[3], &bullet_s[4], &bullet_s[5]
};
void draw_sparks(void);

/* the object being put: its SCB, screen position (top-left) and size */
static SCB_REHV_PAL *os;
int16_t ox, oy;
uint8_t ow, oh;
/* this buffer's repaint list */
uint8_t *dlx, *dly, dln, dfull;

/* into the chain, and the cells under it into this buffer's list
   (render_asm.s put_cells) */
static void put(void)
{
    olist[on++] = os;
    put_cells();
}

/* a w x h box of colour c (the original's RectFill / WritePixel) */
static uint8_t bc;
static void box(void)
{
    os = bp++;
    os->hpos = ox;
    os->vpos = oy;
    os->hsize = (uint16_t)ow << 8;
    os->vsize = (uint16_t)oh << 8;
    os->penpal[0] = bc;
    put();
}

/* a sprite (os, its data od) at (ox, oy), ow x oh on screen, mirrored when
   oflip (then it's drawn leftwards from ox + ow - 1) */
static const uint8_t *od;
static uint8_t oflip;
static void sprite(void)
{
    os->data = (unsigned char *)od;
    if (oflip) {
        os->sprctl0 = BPP_4 | TYPE_NORMAL | HFLIP;
        os->hpos = ox + ow - 1;
    } else {
        os->sprctl0 = BPP_4 | TYPE_NORMAL;
        os->hpos = ox;
    }
    os->vpos = oy;
    put();
}

/* world y (pixels) -> screen y; 0 when it is off the room (not drawn) */
#define scr_y(wy) ((uint16_t)(wy) < 256 ? sy_tab[(uint8_t)(wy)] : 0)

static void objects(void)
{
    static uint8_t i, g, r, y;
    static int16_t wx, wy;
    on = 0;
    bp = box_s;
    if (cur) { dlx = dl1_x; dly = dl1_y; } else { dlx = dl0_x; dly = dl0_y; }
    dln = dl_n[cur];
    dfull = 0;
    g = (uint8_t)gs.frame;          /* the original's g_frame */

    /* the orbs (draw_items): pulsing, the ones not taken in this room */
    for (i = 0; i < ri_n; ++i) {
        if (gs.items_collected & ((uint16_t)1 << ri_i[i])) continue;
        os = item_p[i];
        od = enemies_frames[(uint8_t)(((g >> 2) + ri_i[i]) & 3) > 1 ? F_ORB : F_ORB + 1];
        ox = ri_x[i];
        oy = ri_y[i];
        ow = 4; oh = 3; oflip = 0;
        sprite();
    }

    /* the hunter (draw_hunter): flickers while invulnerable, every other
       frame. (port: original bug, fixed in amiga_games #50: the frame
       counter went up twice a loop, so the test always saw it odd and hid
       the hunter for the whole time) */
    if (!(h_invuln_timer > 0 && (g & 1)) && (y = scr_y(FIX_INT(h_y))) != 0) {
        os = &hunter_s;
        ox = (int16_t)h_x >> 5;
        oy = y;
        if (h_rolled) {
            od = roll_frames[(g >> 1) & 3];
            ++ox;
            ow = 4; oh = 3; oflip = 0;
        } else {
            od = hero_data;
            ow = 8; oh = 6; oflip = h_facing;
        }
        sprite();
    }

    /* the beams and rockets (draw_bullets) */
    oh = 1;
    for (i = 0; i < MAX_BULLETS; ++i) {
        if (!b_on[i] || !(y = scr_y(FIX_INT(b_y[i])))) continue;
        os = bullet_p[i];
        ox = b_x[i] >> 5;
        oy = y;
        if (b_type[i] == BULLET_TYPE_MISSILE) {
            od = enemies_frames[F_ROCKET];
            ow = 3;
            oflip = b_dx[i] < 0;
        } else {
            od = enemies_frames[F_BEAM];
            ow = 2;
            oflip = 0;
        }
        sprite();
    }

    /* the bombs (draw_bombs): a yellow ball, then the flash growing */
    for (i = 0; i < MAX_BOMBS; ++i) {
        if (!bm_on[i]) continue;
        wx = FIX_INT(bm_x[i]);
        wy = FIX_INT(bm_y[i]);
        if (bm_timer[i] > 10) {
            if (!(y = scr_y(wy))) continue;
            ox = wx >> 1; oy = y; ow = 2; oh = 2; bc = COL_HUNTER2;
            box();
        } else {
            r = (uint8_t)(10 - bm_timer[i]) + 2;
            if (!(y = scr_y(wy - r + 2))) continue;
            ox = (wx - r + 2) >> 1; oy = y; ow = r; oh = (uint8_t)((r * 6 + 4) >> 3); bc = COL_WHITE;
            box();
            if (r > 2 && (y = scr_y(wy - r + 3)) != 0) {
                ox = (wx - r + 3) >> 1; oy = y; ow = r - 1;
                oh = (uint8_t)(((r - 1) * 6 + 4) >> 3); bc = COL_HUNTER2;
                box();
            }
        }
    }

    /* the creatures (draw_enemies): flash for 8 frames when hit. (port:
       original bug, fixed in amiga_games #50: its "flash when hit" test used
       the AI's timer, which always runs, with the always-odd counter, so no
       creature was ever drawn) */
    for (i = 0; i < MAX_ENEMIES; ++i) {
        if (!e_on[i] || (e_hit_timer[i] && (g & 1)) || !(y = scr_y(FIX_INT(e_y[i])))) continue;
        os = enemy_p[i];
        ox = e_x[i] >> 5;
        oy = y;
        oflip = 0;
        switch (e_type[i]) {
        case ENEMY_CRAWLER:
            od = enemies_frames[F_CRAWLER]; ow = 6; oh = 4; oflip = e_facing[i];
            break;
        case ENEMY_FLYER:
            od = enemies_frames[F_FLYER + ((g / 3) & 1)]; ow = 5; oh = 4;
            break;
        case ENEMY_HOPPER:
            od = enemies_frames[F_HOPPER]; ow = 5; oh = 5;
            break;
        case ENEMY_TURRET:
            od = enemies_frames[F_TURRET + ((g >> 3) & 1)]; ow = 6; oh = 5;
            break;
        case ENEMY_BOSS_BEAST:
            od = beast_data; ow = 12; oh = 12;
            break;
        default:
            od = drake_frames[(g >> 2) & 1]; ow = 10; oh = 9;
            break;
        }
        sprite();
    }

    /* their shots (draw_enemy_bullets) */
    ow = 2; oh = 1; bc = COL_ENEMY2;
    for (i = 0; i < MAX_ENEMY_BULLETS; ++i) {
        if (!eb_on[i] || !(y = scr_y(FIX_INT(eb_y[i])))) continue;
        ox = eb_x[i] >> 5;
        oy = y;
        box();
    }

    /* the sparks (draw_particles): a pixel each (render_asm.s) */
    draw_sparks();

    dl_n[cur] = dln;
    if (dfull) full |= (uint8_t)(1 << cur);
    rd_objs = on;
    if (!on) return;
    for (i = 1; i < on; ++i) olist[i - 1]->next = (char *)olist[i];
    olist[on - 1]->next = 0;
    spr_draw(olist[0]);
}

/* ------------------------------------------------------------ HUD (draw_hud) */

/* spr_box, also in the background colour (drawn as COL_BGD) */
static void solid(SCB_REHV_PAL *s, int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t c)
{
    spr_box(s, x, y, w, h, c == COL_BG ? COL_BGD : c);
}

/* port: "WYRM HOLLOW" is "W. HOLLOW": the score takes the HUD's room
   after the name (the original had a second HUD line for it) */
static const char *const area_names[6] = {
    "CAVERN", "MAGMA", "BEAST DEN", "W. HOLLOW", "CITADEL", "SURFACE"
};
static WORD hud_health = -1, hud_missiles = -1;
static int32_t hud_score = -1;
static char hud_score_s[12];
static uint8_t hud_score_n, hud_score_w, hud_sc;   /* digits, width to clear, bit n: buffer n redraws it */
static uint8_t hud_sc_wait;            /* frames left to wait for a quiet frame */
static SCB_REHV_PAL score_box;
static int8_t hud_ry = -1;
static uint8_t hud_hasm = 255;
static SCB_REHV_PAL hud_box[4], dot_s;

/* the energy bar and the numbers (a box of background behind each, so
   they redraw without the rest) */
static void hud_numbers(void)
{
    static WORD v;
    static uint8_t w;
    static char num[4];
    v = h_health < 0 ? 0 : h_health;
    w = h_max_energy > 0 ? (uint8_t)(((uint16_t)v << 5) / (uint16_t)h_max_energy) : 0;
    if (w > 32) w = 32;
    /* the bar: 32 pixels for the original's 64 */
    solid(&hud_box[0], 10, 1, 32, 3, COL_BG);
    solid(&hud_box[1], 10, 1, w ? w : 1, 3, h_health * 4 > h_max_energy ? COL_BEAM : COL_LAVA);
    if (!w) spr_hide(&hud_box[1], 1);
    solid(&hud_box[2], 45, 0, 12, 5, COL_BG);
    solid(&hud_box[3], 59, 0, 12, 5, COL_BG);
    spr_link(&hud_box[0], &hud_box[1]);
    spr_link(&hud_box[1], &hud_box[2]);
    spr_link(&hud_box[2], &hud_box[3]);
    spr_draw(&hud_box[0]);
    /* the energy, three digits (the original's %03ld) */
    num[0] = (char)('0' + v / 100);
    num[1] = (char)('0' + v / 10 % 10);
    num[2] = (char)('0' + v % 10);
    num[3] = 0;
    text_small(45, 0, COL_WHITE, num);
    if (h_has_missiles) {
        text_small(59, 0, COL_MISSILE, "M");
        num[0] = (char)('0' + h_missiles / 10 % 10);
        num[1] = (char)('0' + h_missiles % 10);
        num[2] = 0;
        text_small(63, 0, COL_WHITE, num);
    }
}

static uint8_t hud_num;             /* bit n: buffer n redraws the numbers */

/* hud_score_s = gs.score. port (speed): every score is 100s (a creature
   100, a boss 2000), so the digits of the hundreds are counted up (ltoa's
   32-bit divisions made a creature's last frame late) */
static uint8_t sdig[6];                 /* hundreds, units first */
static void score_digits(void)
{
    static uint8_t k, n;
    if (gs.score < hud_score || hud_score < 0) {
        memset(sdig, 0, sizeof sdig);
        hud_score = 0;
    }
    while (hud_score < gs.score) {
        hud_score += 100;
        for (k = 0; k < 6 && ++sdig[k] == 10; ++k) sdig[k] = 0;
    }
    n = 6;
    while (n && !sdig[n - 1]) --n;
    if (!n) { hud_score_s[0] = '0'; hud_score_s[1] = 0; return; }
    for (k = 0; n; ) hud_score_s[k++] = (char)('0' + sdig[--n]);
    hud_score_s[k++] = '0';
    hud_score_s[k++] = '0';
    hud_score_s[k] = 0;
}

static void hud(void)
{
    if (gs.room_y != hud_ry) {
        hud_ry = gs.room_y;
        hud_dirty = 3;
    }
    /* port (speed): a new score is drawn in a quiet frame (few sprites),
       within 2 s: on the kill's frame or among its sparks, the digits made
       a frame late */
    if (gs.score != hud_score && !hud_sc_wait) hud_sc_wait = 100;
    if (hud_sc_wait && (rd_objs < 8 || !--hud_sc_wait)) {
        hud_sc_wait = 0;
        score_digits();
        hud_score_w = hud_score_n;      /* (clear the old digits too: a new game's 0) */
        hud_score_n = (uint8_t)strlen(hud_score_s);
        if (hud_score_n > hud_score_w) hud_score_w = hud_score_n;
        hud_sc = 3;
    }
    if (h_health != hud_health || h_missiles != hud_missiles || h_has_missiles != hud_hasm) {
        hud_health = h_health;
        hud_missiles = h_missiles;
        hud_hasm = h_has_missiles;
        hud_num = 3;
    }
    if (hud_dirty & (1 << cur)) {
        hud_dirty &= (uint8_t)~(1 << cur);
        hud_num |= (uint8_t)(1 << cur);
        solid(&hud_box[0], 0, 0, 160, 6, COL_BG);
        solid(&hud_box[1], 9, 0, 34, 5, COL_METAL2);         /* the bar's frame */
        solid(&hud_box[2], 0, 5, 131, 1, COL_METAL2);        /* the line under the HUD */
        solid(&hud_box[3], 133, 0, 26, 6, COL_METAL);        /* the map of rooms, 8 x 6 */
        spr_link(&hud_box[0], &hud_box[1]);
        spr_link(&hud_box[1], &hud_box[2]);
        spr_link(&hud_box[2], &hud_box[3]);
        spr_draw(&hud_box[0]);
        text_small(1, 0, COL_WHITE, "PW");
        text_small(74, 0, COL_ITEM, area_names[gs.room_y < 0 ? 0 : gs.room_y > 5 ? 5 : gs.room_y]);
        hud_sc |= (uint8_t)(1 << cur);
    }
    /* the score, right-aligned before the map (a box of background behind
       it, so it redraws without the rest). The original's fix (amiga_games
       #50): kept but never shown (a leftover debug line was where it is) */
    if (hud_sc & (1 << cur)) {
        hud_sc &= (uint8_t)~(1 << cur);
        solid(&score_box, 131 - 4 * hud_score_w, 0, 4 * hud_score_w, 5, COL_BG);
        spr_draw(&score_box);
        text_small(131 - 4 * hud_score_n, 0, COL_WHITE, hud_score_s);
    }
    if (hud_num & (1 << cur)) {
        hud_num &= (uint8_t)~(1 << cur);
        hud_numbers();
    }
    /* the current room on the map, flashing */
    spr_box(&dot_s, 134 + gs.room_x * 3, gs.room_y, 2, 1,
            ((uint8_t)gs.frame & 4) ? COL_WHITE : COL_HUNTER);
    spr_draw(&dot_s);
}

/* ------------------------------------------------------------ frames */

void render_play(void)
{
    static uint8_t p, i;
    /* the lava's wave (draw_tile: g_frame / 4) and the save point's flash (/ 8) */
    p = ((uint8_t)gs.frame >> 2) & 3;
    if (p != lava_phase) {
        lava_phase = p;
        save_phase = (uint8_t)(((uint8_t)gs.frame >> 3) & 1);
        for (i = 0; i < an_n; ++i) render_tile_changed(an_x[i], an_y[i]);
    }
    repaint();
    objects();
    hud();
}

static SCB_REHV_PAL msg_box[3];

static void framed_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t edge)
{
    solid(&msg_box[0], x, y, w, h, edge);
    solid(&msg_box[1], x + 1, y + 1, w - 2, h - 2, COL_BG);
    spr_link(&msg_box[0], &msg_box[1]);
    spr_draw(&msg_box[0]);
}

void render_item_box(const char *name, uint8_t blink)
{
    static char buf[24];
    /* the original's 200 x 60 box, centred */
    framed_box(30, 39, 100, 24, COL_ITEM);
    strcpy(buf, "GOT ");
    strcat(buf, name);
    strcat(buf, "!");
    text_small_centre(44, COL_WHITE, buf);
    if (blink) text_small_centre(55, COL_METAL2, "PRESS A");
}

void render_paused(void)
{
    framed_box(44, 40, 72, 22, COL_METAL2);
    text_small_centre(44, COL_WHITE, "PAUSED");
    text_small_centre(54, COL_METAL2, "OPTION 1: TITLE");
}

static SCB_REHV_PAL big_s, line_s;

/* a word of the original's font, twice the size, centred at row y */
static void big_word(const uint8_t *data, uint8_t w, uint8_t y, uint8_t c, uint8_t line_c)
{
    uint8_t x = (uint8_t)((160 - w * 2) / 2);
    spr_init(&big_s, data, 1);
    spr_color(&big_s, c);
    spr_scale(&big_s, SPR_SCALE_2, SPR_SCALE_2);
    spr_pos(&big_s, x, y);
    /* the underline */
    spr_box(&line_s, x, y + 16, w * 2, 1, line_c);
    spr_link(&big_s, &line_s);
    spr_draw(&big_s);
}

/* the title and GAME OVER are drawn in full into both buffers when they
   come up (render_screen_new), then only their blinking line */
static uint8_t screen_full;
static SCB_REHV_PAL blink_s;

void render_screen_new(void)
{
    screen_full = 2;
}

static uint8_t blink_line(uint8_t y, const char *s, uint8_t blink)
{
    if (screen_full) return 1;
    solid(&blink_s, 0, y, 160, 5, COL_BG);
    spr_draw(&blink_s);
    if (blink) text_small_centre(y, COL_WHITE, s);
    return 0;
}

void render_title(uint8_t blink)
{
    if (!blink_line(42, "PRESS A TO START", blink)) return;
    --screen_full;
    gfx_clear(COL_BG);
    big_word(logo_data, LOGO_W, 7, COL_HUNTER, COL_HUNTER2);
    text_small_centre(28, COL_ITEM, "THE LOST CAVERNS");
    if (blink) text_small_centre(42, COL_WHITE, "PRESS A TO START");
    text_small(22, 55, COL_METAL2, "CONTROLS:");
    text_small(22, 62, COL_METAL2, "LEFT/RIGHT - MOVE");
    text_small(22, 68, COL_METAL2, "UP - JUMP / AIM UP");
    text_small(22, 74, COL_METAL2, "DOWN - ROLL FORM");
    text_small(22, 80, COL_METAL2, "A - SHOOT/BOMB");
    /* port: original bug: its title says "START - MISSILE MODE", but a
       rocket is up + fire (game.c update_hunter) and START does nothing in
       play */
    text_small(22, 86, COL_METAL2, "UP+A - ROCKET");
    text_small(2, 96, COL_ROCK2, "V1.0 - 2026");
}

void render_gameover(uint8_t blink, const char *score)
{
    if (!blink_line(70, "PRESS A TO CONTINUE", blink)) return;
    --screen_full;
    gfx_clear(COL_BG);
    big_word(gameover_data, GAMEOVER_W, 30, COL_LAVA, COL_LAVA2);
    /* port: the original keeps a score but never shows it; here it is */
    text_small_centre(55, COL_METAL2, score);
    if (blink) text_small_centre(70, COL_WHITE, "PRESS A TO CONTINUE");
}

void render_present(void)
{
    gfx_present();
    cur ^= 1;
}

void render_init(void)
{
    uint8_t i;
    gfx_init();
    gfx_set_rate(50);               /* the original's PAL 50 Hz */
    gfx_palette(palette);
    for (i = 0; i < 96; ++i) row6[i] = i / 6;
    i = 0;
    do sy_tab[i] = (uint8_t)(PF_Y + (((uint16_t)i * 3) >> 3)); while (++i);
    for (i = 0; i < ROOM_H; ++i) cell_y[i] = PF_Y + i * 6;
    for (i = 0; i < POOL; ++i) {
        spr_init(&pool[i], tiles_data_0, 4);
        pool[i].next = (char *)&pool[i + 1];
    }
    spr_init(&hunter_s, hero_data, 4);
    for (i = 0; i < MAX_ENEMIES; ++i) spr_init(&enemy_s[i], enemies_data_0, 4);
    for (i = 0; i < 4; ++i) spr_init(&item_s[i], enemies_data_0, 4);
    for (i = 0; i < MAX_BULLETS; ++i) spr_init(&bullet_s[i], enemies_data_0, 4);
    for (i = 0; i < NBOX; ++i) spr_box(&box_s[i], 0, 0, 1, 1, COL_WHITE);
    for (i = 0; i < MAX_PARTICLES; ++i) spr_box(&spark_s[i], 0, 0, 1, 1, COL_WHITE);
    gfx_clear(COL_BG);
    gfx_present();
    gfx_clear(COL_BG);
}
