/*
 * ball.c - the ball's texture, its picture, its moves' animations and its
 * sprite.
 *
 * The texture is 64 x 32 texels round the ball (longitude east from 0,
 * latitude from the north pole), made from the puzzle's state: texel (u,
 * v) is in slot (band, seg); the piece there is home tile (hb, hs) turned
 * or not, so the texel is the picture's at the same place in tile (hb,
 * hs), read backwards if it is turned. Slots and tiles are cut at
 * round(s * 64 / S) (Medium's 12 segments are 5 or 6 texels wide), so a
 * texel's place in its tile is kept in 16ths. The first texel column and
 * row of each slot are the groove (the original draws its grooves into the
 * picture too, darker). A texel is 4 | c (c: 0 the groove, 1-3 the
 * picture's colours); view_play (ballasm.s) turns it into the pixel's pen.
 *
 * port: the original wraps a 1024 x 512 picture on a mesh lit per pixel;
 * here 64 x 32 texels in three colours, lit in three steps per pixel
 * (tools/gen_data.py), drawn from tables made on the host (src/view.s)
 * into a 4-bit Suzy sprite.
 */
#include <string.h>
#include "lynxgame.h"
#include "bt_core.h"
#include "gfx_data.h"
#include "ball.h"

#pragma static-locals (on)

uint8_t texl[TEX_H * TEX_ROW];
uint8_t pic_buf[512];                     /* the picture: 64 x 32 texels, 2 bits (lynx_load) */
#define tw_save HI_TWSAVE                 /* a twisting band's rows (in the second screen buffer: play only) */
static uint8_t tw_v0, tw_n;

/* the grid's tables */
/* the grid's tables (ballasm.s tex_rows reads them) */
uint8_t colslot[TEX_W], fracu[TEX_W], groove_u[TEX_W];
uint8_t rowslot[TEX_H], fracv[TEX_H], groove_v[TEX_H];
uint8_t srcu[BT_MAX_S * 16], srcv[BT_MAX_B * 16];
uint8_t home_hb16[BT_MAX_T], home_hs16[BT_MAX_T];
uint8_t slot_u0[BT_MAX_S + 1];           /* col0(s) */
uint8_t cur_S, cur_B;
static uint8_t cur_pic = 0xFF;
uint8_t ball_pic_colours;                 /* picture colours changed: re-make the palette */

/* where segment s and band b start, in texels */
static uint8_t col0(uint8_t s) { return (uint8_t)(((uint16_t)s * TEX_W * 2 / cur_S + 1) >> 1); }
static uint8_t row0(uint8_t b) { return (uint8_t)(((uint16_t)b * TEX_H * 2 / cur_B + 1) >> 1); }

static void grid_tables(uint8_t B, uint8_t S)
{
    uint8_t s, b, u, v, f, w, a, h;
    if (S == cur_S && B == cur_B) return;
    cur_S = S;
    cur_B = B;
    for (b = 0, h = 0; b < B; ++b)
        for (s = 0; s < S; ++s, ++h) {
            home_hb16[h] = (uint8_t)(b * 16);
            home_hs16[h] = (uint8_t)(s * 16);
        }
    for (s = 0; s < S; ++s) {
        a = col0(s);
        w = (uint8_t)(col0(s + 1) - a);
        for (u = a; u < a + w; ++u) {
            colslot[u] = s;
            fracu[u] = (uint8_t)(((uint16_t)(u - a) * 32 + 16) / w >> 1);    /* the texel's middle, 16ths */
            groove_u[u] = u == a && w > 4;           /* port: no grooves on Hard's 4-texel tiles (as the a8) */
        }
        for (f = 0; f < 16; ++f) srcu[s * 16 + f] = (uint8_t)(a + ((uint16_t)f * w >> 4));
        slot_u0[s] = a;
    }
    slot_u0[S] = TEX_W;
    for (b = 0; b < B; ++b) {
        a = row0(b);
        w = (uint8_t)(row0(b + 1) - a);
        for (v = a; v < a + w; ++v) {
            rowslot[v] = b;
            fracv[v] = (uint8_t)(((uint16_t)(v - a) * 32 + 16) / w >> 1);
            groove_v[v] = v == a && w > 4;
        }
        for (f = 0; f < 16; ++f) srcv[b * 16 + f] = (uint8_t)(a + ((uint16_t)f * w >> 4));
    }
}

static const uint8_t shift_of[4] = { 6, 4, 2, 0 };
static uint8_t pic_texel(uint8_t x, uint8_t y)
{
    return (uint8_t)((pic_buf[(uint16_t)y * 16 + (x >> 2)] >> shift_of[x & 3]) & 3);
}

/* the packed picture: the Boing checkerboard made for the grid (red 2,
   white 3), or one from the cartridge */
static void load_picture(uint8_t pic)
{
    uint8_t x, y, c;
    if (pic == 0) {
        uint8_t *d = pic_buf;
        for (y = 0; y < TEX_H; ++y)
            for (x = 0; x < TEX_W; x += 4, ++d) {
                uint8_t byte = 0, k;
                for (k = 0; k < 4; ++k) {
                    c = ((colslot[x + k] + rowslot[y]) & 1) ? 2 : 3;
                    byte = (uint8_t)((byte << 2) | c);
                }
                *d = byte;
            }
    } else
        lynx_load(pic);                   /* cartridge file n: picture n (src/directory.s) */
}

/* what tile h looks like, upright and turned: its picture sampled 4 x 4 */
static void signatures(BtPuzzle *p)
{
    uint8_t h, i, j, hb, hs, x, y, c, ub[4], tb[4];
    for (h = 0; h < (uint8_t)(p->bands * p->segs); ++h) {
        hb = home_hb16[h];
        hs = home_hs16[h];
        for (j = 0; j < 4; ++j) {
            uint8_t u8 = 0, t8 = 0;
            y = srcv[hb + j * 4 + 2];
            for (i = 0; i < 4; ++i) {
                x = srcu[hs + i * 4 + 2];
                c = pic_texel(x, y);
                u8 = (uint8_t)((u8 << 2) | c);
                t8 = (uint8_t)((t8 >> 2) | (c << 6));
            }
            ub[j] = u8;
            tb[3 - j] = t8;
        }
        bt_set_sig(p, h, *(uint32_t *)ub + 1, *(uint32_t *)tb + 1);
    }
}

/* the picture on a grid (the tables, the picture in pic_buf) */
void ball_picture(uint8_t pic, uint8_t bands, uint8_t segs)
{
    uint8_t regrid = (uint8_t)(segs != cur_S || bands != cur_B);
    grid_tables(bands, segs);
    if (pic != cur_pic || (pic == 0 && regrid)) {
        load_picture(pic);
        ball_pic_colours = 1;
    }
    cur_pic = pic;
}

void ball_signatures(BtPuzzle *p) { signatures(p); }

/* rows v0..v1-1 of the texture from the state (ident: as solved, the peek;
   p is then not read): ballasm.s tex_rows */
void ball_texture_rows(const BtPuzzle *p, uint8_t v0, uint8_t v1, uint8_t ident)
{
    tx_args.v0 = v0;
    tx_args.v1 = v1;
    tx_args.ident = ident;
    tx_args.slots = p ? (const uint8_t *)p->slot : 0;
    tx_args.s0 = 0;
    tx_args.s1 = cur_S;
    tex_rows();
}

/* after a flip: only half k's slots changed */
void ball_texture_half(const BtPuzzle *p, uint8_t k)
{
    uint8_t e = (uint8_t)(k + cur_S / 2);
    tx_args.v0 = 0;
    tx_args.v1 = TEX_H;
    tx_args.ident = 0;
    tx_args.slots = (const uint8_t *)p->slot;
    tx_args.s0 = k;
    tx_args.s1 = e > cur_S ? cur_S : e;
    tex_rows();
    if (e > cur_S) {
        tx_args.s0 = 0;
        tx_args.s1 = (uint8_t)(e - cur_S);
        tex_rows();
    }
}

void ball_texture(const BtPuzzle *p, uint8_t ident) { ball_texture_rows(p, 0, TEX_H, ident); }

void ball_band_rows(uint8_t band, uint8_t *v0, uint8_t *v1)
{
    *v0 = row0(band);
    *v1 = row0((uint8_t)(band + 1));
}

/* a band's twist: its rows saved at the start, shifted d texels east */
void ball_twist_begin(uint8_t band)
{
    uint8_t v;
    tw_v0 = row0(band);
    tw_n = (uint8_t)(row0((uint8_t)(band + 1)) - tw_v0);
    for (v = 0; v < tw_n; ++v) memcpy(tw_save + v * TEX_W, texl + (uint16_t)(tw_v0 + v) * TEX_ROW, TEX_W);
}

void ball_twist_shift(int8_t d)
{
    tw_args.src = tw_save;
    tw_args.v0 = tw_v0;
    tw_args.n = tw_n;
    tw_args.shift = (uint8_t)(-d) & 63;
    band_rotate();
}

/* the turn (Y) that puts half k's middle in front: u_mid - 16 (gen_data.py) */
uint8_t ball_half_turn(uint8_t k)
{
    uint16_t mid2 = (uint16_t)(2 * k + cur_S / 2) * TEX_W / cur_S;   /* 2 x the middle's texel */
    return (uint8_t)(((mid2 + 1) >> 1) - 16) & 63;
}

uint8_t ball_cur_segs(void) { return cur_S; }

/* ------------------------------------------------------------ the sprite */
SCB_REHV_PAL ball_scb;

/* the picture's three colours lit three ways: screen colours 7-9 dark,
   10-12 lit, 13-15 the highlight; 6 the groove (render.c: "a groove that
   shows on dark pictures as well as light ones") */
void ball_palette(const uint16_t *col)
{
    uint8_t c, k, r, g, b, i;
    static const uint8_t mul[3] = { 10, 15, 16 };    /* dark, lit, highlight (x/16) */
    for (c = 0; c < 3; ++c) {
        uint16_t v = col[c];
        for (k = 0; k < 3; ++k) {
            r = (uint8_t)(((v >> 8) & 15) * mul[k] >> 4);
            g = (uint8_t)(((v >> 4) & 15) * mul[k] >> 4);
            b = (uint8_t)((v & 15) * mul[k] >> 4);
            if (k == 2) {                    /* the highlight: towards white */
                r = (uint8_t)((r + 15 + 1) >> 1);
                g = (uint8_t)((g + 15 + 1) >> 1);
                b = (uint8_t)((b + 15 + 1) >> 1);
            }
            i = (uint8_t)(7 + k * 3 + c);
            gfx_color(i, r, g, b);
        }
    }
    gfx_color(6, 3, 3, 4);
}

void ball_init(void)
{
    static const uint8_t pens[8] = { 0x00, 0x00, 0x6A, 0xBC, 0x67, 0x89, 0x6D, 0xEF };
    spr_init(&ball_scb, ball_spr, 4);
    memcpy(ball_scb.penpal, pens, 8);
}

/* draw the ball (already rendered) with its middle at (cx, cy), scale 8.8 */
void ball_draw(int16_t cx, int16_t cy, uint16_t scale)
{
    ball_scb.hpos = cx + (int16_t)(((int32_t)BALL_X0 * (int16_t)scale) >> 8);
    ball_scb.vpos = cy + (int16_t)(((int32_t)BALL_Y0 * (int16_t)scale) >> 8);
    ball_scb.hsize = scale;
    ball_scb.vsize = scale;
    spr_draw(&ball_scb);
}
