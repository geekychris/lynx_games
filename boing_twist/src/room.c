/*
 * room.c - the room round the ball, the ball's shadow, the HUD strips, the
 * band markers, the title's Boing ball and logo.
 *
 * A room is a list of Suzy shapes made on the host from the original's
 * theme (tools/gen_data.py): boxes, and trapezoids (a 1-pixel sprite with
 * Suzy's stretch and tilt: the floor in perspective, the floor's grid lines
 * running away from you), with two marks where the shadow goes (on the back
 * wall, before the floor is drawn over its foot; or on the floor). They are
 * drawn in batches of SCBs.
 *
 * Screen colours: 0 background, 1 back wall, 2 floor, 3 grid, 4 shadow,
 * 5 text and markers, 6-15 the ball (ball.c) or the title's Boing ball.
 */
#include <string.h>
#include <tgi.h>
#include "lynxgame.h"
#include "gfx_data.h"
#include "room.h"

extern uint8_t texl[];                   /* ball.c: the title uses its place (no picture shows) */

#pragma static-locals (on)

#define POOL 2                             /* the HUD strips, panels (roomasm.s draws the room) */
static SCB_REHVST_PAL pool[POOL];
static uint8_t npool;
RoomShadow room_shadow;

static void flush(void)
{
    if (npool) {
        pool[npool - 1].next = 0;
        spr_draw(&pool[0]);
        npool = 0;
    }
}

static SCB_REHVST_PAL *shape(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t c)
{
    SCB_REHVST_PAL *s;
    if (npool == POOL) flush();
    s = &pool[npool];
    if (npool) pool[npool - 1].next = (char *)s;
    ++npool;
    s->sprctl0 = BPP_1 | TYPE_BACKNONCOLL;    /* draws colour 0 too (normal types skip it) */
    s->sprctl1 = PACKED | REHVST;
    s->sprcoll = NO_COLLIDE;
    s->next = 0;
    s->data = (unsigned char *)spr_pixel_data;
    s->hpos = x;
    s->vpos = y;
    s->hsize = w;
    s->vsize = h;
    s->stretch = 0;
    s->tilt = 0;
    s->penpal[0] = c;
    memset(s->penpal + 1, 0, 7);
    return s;
}

/* the shadow's disc, scaled: (cx, cy) its middle, rx/ry half-axes x16 */
static SCB_REHV_PAL sh_scb;
static void shadow_disc(int16_t cx, int16_t cy, uint16_t rx16, uint16_t ry16)
{
    spr_init(&sh_scb, shadow_data, 1);
    spr_color(&sh_scb, 4);
    sh_scb.hpos = cx - (int16_t)(rx16 >> 4);
    sh_scb.vpos = cy - (int16_t)(ry16 >> 4);
    sh_scb.hsize = (uint16_t)(rx16 * 2 * 16 / SHADOW_N);       /* 2r pixels over SHADOW_N, 8.8 */
    sh_scb.vsize = (uint16_t)(ry16 * 2 * 16 / SHADOW_N);
    spr_draw(&sh_scb);
}

uint8_t room_buf[ROOM_BUF_SIZE];          /* the room's shapes (a cartridge file) */
static uint8_t loaded_room = 0xFF;

const uint8_t *__fastcall__ room_shapes(const uint8_t *p);   /* roomasm.s */

void room_draw(uint8_t r)
{
    const uint8_t *p = room_buf;
    if (r != loaded_room) {
        lynx_load(ROOM_FILE + r);
        loaded_room = r;
    }
    for (;;) {
        p = room_shapes(p);                      /* boxes, trapezoids, row patterns */
        switch (*p) {
        case 3:                                  /* line x1 y1 x2 y2 pen */
            gfx_line(p[1], p[2], p[3], p[4], p[5]);
            p += 6;
            break;
        case 4:                                  /* the shadow on the back wall goes here */
        case 5:                                  /* or on the floor */
            if (room_shadow.on && rooms[r].wall == (*p == 4))
                shadow_disc(room_shadow.cx, room_shadow.cy, room_shadow.rx16, room_shadow.ry16);
            ++p;
            break;
        default:                                 /* 0: the end */
            return;
        }
    }
}

/* the room's colours: 0-4, and the text's (5): dark on a light room */
void room_palette(uint8_t r)
{
    const uint16_t *pal = rooms[r].pal;
    uint8_t i;
    uint16_t c, lum;
    for (i = 0; i < 5; ++i) {
        c = pal[i];
        gfx_color(i, (uint8_t)(c >> 8), (uint8_t)((c >> 4) & 15), (uint8_t)(c & 15));
    }
    c = pal[0];
    lum = ((c >> 8) & 15) * 3 + ((c >> 4) & 15) * 6 + (c & 15);
    if (lum > 80) gfx_color(5, 2, 0, 3);
    else gfx_color(5, 15, 15, 15);
}

/* the HUD strips over the top and bottom of the room */
void hud_strips(void)
{
    shape(0, 0, 160u << 8, (uint16_t)HUD_TOP << 8, 0);
    shape(0, HUD_BOT, 160u << 8, (uint16_t)(102 - HUD_BOT) << 8, 0);
    flush();
}

void hud_strip(uint8_t bottom)
{
    if (bottom) shape(0, HUD_BOT, 160u << 8, (uint16_t)(102 - HUD_BOT) << 8, 0);
    else shape(0, 0, 160u << 8, (uint16_t)HUD_TOP << 8, 0);
    flush();
}

/* The band markers: 2-pixel bars either side of the ball, written straight
   into the screen (play is single-buffered): markers_save keeps what the
   room has in those two byte columns, markers_put puts it back and draws
   the bars on rows top..bot (top > bot: none). xl, xr even. */
#define SCREEN ((uint8_t *)0xC038)          /* page 1: the one shown while playing */
static uint8_t mk_save[2][HUD_BOT - HUD_TOP];
static uint8_t mk_xl, mk_xr;
void markers_save(uint8_t xl, uint8_t xr)
{
    uint8_t y;
    uint8_t *p = SCREEN + HUD_TOP * 80;
    mk_xl = xl >> 1;
    mk_xr = xr >> 1;
    for (y = 0; y < HUD_BOT - HUD_TOP; ++y, p += 80) {
        mk_save[0][y] = p[mk_xl];
        mk_save[1][y] = p[mk_xr];
    }
}

void markers_put(int16_t top, int16_t bot)
{
    uint8_t y;
    uint8_t *p = SCREEN + HUD_TOP * 80;
    for (y = HUD_TOP; y < HUD_BOT; ++y, p += 80) {
        if ((int16_t)y >= top && (int16_t)y <= bot) {
            p[mk_xl] = 0x55;
            p[mk_xr] = 0x55;
        } else {
            p[mk_xl] = mk_save[0][y - HUD_TOP];
            p[mk_xr] = mk_save[1][y - HUD_TOP];
        }
    }
}

/* ------------------------------------------------------------ the title ball */
/* the sub-sprites' SCBs follow their data in the texture's place: sorted by
   light step, the first of each step with the step's pen palette, the rest
   reusing it (REUSEPAL) */
#define tb_head ((SCB_REHV_PAL *)(texl + TITLE_SIZE))
#define tb_rest ((SCB_REHV *)(texl + TITLE_SIZE + TB_LEVELS * sizeof(SCB_REHV_PAL)))
static void *tb_first;
/* per phase, the pen palette's nibbles: 1 where the pen is white */
static uint8_t tb_pat[TB_PHASES][8];

/* the sub-sprites' data from the cartridge, their SCBs */
void title_ball_init(void)
{
    uint8_t i, lv = 0xFF, ph, p, k;
    void *prev = 0, *cur;
    lynx_load(TITLE_FILE);
    for (i = 0; i < TB_N; ++i) {
        const TbSub *sub = &tb_subs[i];
        if (sub->level != lv) {
            lv = sub->level;
            spr_init(&tb_head[lv], texl + sub->off, 4);
            cur = &tb_head[lv];
        } else {
            spr_init_nopal(&tb_rest[i], texl + sub->off, 4);
            cur = &tb_rest[i];
        }
        if (prev) ((SCB_REHV *)prev)->next = (char *)cur;
        else tb_first = cur;
        prev = cur;
    }
    /* pen p (1..14) is white when (p - 1 + ph) mod 14 >= 7 */
    memset(tb_pat, 0, sizeof(tb_pat));
    for (ph = 0; ph < TB_PHASES; ++ph)
        for (p = 1; p <= TB_PHASES; ++p) {
            k = (uint8_t)(p - 1 + ph);
            if (k >= TB_PHASES) k -= TB_PHASES;
            if (k >= TB_PHASES / 2) tb_pat[ph][p >> 1] |= (uint8_t)(p & 1 ? 1 : 0x10);
        }
}

/* the title's colours: screen 6 + 2k red, 7 + 2k white for light step k */
void title_ball_palette(void)
{
    uint8_t k;
    for (k = 0; k < TB_LEVELS; ++k) {
        uint16_t r = tb_red[k], w = tb_white[k];
        gfx_color((uint8_t)(6 + 2 * k), (uint8_t)(r >> 8), (uint8_t)((r >> 4) & 15), (uint8_t)(r & 15));
        gfx_color((uint8_t)(7 + 2 * k), (uint8_t)(w >> 8), (uint8_t)((w >> 4) & 15), (uint8_t)(w & 15));
    }
}

/* Draw it with its middle at (cx, cy), turned to phase ph (0..13): the
   demo's colour cycling, done with the sub-sprites' pen palettes (one per
   light step: the others reuse it) */
void title_ball_draw(int16_t cx, int16_t cy, uint8_t ph)
{
    uint8_t i, k, base;
    const uint8_t *pat = tb_pat[ph];
    SCB_REHV *t = (SCB_REHV *)tb_first;
    const TbSub *sub = tb_subs;
    for (k = 0; k < TB_LEVELS; ++k) {
        uint8_t *pp = tb_head[k].penpal;
        base = (uint8_t)((6 + 2 * k) * 0x11);
        for (i = 0; i < 8; ++i) pp[i] = (uint8_t)(pat[i] + base);
        pp[0] &= 0x0F;                       /* pen 0 -> colour 0: transparent (Suzy looks after the mapping) */
    }
    for (i = 0; i < TB_N; ++i, ++sub) {
        t->hpos = cx + sub->dx;
        t->vpos = cy + sub->dy;
        t = (SCB_REHV *)t->next;
    }
    spr_draw(tb_first);
}

/* ------------------------------------------------------------ the logo */
static SCB_REHV_PAL logo_scb;

/* BOING TWIST, scaled by Suzy (8.8), centred on x = 80, top at y */
void logo_draw(int16_t y, uint16_t scale, uint8_t c, uint8_t shade)
{
    spr_init(&logo_scb, logo_data, 2);
    logo_scb.penpal[0] = (uint8_t)((0 << 4) | c);    /* pen 1 the letters, pen 2 the shadow */
    logo_scb.penpal[1] = (uint8_t)(shade << 4);
    logo_scb.hsize = scale;
    logo_scb.vsize = scale;
    logo_scb.hpos = 80 - (int16_t)(((uint32_t)LOGO_W * scale) >> 9);
    logo_scb.vpos = y;
    spr_draw(&logo_scb);
}

/* a solid box over the room (panels) */
void panel(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t c)
{
    shape(x, y, (uint16_t)w << 8, (uint16_t)h << 8, c);
    flush();
}
