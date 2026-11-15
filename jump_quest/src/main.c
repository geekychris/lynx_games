/*
 * Jump Quest (Atari Lynx). Port of amiga_mcp/examples/jump_quest.
 *
 * jq.c is the original's game logic (by way of the Spectrum and Atari 8-bit
 * ports, with their asm for the enemies, the box tests and the tile
 * lookups); this file is the Lynx front end: the view, Suzy sprites, the
 * title, the HUD, game over, pause, the log lines the test reads.
 *
 * The view: the original's 320 x 224 playfield (20 x 14 tiles of 16) at
 * half width and 7/16 height: tiles are 8 x 7, all 14 rows fit in 98 lines
 * and the HUD takes the bottom 6 lines (102). It scrolls a Lynx pixel (two
 * world pixels) at a time, following the hero.
 *
 * Every frame draws one chain of Suzy sprites: the tiles in view (sky is
 * not drawn), the items, the enemies, the hero, the particles. The screen
 * is not cleared: there is a chain for each of the two screen buffers, and
 * before a buffer's chain is rebuilt it is drawn once more with every pen
 * mapped to the sky's colour, which erases exactly what that buffer shows.
 * The chain's first sprite loads the pens; the others reuse them
 * (SCB_RENONE + REUSEPAL), so erasing is changing one pen palette.
 *
 * The logic runs at the original's PAL rate, one tick a frame at 50 Hz
 * (gfx_set_rate(50)). A late frame slows the game for that frame rather
 * than running two ticks in the next one (two ticks and a frame's drawing
 * don't fit in a frame, so catching up made every frame late).
 */
#include <string.h>
#include <stdlib.h>
#include "lynxgame.h"
#include "jq.h"
#include "tmap.h"
#include "art.h"
#include "sound.h"

#pragma static-locals (on)

/* the original's palette indices (game.h) */
#define COL_SKY      1      /* port: the Lynx's pen 0 is transparent: sky is colour 1 (and 0) */
#define COL_BROWN    2
#define COL_DKBROWN  3
#define COL_GREEN    4
#define COL_DKGREEN  5
#define COL_GREY     9
#define COL_BLUE     10
#define COL_WHITE    11
#define COL_BLACK    12
#define COL_YELLOW   13
#define COL_RED      14

#define HUD_Y 96
#define VIEW_W 320                          /* world pixels across */

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

/* ------------------------------------------------------------ the sprite chains */

/* a level's tile sprites: single tiles, and runs of 2-4 identical ground,
   grass, brick, stone or platform tiles as one (art_strip): level 2's 426
   tiles are 145 sprites */
#define MAX_LEVEL_TILES 160
#define N_BODY (MAX_ITEMS + MAX_ENEMIES + 1 + MAX_PARTICLES)

static const uint8_t blank_data[] = { 2, 0x00, 0 };     /* one pen-0 pixel pair */
static const uint8_t pens_erase[8] = { 0x01, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 };

SCB_REHV_PAL heads[2];                      /* one chain per screen buffer */
SCB_RENONE body[2][N_BODY];                 /* its items, enemies, hero, particles */
SCB_RENONE tscb[MAX_LEVEL_TILES];           /* the level's tiles (both chains link them) */
static SCB_RENONE *body_end[2];             /* each chain's last body SCB + 1 */
static int16_t chain_hoff[2];               /* each chain's HOFF */
static uint8_t cur;                         /* the chain of the back buffer */
static SCB_RENONE *bp;                      /* the next free SCB while building */
static uint8_t full_clear;                  /* clear (not erase) the next n frames */
int16_t view_x;                             /* world pixel at the screen's left (even) */
uint8_t n_tiles, n_sprites;                 /* the last chain's (for the log and tests) */
uint16_t n_level_tiles;

/* tiles.s */
extern void tiles_span(void);
extern void tiles_link(void);
extern void tiles_unlinked(void);
extern void build_items(void);
extern void build_parts(void);
extern SCB_RENONE *bt_scb;                  /* in: first free SCB; out: the next free one */
extern uint8_t bt_col;                      /* first column in view */
extern int8_t bt_x;                         /* its screen x (0..-7) */
extern int16_t bt_hoff;                     /* the chain's HOFF (added to the objects' x) */
extern uint8_t bo_noenemies;
extern uint8_t row_list[MAX_LEVEL_TILES + TILES_Y];   /* each row's tile sprites' last columns, $FF after each row */
extern uint8_t rowl_lo[TILES_Y], rowl_hi[TILES_Y];    /* where each row's list starts */
extern uint8_t row_cur[TILES_Y];
extern uint8_t span_a[2][TILES_Y], span_b[2][TILES_Y];
extern uint8_t lk_set;
extern void *lk_head, *lk_tail;
extern uint8_t row_addr_lo[TILES_Y], row_addr_hi[TILES_Y];

static void chains_init(void)
{
    uint8_t s;
    uint16_t i;
    for (s = 0; s < 2; ++s) {
        spr_init(&heads[s], blank_data, 4);
        heads[s].sprctl0 = BPP_4 | TYPE_NORMAL;
        for (i = 0; i < N_BODY; ++i) {
            body[s][i].sprctl0 = BPP_4 | TYPE_NORMAL;
            body[s][i].sprctl1 = LITERAL | RENONE | REUSEPAL;
            body[s][i].sprcoll = NO_COLLIDE;
        }
    }
}

/* a level (re)started: the SCBs of its tiles at their places in the level,
   and each row's list of them (by last column) for tiles_span */
static uint8_t strip_index(uint8_t t)
{
    switch (t) {
    case TILE_GROUND: return 0;
    case TILE_GRASS: return 1;
    case TILE_BRICK: return 2;
    case TILE_STONE: return 3;
    case TILE_PLATFORM: return 4;
    }
    return 0xFF;
}

static void rows_index(void)
{
    uint8_t r, c, t, n, k;
    const uint8_t *row;
    SCB_RENONE *s = tscb;
    uint8_t *l = row_list;
    for (r = 0; r < TILES_Y; ++r) {
        row = tmap_row[r];
        row_addr_lo[r] = (uint8_t)(uint16_t)s;
        row_addr_hi[r] = (uint8_t)((uint16_t)s >> 8);
        rowl_lo[r] = (uint8_t)(uint16_t)l;
        rowl_hi[r] = (uint8_t)((uint16_t)l >> 8);
        for (c = 0; c < tmap_w; c += n) {
            t = row[c];
            n = 1;
            if (!t || s == tscb + MAX_LEVEL_TILES) continue;
            s->data = (unsigned char *)art_tiles[t];
            k = strip_index(t);
            if (k != 0xFF) {
                while (n < 4 && c + n < tmap_w && row[c + n] == t) ++n;
                if (n > 1) s->data = (unsigned char *)art_strip[k][n - 2];
            }
            *l++ = c + n - 1;
            s->sprctl0 = BPP_4 | TYPE_NORMAL;
            s->sprctl1 = LITERAL | RENONE | REUSEPAL;
            s->sprcoll = NO_COLLIDE;
            s->hpos = (int16_t)c << 3;
            s->vpos = r * 7;
            s->next = (char *)(s + 1);
            ++s;
        }
        *l++ = 0xFF;
        row_cur[r] = 0;
        span_a[0][r] = span_b[0][r] = span_a[1][r] = span_b[1][r] = 0;
    }
    n_level_tiles = (uint16_t)(s - tscb);
    tiles_unlinked();
}

/* a ? block was hit: its tile's picture */
static void tile_changed(uint8_t tx, uint8_t ty)
{
    uint8_t k;
    SCB_RENONE *s = (SCB_RENONE *)(row_addr_lo[ty] | (uint16_t)row_addr_hi[ty] << 8);
    const uint8_t *l = (const uint8_t *)(rowl_lo[ty] | (uint16_t)rowl_hi[ty] << 8);
    for (k = 0; l[k] != 0xFF; ++k)
        if (l[k] == tx) {
            s[k].data = (unsigned char *)art_tiles[TMAP(tx, ty)];
            break;
        }
}

/* add a sprite to the body being built (x relative to the view) */
static void emit(const unsigned char *data, int16_t x, int16_t y)
{
    if (x <= -8 || x >= 160 || y >= HUD_Y || y <= -14) return;
    bp->sprctl0 = BPP_4 | TYPE_NORMAL;
    bp->data = (unsigned char *)data;
    bp->hpos = x + bt_hoff;
    bp->vpos = y;
    bp->next = (char *)(bp + 1);
    ++bp;
}

/* world y -> screen y (7/16) */
static int16_t sy_of(int16_t wy)
{
    return ((wy << 3) - wy) >> 4;
}

/* link chain `set`: head -> its tiles in view -> its body */
static void link_chain(uint8_t set)
{
    SCB_RENONE *start = body[set];
    lk_set = set;
    lk_head = &heads[set];
    lk_tail = body_end[set] == start ? 0 : start;
    tiles_link();
}

/* draw chain `set` (HOFF is its own; TGI wants 0) */
static void draw_chain(uint8_t set)
{
    /* Suzy's 16-bit registers: the low byte first (writing it clears the high) */
    LYNX_REG8(0xFC04) = (uint8_t)chain_hoff[set];
    LYNX_REG8(0xFC05) = (uint8_t)((uint16_t)chain_hoff[set] >> 8);
    spr_draw(&heads[set]);
    LYNX_REG8(0xFC04) = 0;
}

/* this frame's chain for the back buffer */
static void build_chain(void)
{
    Player *p = &game.player[(uint8_t)game.active_player];
    SCB_RENONE *start = body[cur];
    int16_t vx = view_x;

    /* tiles: the runs in view */
    lk_set = cur;
    bt_col = (uint8_t)(vx >> 4);
    bt_x = -(int8_t)((vx & 15) >> 1);
    tiles_span();                           /* (and n_tiles) */
    chain_hoff[cur] = bt_hoff = vx >> 1;

    /* items and enemies (the original's level-complete screen has no enemies) */
    bt_scb = start;
    bo_noenemies = game.state == STATE_LEVELWIN;
    build_items();
    bp = bt_scb;
    /* the hero: not while dying; flashing while invulnerable */
    if (game.state != STATE_DYING && !(p->invuln & 4)) {
        static const uint8_t leg[4] = { 0, 1, 0, 2 };
        uint8_t f = p->state == 1 ? leg[p->walk_frame & 3] : 0;
        SCB_RENONE *h = bp;
        int16_t sx = (p->x - vx) >> 1;
        if (p->dir < 0) sx += HERO_W - 1;   /* HFLIP mirrors around the position */
        emit(art_hero[(uint8_t)p->character][f], sx, sy_of(p->y));
        if (h != bp && p->dir < 0) h->sprctl0 |= HFLIP;
    }
    bt_scb = bp;
    build_parts();
    bp = bt_scb;
    if (bp != start) bp[-1].next = 0;
    body_end[cur] = bp;
    n_sprites = n_tiles + (uint8_t)(bp - start);
    link_chain(cur);
}

/* draw this buffer's chain as it was (two frames ago) in the sky's colour */
static void erase_chain(void)
{
    SCB_REHV_PAL *h = &heads[cur];
    link_chain(cur);
    memcpy(h->penpal, pens_erase, 8);
    draw_chain(cur);
    memcpy(h->penpal, spr_pens_identity, 8);
}

/* ------------------------------------------------------------ HUD */

/* A separate chain, rebuilt when what it shows changes, drawn every frame
   (the playfield's sprites can reach into it). The original's two lines
   (name, hearts / SCORE / LIVES / LV) as one: name, hearts, score, lives,
   level. */
extern const unsigned char font3x5_data[64][16];
#define HUD_N 40
static SCB_REHV_PAL hud[HUD_N];
static uint8_t hud_n;
static int32_t hud_score = -1;
static int8_t hud_health = -1, hud_lives = -1, hud_level = -1, hud_char = -1;

static SCB_REHV_PAL *hud_spr(const unsigned char *data, int16_t x, int16_t y, uint8_t c)
{
    SCB_REHV_PAL *s = &hud[hud_n];
    spr_init(s, data, 1);
    spr_color(s, c);
    s->hpos = x;
    s->vpos = y;
    if (hud_n) spr_link(s - 1, s);
    ++hud_n;
    return s;
}

static int16_t hud_text(int16_t x, uint8_t c, const char *t)
{
    for (; *t; ++t, x += 4)
        if (*t != ' ') hud_spr(font3x5_data[*t - 32], x, HUD_Y + 1, c);
    return x;
}

static void hud_build(const Player *p)
{
    char buf[12];
    uint8_t i;
    int16_t x;
    hud_n = 0;
    spr_box(&hud[0], 0, HUD_Y, 160, 6, COL_BLACK);
    hud_n = 1;
    spr_box(&hud[1], 0, HUD_Y, 160, 1, COL_GREY);           /* the divider */
    spr_link(&hud[0], &hud[1]);
    hud_n = 2;
    x = hud_text(1, COL_WHITE, p->character == CHAR_RJ ? "RJ" : "DALE");
    x = 19;
    for (i = 0; i < (uint8_t)p->max_health; ++i, x += 6)
        hud_spr(art_heart, x, HUD_Y + 1, i < (uint8_t)p->health ? COL_RED : COL_DKBROWN);
    x = hud_text(51, COL_YELLOW, "SCORE");
    ultoa(p->score < 0 ? 0 : (unsigned long)p->score, buf, 10);
    hud_text(x + 2, COL_WHITE, buf);
    x = hud_text(105, COL_YELLOW, "LIVES");
    itoa(p->lives < 0 ? 0 : p->lives, buf, 10);
    hud_text(x + 2, COL_WHITE, buf);
    x = hud_text(137, COL_YELLOW, "LV");
    buf[0] = (char)('1' + (game.current_level < NUM_LEVELS ? game.current_level : NUM_LEVELS - 1));
    buf[1] = 0;
    hud_text(x + 2, COL_WHITE, buf);
    hud[hud_n - 1].next = 0;
}

/* rebuild when it changed; 1 if it did */
static uint8_t hud_update(const Player *p)
{
    if (p->score == hud_score && p->health == hud_health && p->lives == hud_lives &&
        game.current_level == hud_level && p->character == hud_char) return 0;
    hud_score = p->score;
    hud_health = p->health;
    hud_lives = p->lives;
    hud_level = game.current_level;
    hud_char = p->character;
    hud_build(p);
    return 1;
}

/* ------------------------------------------------------------ the view */

/* port: the view follows the hero smoothly, a Lynx pixel at a time, with
   the original's target (a third of the screen ahead of the hero's left
   edge) and easing ((target - cam + 3) / 4 a tick); the original then
   snapped cam_x to a whole tile (game.cam_x, still computed by jq.c) */
static void view_follow(void)
{
    const Player *p = &game.player[(uint8_t)game.active_player];
    int16_t target = p->x - VIEW_W / 3;
    int16_t maxv = (int16_t)tmap_w * TILE_SIZE - VIEW_W;
    if (target < 0) target = 0;
    if (target > maxv) target = maxv;
    if (view_x < target) view_x += (target - view_x + 3) >> 2;
    else if (view_x > target) view_x -= (view_x - target + 3) >> 2;
    view_x &= ~1;
}

/* ------------------------------------------------------------ high score */

int32_t hiscore = 1000;                     /* port: added (kept until power-off) */
static uint8_t new_hi;

/* ------------------------------------------------------------ the test hook */

#ifndef LYNX_NO_LOG
/* jq_autoplay = 1 (written by a test): the hero follows the routes in
   demo.h (found by the Spectrum port's solver over this same logic) instead
   of the pad. Harness builds only; nothing in a release build. */
#include "demo.h"
uint8_t jq_autoplay;
static uint16_t demo_at;
static uint8_t demo_left;

static uint8_t demo_input(void)
{
    if (game.state != STATE_PLAYING) return 0;
    if (game.level_ticks == 0) {
        uint8_t l = 0;
        demo_at = 0;
        while (l < (uint8_t)game.current_level) if (demo_route[demo_at++][0] == 0xFF) l++;
        demo_left = demo_route[demo_at][1];
    }
    while (demo_left == 0 && demo_route[demo_at][0] != 0xFF) demo_left = demo_route[++demo_at][1];
    if (demo_route[demo_at][0] == 0xFF) return 0;
    demo_left--;
    return demo_route[demo_at][0];
}
#endif

/* ------------------------------------------------------------ title */

uint8_t sel_char = CHAR_RJ, sel_players = 1;      /* (the test reads them) */
static uint8_t blink;
static SCB_REHV_PAL t_hero[2], t_bug[2];

static void title_init(void)
{
    uint8_t c;
    for (c = 0; c < 2; ++c) {
        spr_init(&t_hero[c], art_hero[c][0], 4);
        spr_scale(&t_hero[c], SPR_SCALE_2, SPR_SCALE_2);
        t_hero[c].hpos = c ? 96 : 50;
        t_hero[c].vpos = c ? 46 : 42;           /* feet level: RJ 12 lines, Dale 10 */
    }
    spr_init(&t_bug[0], art_enemy[0][0], 4);    /* the original's decorative bugs */
    t_bug[0].hpos = 10; t_bug[0].vpos = 85;
    spr_init(&t_bug[1], art_enemy[3][0], 4);
    t_bug[1].hpos = 140; t_bug[1].vpos = 85;
    spr_link(&t_bug[0], &t_bug[1]);
}

static void draw_title(void)
{
    uint8_t c;
    gfx_clear(COL_SKY);
    gfx_rect(0, 92, 160, 3, COL_GREEN);
    gfx_rect(0, 95, 160, 7, COL_BROWN);
    gfx_text_centre(3, COL_BLACK, "RJ AND DALE'S");     /* the original's shadow */
    gfx_text(29, 2, COL_WHITE, "RJ AND DALE'S");
    gfx_text(41, 14, COL_BLACK, "JUMP QUEST");
    gfx_text(40, 13, COL_YELLOW, "JUMP QUEST");
    text_small_centre(25, COL_WHITE, sel_players == 1 ? "1 PLAYER" : "2 PLAYERS");
    text_small_centre(32, COL_GREY, "UP/DOWN:PLAYERS");
    for (c = 0; c < 2; ++c) {
        uint8_t x = c ? 92 : 46;
        if (sel_char == c) gfx_frame(x - 1, 40, 24, 30, COL_YELLOW);
        spr_draw(&t_hero[c]);
    }
    text_small(53, 72, sel_char == CHAR_RJ ? COL_YELLOW : COL_WHITE, "RJ");
    text_small(96, 72, sel_char == CHAR_DALE ? COL_YELLOW : COL_WHITE, "DALE");
    text_small_centre(79, COL_GREY, "LEFT/RIGHT:SELECT");
    if ((blink >> 4) & 1) text_small_centre(86, COL_RED, "PRESS A TO START");
    spr_draw(&t_bug[0]);
    {
        char buf[16] = "HI ";
        ultoa((unsigned long)hiscore, buf + 3, 10);
        text_small_centre(96, COL_WHITE, buf);
    }
}

/* ------------------------------------------------------------ game over */

static char final_str[12];

static void draw_gameover(void)
{
    gfx_clear(COL_BLACK);
    if (game.current_level >= NUM_LEVELS) {
        gfx_text_centre(20, COL_YELLOW, "CONGRATULATIONS!");
        gfx_text_centre(34, COL_WHITE, "YOU SAVED THE DAY!");
    } else {
        gfx_text_centre(28, COL_RED, "GAME OVER");
    }
    gfx_text(8, 56, COL_WHITE, "FINAL SCORE:");
    gfx_text(104, 56, COL_WHITE, final_str);
    if (new_hi) gfx_text_centre(70, COL_YELLOW, "NEW HIGH SCORE");
}

/* ------------------------------------------------------------ main */

static uint8_t paused;
static uint16_t fps_vbl, fps_loops, late, last_vbl;
static uint8_t busy_max;

static void log_score(const Player *p)
{
    char buf[12];
    ultoa((unsigned long)p->score, buf, 10);
    LOGS(" score=");
    LOGS(buf);
}

static void start_game(void)
{
    jq_new_game(sel_char, sel_players);
    sound_music_start();
    view_x = 0;
    full_clear = 2;
    hud_score = -1;
    fps_vbl = frame_count;
    fps_loops = late = busy_max = 0;
    new_hi = 0;
    rows_index();
    LOG("jq: start char=%u players=%u\n", (unsigned)sel_char, (unsigned)sel_players);
    LOG("jq: level=1 player=1 lives=3 health=3 score=0\n");
}

static void to_title(void)
{
    game.state = STATE_TITLE;
    sound_all_off();
    paused = 0;
    blink = 0;
    {
        char buf[12];
        ultoa((unsigned long)hiscore, buf, 10);
        LOGS("jq: title hi=");
        LOGS(buf);
        LOGNL();
    }
}

static void game_over(void)
{
    int32_t s = game.player[0].score;
    if (game.num_players == 2) s += game.player[1].score;
    ultoa((unsigned long)s, final_str, 10);
    new_hi = s > hiscore;
    if (new_hi) hiscore = s;
    sound_music_stop();
    LOGS("jq: gameover");
    LOGS(" score=");
    LOGS(final_str);
    logu(" won=", game.current_level >= NUM_LEVELS);
    logu(" new_hi=", new_hi);
    LOGNL();
}

/* one tick of the game and what follows from its events */
static void tick(uint8_t inp)
{
    Player *p;
    uint16_t ev;
#ifndef LYNX_NO_LOG
    if (jq_autoplay && game.state == STATE_PLAYING) inp = demo_input();
#endif
    jq_tick(inp);
    ev = game.events;
    p = &game.player[(uint8_t)game.active_player];
    if (game.state == STATE_PLAYING || game.state == STATE_DYING || game.state == STATE_LEVELWIN)
        sound_music_tick();
    if (!ev) return;
    if (ev & EV_JUMP) { sound_jump(); LOGS("jq: jump\n"); }
    if (ev & EV_STOMP) { sound_stomp(); LOGS("jq: stomp"); log_score(p); LOGNL(); }
    if (ev & EV_COLLECT) { sound_collect(); LOGS("jq: collect"); log_score(p); LOGNL(); }
    if (ev & EV_POWERUP) {
        sound_powerup();
        logu("jq: powerup health=", p->health);
        logu(" lives=", p->lives);
        logu(" invuln=", p->invuln);
        logu(" boost=", p->speed_boost);
        LOGNL();
    }
    if (ev & EV_HURT) { sound_hurt(); logu("jq: hurt health=", p->health); LOGNL(); }
    if (ev & EV_DIE) {
        sound_die();
        logu("jq: died lives=", p->lives - 1);
        logu(" level=", game.current_level + 1);
        logu(" x=", p->x);
        LOGNL();
    }
    if (ev & EV_LEVELWIN) {
        sound_levelwin();
        logu("jq: cleared level=", game.current_level + 1);
        log_score(p);
        logu(" ticks=", game.level_ticks);
        LOGNL();
        full_clear = 2;
    }
    if (ev & EV_TILE) tile_changed(game.tile_tx, game.tile_ty);
    if (ev & EV_LEVEL) {
        view_x = 0;
        rows_index();
        full_clear = 2;
        logu("jq: level=", game.current_level + 1);
        logu(" player=", game.active_player + 1);
        logu(" lives=", p->lives);
        logu(" health=", p->health);
        log_score(p);
        LOGNL();
    }
    if (ev & EV_GAMEOVER) game_over();
}

int main(void)
{
    uint16_t now;
    uint8_t inp, busy;
    Player *p;

    gfx_init();
    gfx_set_rate(50);                       /* the original's PAL rate: 50 ticks a second */
    gfx_palette(art_palette);
    LYNX_GREEN(COL_SKY) = LYNX_GREEN(0);    /* port: colour 1 (the original's darker sky, */
    LYNX_BLUERED(COL_SKY) = LYNX_BLUERED(0);/* only behind the title's portraits) is the sky */
    snd_init();
    chains_init();
    title_init();
    LOG("jq: ready hz=50\n");
    to_title();
    last_vbl = frame_count;

    for (;;) {
        input_update();

        if (game.state == STATE_TITLE) {
            if (input_new & (IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN)) {
                if (input_new & IN_LEFT) sel_char = CHAR_RJ;
                if (input_new & IN_RIGHT) sel_char = CHAR_DALE;
                if (input_new & (IN_UP | IN_DOWN)) sel_players = 3 - sel_players;
                logu("jq: select char=", sel_char);
                logu(" players=", sel_players);
                LOGNL();
            }
            if (input_new & (IN_A | IN_B)) {
                start_game();
                input_held |= IN_A | IN_B;
            } else {
                draw_title();
                ++blink;
                gfx_present();
                last_vbl = frame_count;
                continue;
            }
        }

        if (input_new & IN_PAUSE) {
            paused ^= 1;
            if (paused) { snd_all_off(); LYNX_AUD(0, LYNX_AUD_VOL) = 0; LYNX_AUD(1, LYNX_AUD_VOL) = 0; }
            full_clear = 2;
            logu("jq: paused=", paused);
            LOGNL();
        }
        if (paused && (input_new & IN_OPT1)) {   /* the Lynx way: PAUSE, OPTION 1 = give up (the original's ESC) */
            to_title();
            continue;
        }

        /* --- the game: one tick a frame --- */
        if (!paused) {
            inp = 0;
            if (input_held & IN_LEFT) inp |= INP_LEFT;
            if (input_held & IN_RIGHT) inp |= INP_RIGHT;
            if (input_held & IN_UP) inp |= INP_UP;
            if (input_held & IN_DOWN) inp |= INP_DOWN;
            if (input_held & (IN_A | IN_B)) inp |= INP_JUMP;
            tick(inp);
            if (game.state == STATE_TITLE) { to_title(); continue; }
            if (game.state != STATE_GAMEOVER) view_follow();
        }

        /* --- draw --- */
        p = &game.player[(uint8_t)game.active_player];
        if (game.state == STATE_GAMEOVER) {
            draw_gameover();
            hud_update(&game.player[0]);
            spr_draw(&hud[0]);
            full_clear = 2;
        } else {
            if (full_clear || game.state == STATE_LEVELWIN) {
                gfx_clear(COL_SKY);
                if (full_clear) --full_clear;
            } else {
                erase_chain();
            }
            build_chain();
            draw_chain(cur);
            hud_update(p);
            spr_draw(&hud[0]);
            if (game.state == STATE_LEVELWIN) {
                gfx_text_centre(30, COL_WHITE, "LEVEL COMPLETE!");
                gfx_text_centre(44, COL_YELLOW, "BONUS: +500");
            }
            if (paused) { gfx_text_centre(40, COL_YELLOW, "PAUSED"); full_clear = 2; }
        }

        busy = (uint8_t)(MIKEY.timer2.reload - MIKEY.timer2.count);
        if (busy > busy_max) busy_max = busy;
        gfx_present();
        cur ^= 1;
        if (!paused) sound_tick();

        /* --- frame rate: loops per vertical blank, logged every 2 s --- */
        now = frame_count;
        if ((uint16_t)(now - last_vbl) > 1) late += (uint16_t)(now - last_vbl) - 1;
        last_vbl = now;
        ++fps_loops;
        if ((uint16_t)(now - fps_vbl) >= 100) {
            if (game.state == STATE_PLAYING && !paused) {
                logu("jq: fps=", (uint16_t)(fps_loops * 50u / (uint16_t)(now - fps_vbl)));
                logu(" late=", late);
                logu(" busy=", busy_max);
                logu(" tiles=", n_tiles);
                logu(" sprites=", n_sprites);
                logu(" level=", game.current_level + 1);
                LOGNL();
            }
            fps_vbl = now;
            fps_loops = late = 0;
            busy_max = 0;
        }
    }
}
