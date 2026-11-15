/*
 * Boing Twist (Atari Lynx). Port of amiga_mcp/examples/boing_twist.
 *
 * A picture is wrapped round the Boing ball, the ball is cut into tiles
 * along lines of latitude and longitude, and the tiles are jumbled. Twist
 * bands round the ball and flip halves of it over until the picture is
 * whole again. bt_core.c is the original's puzzle (rules, scrambles, undo,
 * the solved test), unchanged but for cc65 (marked port:).
 *
 * This file is the game around it (the original's main_os4.c, by way of the
 * Atari 8-bit port's game.c): the title with the Boing ball bouncing round
 * its room, the select screen (level, picture, room), play, the solved
 * screen. Moves go through a queue as in the original: a move animates (a
 * twist 240 ms, a flip 420 ms; faster while scrambling or giving up) and is
 * applied to the puzzle when it ends. Everything runs on 50 Hz ticks
 * (fixed timestep): a slow frame shows fewer steps of a move, not a slower
 * move.
 */
#include <string.h>
#include <tgi.h>
#include "lynxgame.h"
#include "bt_core.h"
#include "gfx_data.h"
#include "ball.h"
#include "room.h"
#include "sound.h"

#pragma static-locals (on)

enum { SC_TITLE, SC_SELECT, SC_PLAY, SC_SOLVED };
enum { K_DO, K_UNDO, K_SOLVE, K_SCRAMBLE };

/* the puzzle and the move queue live in the second screen buffer while
   playing (ball.h); the solved screen keeps what it shows */
#define P       (*(BtPuzzle *)HI_PUZZLE)
#define q_kind  HI_QKIND
#define q_move  HI_QMOVE
static int16_t solved_moves;
uint8_t screen, paused, peeking, gave_up, busy, sel_band, sel_half, room;
uint8_t sel_level, sel_pic, sel_row, play_level, play_pic;
uint16_t secs, peeks;
int16_t anim_move = -1;
uint8_t bt_idle;                           /* no move animating or queued, the ball not turning (the test reads it) */
uint16_t anim_frames, anim_ticks;          /* frames drawn / ticks elapsed while moves animate */
static uint8_t qh, qt;
static uint8_t anim_kind, anim_len, anim_t, auto_solve, timer_running, sec_ticks;
static uint16_t off16, target16;           /* the ball's turn: texels x 256 */
static uint16_t scene_t, seed_ticks;
static uint16_t best_mv[NPICS][4], best_s[NPICS][4];
static uint8_t best_new;
static uint8_t title_page;

/* drawing: what each of the two screen buffers still needs (bit 0 / bit 1) */
static uint8_t cur_buf, need_room, need_ball, need_hud;
static uint8_t single;                     /* play: one screen buffer, the other holds the flip's tables */
static uint8_t drv_page = 1;               /* the TGI driver's draw page (gfx_init leaves it 1) */
static int16_t mk_top = 1, mk_bot = 0;     /* the markers shown (top > bot: none) */
static uint8_t render;                     /* the ball's sprite must be drawn again */
static uint8_t shown_turn = 0xFF;
int16_t ball_cx = SCR_CX, ball_cy = SCR_CY;      /* the ball's middle on screen (the test reads it) */
static uint16_t ball_scale = 0x100;
static uint16_t bounce_a, bounce_x, spin16; /* the bounce's angles (8.8 of 256 a turn), the title's spin */
static uint8_t title_zoom;

static const char *const level_names[] = { "EASY", "MEDIUM", "HARD", "EXPERT" };

/* log lines for the test, without printf (lynx_log's vsnprintf costs 2.5 KB
   and ~25k cycles a line): "bt: <s>" then " k=v" pairs, then the newline */
#define LOG(s)       lynx_log_s("bt: " s)
#define KV(k, v)     (lynx_log_s(" " k "="), lynx_log_u((unsigned)(v)))
#define END()        lynx_log_c('\n')

#define ALL 3
#define dirty_all() (need_room = need_ball = need_hud = ALL)

/* ------------------------------------------------------------ small helpers */
/* sin of a (256 a turn) x 256 */
static const uint8_t sin_q[65] = {
    0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92, 98, 104, 109, 115, 121, 126,
    132, 137, 142, 147, 152, 157, 162, 167, 172, 177, 181, 185, 190, 194, 198, 202, 206, 209,
    213, 216, 220, 223, 226, 229, 231, 234, 237, 239, 241, 243, 245, 247, 248, 250, 251, 252,
    253, 254, 255, 255, 256 - 1, 255, 255,
};
static int16_t isin(uint8_t a)
{
    uint8_t q = a & 63;
    int16_t v;
    switch (a >> 6) {
    case 0: v = sin_q[q]; break;
    case 1: v = sin_q[64 - q]; break;
    case 2: v = -(int16_t)sin_q[q]; break;
    default: v = -(int16_t)sin_q[64 - q]; break;
    }
    if (a == 64) v = 256;
    if (a == 192) v = -256;
    return v;
}

static char numbuf[8];
static char *num(uint16_t v, uint8_t w)
{
    uint8_t i = 7;
    numbuf[7] = 0;
    do { numbuf[--i] = (char)('0' + v % 10); v /= 10; } while (v && i);
    while (i && (uint8_t)(7 - i) < w) numbuf[--i] = '0';
    return numbuf + i;
}

static char mmssbuf[6];
static char *mmss(uint16_t s)
{
    uint8_t m = (uint8_t)(s / 60), ss = (uint8_t)(s % 60);
    if (m > 99) m = 99;
    mmssbuf[0] = (char)('0' + m / 10);
    mmssbuf[1] = (char)('0' + m % 10);
    mmssbuf[2] = ':';
    mmssbuf[3] = (char)('0' + ss / 10);
    mmssbuf[4] = (char)('0' + ss % 10);
    mmssbuf[5] = 0;
    return mmssbuf;
}

/* Play is single-buffered: page 1 ($C038) is shown and drawn into, and page
   0 ($E018-$FFF7) holds the flip's tables and work space (ball.h). The
   other screens swap the two (gfx_present). */
static void buffers(uint8_t one)
{
    if (one == single) return;
    if (one) {
        if (drv_page == 1) {               /* make page 1 the one shown */
            gfx_present();
            drv_page = 0;
        }
        tgi_setdrawpage(1);
        lynx_load(FLIP_FILE);              /* cart/flip.bin into page 0 */
    } else
        tgi_setdrawpage(0);                /* back in step with the driver (it draws page 0) */
    single = one;
    cur_buf = 0;
}

/* ------------------------------------------------------------ the queue */
static uint16_t ease(uint8_t t, uint8_t len)
{
    uint16_t x = (uint16_t)(((uint16_t)t << 8) / len);       /* 0..256 */
    return (uint16_t)((uint32_t)x * x * (768 - 2 * x) >> 16);
}

/* ticks a move takes (50 a second): the original's 240 / 420 ms, faster
   while scrambling or giving up */
static uint8_t anim_len_of(uint8_t kind, int16_t m)
{
    uint8_t flip = BT_IS_FLIP(m) ? 1 : 0;
    switch (kind) {
    case K_SCRAMBLE: return flip ? 8 : 5;
    case K_SOLVE:    return flip ? 13 : 8;
    default:         return flip ? 21 : 12;
    }
}

static uint8_t q_len(void) { return (uint8_t)(qt - qh) & 127; }

static void enqueue(uint8_t kind, int16_t m)
{
    if (q_len() >= 127) return;
    if (kind == K_DO && !bt_valid(&P, m)) return;
    q_kind[qt] = kind;
    q_move[qt] = m;
    qt = (qt + 1) & 127;
}

static void face_half(uint8_t k) { target16 = (uint16_t)ball_half_turn(k) << 8; }

static void set_screen(uint8_t s);

static void enter_solved(void)
{
    uint16_t *bm = &best_mv[play_pic][play_level], *bs = &best_s[play_pic][play_level];
    timer_running = 0;
    best_new = 0;
    if (!gave_up) {
        /* the original keeps your best moves and time per picture and level */
        if (!*bm || (uint16_t)P.moves < *bm) { *bm = (uint16_t)P.moves; best_new = 1; }
        if (!*bs || secs < *bs) { *bs = secs; best_new = 1; }
    }
    solved_moves = P.moves;
    sfx(SFX_SOLVED);
    LOG("solved"); KV("moves", P.moves); KV("secs", secs); KV("peeks", peeks); KV("gave_up", gave_up); KV("best", best_new); END();
    set_screen(SC_SOLVED);
}

static void anim_next(void)
{
    if (auto_solve && q_len() == 0 && anim_move < 0) {
        if (P.nhist > 0) enqueue(K_SOLVE, 0);
        else auto_solve = 0;
    }
    while (anim_move < 0 && q_len()) {
        uint8_t kind = q_kind[qh];
        int16_t m = q_move[qh];
        qh = (qh + 1) & 127;
        if (kind == K_UNDO) m = bt_last_move(&P) >= 0 ? bt_inverse(bt_last_move(&P)) : -1;
        else if (kind == K_SOLVE) m = P.nhist > 0 ? bt_inverse(P.hist[P.nhist - 1]) : -1;
        if (m < 0) continue;
        anim_move = m;
        anim_kind = kind;
        anim_t = 0;
        anim_len = anim_len_of(kind, m);
        if (kind == K_DO && !timer_running) timer_running = 1;
        if (kind != K_SCRAMBLE || BT_IS_FLIP(m)) sfx(BT_IS_FLIP(m) ? SFX_FLIP : SFX_CLICK);
        if (peeking) { peeking = 0; ball_texture(&P, 0); need_hud = ALL; }
        if (BT_IS_FLIP(m)) {
            /* port: the half turns to face you first (as the a8 and Spectrum) */
            off16 = target16 = (uint16_t)ball_half_turn((uint8_t)BT_HALF(m)) << 8;
            fl_turn = (uint8_t)(off16 >> 8);
            flip_begin();
        } else
            ball_twist_begin((uint8_t)BT_BAND(m));
        render = 1;
    }
}

static void anim_finish(void)
{
    int16_t m = anim_move;
    uint8_t kind = anim_kind, v0, v1;
    switch (kind) {
    case K_DO: bt_do(&P, m); break;
    case K_UNDO: bt_undo(&P); break;
    case K_SOLVE:
        P.nhist--;
        if (P.nscramble > P.nhist) P.nscramble = P.nhist;
        bt_apply(&P, m);
        break;
    case K_SCRAMBLE:
        bt_apply(&P, m);
        if (P.nhist < BT_HIST) P.hist[P.nhist++] = m;
        P.nscramble = P.nhist;
        break;
    }
    anim_move = -1;
    busy = q_len() && q_kind[qh] == K_SCRAMBLE;
    need_hud = ALL;
    render = 1;
    if (kind == K_DO || kind == K_UNDO) { LOG("move"); KV("m", m); KV("moves", P.moves); END(); }
    if (kind == K_SCRAMBLE && !busy) {
        uint32_t h = bt_hash(&P);
        LOG("scrambled"); KV("n", P.nscramble); lynx_log_s(" hash="); lynx_log_x((unsigned)(h >> 16)); lynx_log_x((unsigned)h); END();
    }
    if (screen == SC_PLAY && !q_len() && kind != K_SCRAMBLE && bt_solved(&P)) {
        auto_solve = 0;
        if (kind == K_SOLVE) gave_up = 1;
        ball_texture(&P, 0);
        enter_solved();
        return;
    }
    if (BT_IS_FLIP(m)) ball_texture_half(&P, (uint8_t)BT_HALF(m));
    else {
        ball_band_rows((uint8_t)BT_BAND(m), &v0, &v1);
        ball_texture_rows(&P, v0, v1, 0);
    }
    if (!q_len() && !auto_solve) { LOG("anim"); KV("frames", anim_frames); KV("ticks", anim_ticks); END(); }
}

/* ------------------------------------------------------------ screens */
static void start_game(void)
{
    uint8_t lv = sel_level, i;
    uint32_t seed = (uint32_t)seed_ticks * 2654435761u + 1;
    buffers(1);                            /* the puzzle's memory (ball.h) */
    play_level = lv;
    play_pic = sel_pic;
    bt_init(&P, bt_levels[lv].bands, bt_levels[lv].segs);
    ball_picture(play_pic, (uint8_t)P.bands, (uint8_t)P.segs);
    ball_signatures(&P);
    seed = bt_start_level(&P, lv, seed, -1);
    LOG("play"); KV("pic", play_pic); KV("level", lv); KV("bands", P.bands); KV("segs", P.segs); KV("scramble", P.nscramble);
    lynx_log_s(" seed="); lynx_log_x((unsigned)(seed >> 16)); lynx_log_x((unsigned)seed); END();
    qh = qt = 0;
    for (i = 0; i < (uint8_t)P.nscramble; i++) enqueue(K_SCRAMBLE, P.hist[i]);
    bt_reset(&P);
    ball_texture(&P, 0);
    anim_move = -1;
    auto_solve = 0;
    busy = 1;
    timer_running = 0;
    secs = sec_ticks = 0;
    peeks = 0;
    gave_up = 0;
    paused = peeking = 0;
    sel_band = (uint8_t)(P.bands / 2 - 1);
    sel_half = (uint8_t)((P.segs / 2 - P.segs / 4 + P.segs) % P.segs);   /* the half facing you (the original) */
    face_half(sel_half);
    off16 = target16;
    anim_frames = anim_ticks = 0;
    set_screen(SC_PLAY);
}

uint8_t pic_allowed(uint8_t i, uint8_t level)
{
    if (i == 0) return level == BT_EASY;
    return pics[i].level <= level;
}

static uint8_t next_allowed(uint8_t from, int8_t d, uint8_t level)
{
    uint8_t i = from, n;
    for (n = 0; n < NPICS; ++n) {
        i = (uint8_t)((i + NPICS + d) % NPICS);
        if (pic_allowed(i, level)) return i;
    }
    return from;
}

/* the select screen's preview: the picture on the level's grid, solved */
static void show_picture(uint8_t pic, uint8_t level)
{
    ball_picture(pic, (uint8_t)bt_levels[level].bands, (uint8_t)bt_levels[level].segs);
    ball_texture(0, 1);
    render = 1;
    LOG("preview"); KV("pic", pic); KV("level", level); END();
}

static void choose_picture(uint8_t i)
{
    sel_pic = i;
    if (pics[i].room) room = pics[i].room;
    show_picture(sel_pic, sel_level);
}

static void set_palette(void)
{
    room_palette(room);
    if (screen == SC_TITLE) title_ball_palette();
    else ball_palette(pics[screen == SC_SELECT ? sel_pic : play_pic].col);
}

static void change_room(int8_t d)
{
    room = (uint8_t)((room + d + NROOMS) % NROOMS);
    set_palette();
    dirty_all();
    LOG("room"); KV("n", room); END();
}

static void change_row(int8_t d)
{
    switch (sel_row) {
    case 0:
        sel_level = (uint8_t)((sel_level + d + BT_NLEVELS) % BT_NLEVELS);
        if (!pic_allowed(sel_pic, sel_level)) sel_pic = next_allowed(sel_pic, 1, sel_level);
        choose_picture(sel_pic);
        break;
    case 1: choose_picture(next_allowed(sel_pic, d, sel_level)); break;
    case 2: room = (uint8_t)((room + d + NROOMS) % NROOMS); break;
    }
    set_palette();
    dirty_all();
}

#ifndef LYNX_NO_LOG
static const char *const screen_names[] = { "TITLE", "SELECT", "PLAY", "SOLVED" };
#endif

static void set_screen(uint8_t s)
{
    buffers(s == SC_PLAY);
    if (s == SC_TITLE && screen != SC_TITLE) title_ball_init();   /* its sprites into the texture's place */
    mk_top = 1;
    mk_bot = 0;
    screen = s;
    scene_t = 0;
    set_palette();
    dirty_all();
    render = 1;
    shown_turn = 0xFF;
    LOG("screen="); lynx_log_s(screen_names[s]); END();
}

static void to_title(void)
{
    title_page = 0;
    title_zoom = 0;
    qh = qt = 0;
    anim_move = -1;
    set_screen(SC_TITLE);
}

static void to_select(void)
{
    if (!pic_allowed(sel_pic, sel_level)) sel_pic = next_allowed(sel_pic, 1, sel_level);
    if (pics[sel_pic].room) room = pics[sel_pic].room;
    show_picture(sel_pic, sel_level);
    set_screen(SC_SELECT);
}

/* ------------------------------------------------------------ input */
/* port: controls. The d-pad's up/down choose the band, left/right twist
   it (the original's arrow keys); B held + left/right choose the half (A /
   D: the ball turns to face it), B + up/down turn the ball to look (Q / E);
   A flips the half (Space). OPTION 1 undoes (U), OPTION 2 held peeks (H).
   PAUSE pauses (P), and paused: A a new scramble (N), B give up (R),
   OPTION 1 the title (Esc), OPTION 2 the room (T). */
static void play_input(uint16_t held, uint16_t edge)
{
    uint8_t S = (uint8_t)P.segs, B = (uint8_t)P.bands, old_band = sel_band, old_half = sel_half, pk;
    if (edge & IN_PAUSE) {
        paused = !paused;
        dirty_all();
        LOG("paused"); KV("on", paused); END();
        return;
    }
    if (paused) {
        if (edge & IN_A) { paused = 0; LOG("new scramble"); END(); start_game(); }
        else if ((edge & IN_B) && !busy) {
            paused = 0;
            qh = qt;
            auto_solve = 1;
            dirty_all();
            LOG("give up"); END();
        } else if (edge & IN_OPT1) { paused = 0; to_title(); }
        else if (edge & IN_OPT2) change_room(1);
        return;
    }
    pk = (held & IN_OPT2) != 0;
    if (pk && !peeking && anim_move < 0 && !busy) {
        peeking = 1;
        peeks++;
        need_hud = ALL;
        ball_texture(&P, 1);
        render = 1;
        LOG("peek"); KV("n", peeks); END();
    }
    if (!pk && peeking) {
        peeking = 0;
        need_hud = ALL;
        ball_texture(&P, 0);
        render = 1;
    }
    if (held & IN_B) {
        if (edge & IN_LEFT) sel_half = (uint8_t)((sel_half + S - 1) % S);
        if (edge & IN_RIGHT) sel_half = (uint8_t)((sel_half + 1) % S);
        if (edge & IN_UP) target16 -= 4u << 8;
        if (edge & IN_DOWN) target16 += 4u << 8;
    } else {
        if ((edge & IN_UP) && sel_band > 0) sel_band--;
        if ((edge & IN_DOWN) && sel_band < B - 1) sel_band++;
        if ((edge & IN_LEFT) && !busy) enqueue(K_DO, BT_TWIST(sel_band, 1));    /* west */
        if ((edge & IN_RIGHT) && !busy) enqueue(K_DO, BT_TWIST(sel_band, 0));   /* east */
    }
    if ((edge & IN_A) && !busy) enqueue(K_DO, BT_FLIP(sel_half));
    if ((edge & IN_OPT1) && !busy) enqueue(K_UNDO, 0);
    if (sel_half != old_half) face_half(sel_half);
    if (sel_band != old_band || sel_half != old_half) {
        need_hud = ALL;
        LOG("sel"); KV("band", sel_band); KV("half", sel_half); END();
    }
}

extern volatile uint16_t in_latch;          /* latch.s */
static uint16_t in_prev;

static void input(void)
{
    uint16_t held, edge, now;
    __asm__("sei");
    now = input_read();
    held = now | in_latch;                 /* down now, or pressed since the last look */
    in_latch = 0;
    __asm__("cli");
    edge = held & ~in_prev;
    in_prev = now;
    switch (screen) {
    case SC_TITLE:
        if (edge & (IN_A | IN_PAUSE)) to_select();
        else if (edge & IN_OPT2) change_room(1);
        break;
    case SC_SELECT:
        /* each press counts, several in one frame too (latch.s) */
        if (edge & IN_B) { to_title(); break; }
        if (edge & IN_UP) { sel_row = (uint8_t)((sel_row + 2) % 3); need_hud = ALL; }
        if (edge & IN_DOWN) { sel_row = (uint8_t)((sel_row + 1) % 3); need_hud = ALL; }
        if (edge & IN_LEFT) change_row(-1);
        if (edge & IN_RIGHT) change_row(1);
        if (edge & (IN_A | IN_PAUSE)) start_game();
        break;
    case SC_PLAY:
        play_input(held, edge);
        break;
    case SC_SOLVED:
        if (scene_t < 50) break;
        if (edge & IN_B) to_title();
        else if (edge & (IN_A | IN_PAUSE)) to_select();
        break;
    }
}

/* ------------------------------------------------------------ ticks */
static void bounce_tick(uint16_t da, uint16_t dx)
{
    uint8_t before = (uint8_t)(bounce_a >> 8) & 127;
    bounce_a += da;
    bounce_x += dx;
    if (((uint8_t)(bounce_a >> 8) & 127) < before && scene_t > 10) sfx(SFX_THUD);   /* |sin| came down to 0 */
}

static void tick(void)
{
    int16_t d;
    seed_ticks++;
    scene_t++;
    switch (screen) {
    case SC_TITLE:
        /* the 1984 demo: bouncing across the room, spinning 70 degrees a second */
        bounce_tick(501, 115);
        spin16 += 111;                     /* 14 phases = 45 degrees */
        if (title_zoom < 32) title_zoom++;
        if (scene_t % 250 == 0) { title_page = (uint8_t)((title_page + 1) % 3); need_hud = ALL; }
        need_room = ALL;
        return;
    case SC_SELECT:
        off16 -= 23;                       /* -25 degrees a second (the original) */
        return;
    case SC_SOLVED:
        bounce_tick(543, 167);
        off16 += 82;                       /* 90 degrees a second */
        need_room = ALL;
        return;
    }
    if (paused) return;
    anim_next();
    if (anim_move >= 0) {
        render = 1;
        anim_ticks++;
        if (++anim_t >= anim_len) { anim_finish(); anim_next(); }
    }
    if (screen != SC_PLAY) return;
    if (timer_running && ++sec_ticks >= 50) {
        sec_ticks = 0;
        secs++;
        need_hud = ALL;
    }
    d = (int16_t)(target16 - off16);
    if (d) {
        int16_t st = d / 6;
        if (st == 0) st = d;
        off16 += st;
    }
}

/* ------------------------------------------------------------ drawing */
static void render_ball(void)
{
    uint8_t turn = (uint8_t)((off16 + 128) >> 8) & 63;
    if (screen == SC_PLAY && anim_move >= 0) {
        uint16_t e = ease(anim_t, anim_len);
        if (BT_IS_FLIP(anim_move)) {
            fl_turn = turn;
            fl_ang = (uint8_t)((e * 32u + 128) >> 8);
            flip_frame();
        } else {
            uint8_t w = (uint8_t)(TEX_W * 16 / P.segs), dd = (uint8_t)((e * w + 2048) >> 12);
            ball_twist_shift(BT_WEST(anim_move) ? -(int8_t)dd : (int8_t)dd);
        }
    } else if (!render && turn == shown_turn)
        return;
    view_play(turn);
    shown_turn = turn;
    render = 0;
    need_ball = ALL;
    if (screen == SC_PLAY && anim_move >= 0) anim_frames++;
}

/* the ball's place on this screen; the shadow's */
static void place_ball(void)
{
    int16_t up, sx;
    room_shadow.on = 1;
    switch (screen) {
    case SC_TITLE:
    case SC_SOLVED:
        up = isin((uint8_t)(bounce_a >> 8) & 127);              /* |sin| */
        sx = isin((uint8_t)(bounce_x >> 8));
        if (screen == SC_TITLE) {
            ball_cx = SCR_CX + (int16_t)((sx * 40) >> 8);
            ball_cy = SCR_CY + 13 - (int16_t)((up * 43) >> 8);
        } else {
            ball_cx = SCR_CX + (int16_t)((sx * 26) >> 8);
            ball_cy = SCR_CY + 13 - (int16_t)((up * 39) >> 8);
        }
        ball_scale = screen == SC_TITLE ? 0x100 : 214;        /* 4.3 away instead of 3.6 */
        break;
    case SC_SELECT:
        ball_cx = 112;
        ball_cy = SCR_CY + 12;
        ball_scale = 220;                                     /* port: 4.2 away (the original's 3.9), under the rows */
        break;
    default:
        ball_cx = SCR_CX;
        ball_cy = SCR_CY;
        ball_scale = 0x100;
        break;
    }
    if (rooms[room].wall) {
        /* render.c: (x + 0.85, y - 0.15) on the back wall, 6.5 away */
        int16_t dist16 = screen == SC_PLAY ? 58 : screen == SC_SELECT ? 67 : 70;   /* x16 */
        room_shadow.cx = SCR_CX + (ball_cx - SCR_CX) * dist16 / 104 + (WALL_SH_DX16 >> 4);
        room_shadow.cy = SCR_CY + (ball_cy - SCR_CY) * dist16 / 104 + (WALL_SH_DY16 >> 4);
        room_shadow.rx16 = room_shadow.ry16 = WALL_SH_R16;
    } else {
        room_shadow.cx = ball_cx;
        room_shadow.cy = FLOOR_SH_Y;
        room_shadow.rx16 = FLOOR_SH_RX16;
        room_shadow.ry16 = FLOOR_SH_RY16;
        if (screen == SC_PLAY || screen == SC_SELECT) room_shadow.on = 0;   /* below the screen */
    }
}

static uint8_t strips_done;                /* this frame's room draw put the HUD strips (under the ball) */

static void hud_top(void)
{
    if (!strips_done) hud_strip(0);
    switch (screen) {
    case SC_TITLE:
        break;
    case SC_SELECT:
        text_small(2, 1, 5, "CHOOSE A PUZZLE");
        break;
    case SC_PLAY:
        text_small(2, 1, 5, pics[play_pic].title);
        text_small(96, 1, 5, "MOVES");
        text_small(122, 1, 5, num((uint16_t)P.moves, 3));
        text_small(138, 1, 5, mmss(secs));
        break;
    case SC_SOLVED:
        text_small(2, 1, 5, gave_up ? "GIVEN UP" : best_new ? "SOLVED! A NEW BEST" : "SOLVED!");
        text_small(92, 1, 5, "MOVES");
        text_small(114, 1, 5, num((uint16_t)solved_moves, 0));
        text_small(138, 1, 5, mmss(secs));
        break;
    }
}

static void hud_bottom(void)
{
    if (!strips_done) hud_strip(1);
    switch (screen) {
    case SC_TITLE:
        text_small_centre(HUD_BOT + 1, 5, title_page == 0 ? "A: CHOOSE A PUZZLE" :
                          title_page == 1 ? "TWIST THE BANDS, FLIP THE HALVES" : "OPTION 2: THE ROOM");
        break;
    case SC_SELECT:
        text_small_centre(HUD_BOT + 1, 5, "UP/DOWN, LEFT/RIGHT, A: PLAY");
        break;
    case SC_PLAY:
        if (busy) text_small(2, HUD_BOT + 1, 5, "SCRAMBLING...");
        else if (paused) text_small(2, HUD_BOT + 1, 5, "PAUSED");
        else if (peeking) text_small(2, HUD_BOT + 1, 5, "PEEKING AT THE PICTURE");
        else {
            text_small(2, HUD_BOT + 1, 5, "BAND");
            text_small(22, HUD_BOT + 1, 5, num((uint16_t)sel_band + 1, 0));
            text_small(34, HUD_BOT + 1, 5, "HALF");
            text_small(54, HUD_BOT + 1, 5, num((uint16_t)sel_half + 1, 0));
            text_small(70, HUD_BOT + 1, 5, level_names[play_level]);
            if (peeks) { text_small(110, HUD_BOT + 1, 5, "PEEKS"); text_small(134, HUD_BOT + 1, 5, num(peeks, 0)); }
        }
        break;
    case SC_SOLVED:
        text_small_centre(HUD_BOT + 1, 5, "A: ANOTHER PUZZLE  B: TITLE");
        break;
    }
}

/* the select screen's rows, the pause menu: panels over the room */
static void panels(void)
{
    if (screen == SC_SELECT) {
        uint16_t bm = best_mv[sel_pic][sel_level];
        static const char *const lines[3] = { "LEVEL", "PICTURE", "ROOM" };
        uint8_t i;
        panel(0, HUD_TOP, 160, 26, 0);
        for (i = 0; i < 3; ++i) {
            uint8_t y = (uint8_t)(HUD_TOP + 1 + i * 6);
            if (sel_row == i) text_small(2, y, 5, ">");
            text_small(8, y, 5, lines[i]);
        }
        text_small(40, HUD_TOP + 1, 5, level_names[sel_level]);
        text_small(72, HUD_TOP + 1, 5, sel_level == 0 ? "4 X 8, 8 MOVES" : sel_level == 1 ? "6 X 12, 20 MOVES" :
                   sel_level == 2 ? "8 X 16, 40 MOVES" : "8 X 16, 80 MOVES");
        text_small(40, HUD_TOP + 7, 5, pics[sel_pic].title);
        text_small(40, HUD_TOP + 13, 5, rooms[room].name);
        if (sel_pic == 0) text_small(8, HUD_TOP + 19, 5, "MAKE THE CHECKERBOARD AGAIN");
        else if (bm) {
            text_small(8, HUD_TOP + 19, 5, "BEST");
            text_small(28, HUD_TOP + 19, 5, num(bm, 0));
            text_small(44, HUD_TOP + 19, 5, "MOVES");
            text_small(68, HUD_TOP + 19, 5, mmss(best_s[sel_pic][sel_level]));
        }
    }
    if (screen == SC_PLAY && paused) {
        panel(16, 22, 128, 50, 0);
        text_small_centre(25, 5, "PAUSED");
        text_small(22, 35, 5, "PAUSE      GO ON");
        text_small(22, 42, 5, "A          NEW SCRAMBLE");
        text_small(22, 49, 5, "B          GIVE UP");
        text_small(22, 56, 5, "OPTION 1   TITLE");
        text_small(22, 63, 5, "OPTION 2   ROOM");
    }
    if (screen == SC_SOLVED) {
        uint16_t bm = best_mv[play_pic][play_level];
        if (bm && !gave_up) {
            panel(0, HUD_TOP, 160, 7, 0);
            text_small(2, HUD_TOP + 1, 5, "BEST");
            text_small(22, HUD_TOP + 1, 5, num(bm, 0));
            text_small(38, HUD_TOP + 1, 5, "MOVES");
            text_small(62, HUD_TOP + 1, 5, mmss(best_s[play_pic][play_level]));
        }
    }
}

/* the band markers (play): put them right if the band, or whether they show, changed */
#define MK_XL (SCR_CX - BALL_R - 6)
#define MK_XR (SCR_CX + BALL_R + 4)
static void draw_markers(uint8_t force)
{
    uint8_t g = P.bands == 4 ? 0 : P.bands == 6 ? 1 : 2;
    int16_t top = 1, bot = 0;
    if (!busy && !peeking && anim_move < 0 && !paused) {
        top = SCR_CY + BALL_Y0 + band_top[g][sel_band];
        bot = SCR_CY + BALL_Y0 + band_bot[g][sel_band];
    }
    if (force || top != mk_top || bot != mk_bot) {
        markers_put(top, bot);
        mk_top = top;
        mk_bot = bot;
    }
}

static uint8_t draw_frame(void)
{
    uint8_t b = single ? ALL : (uint8_t)(1 << cur_buf), drew = 0, full = 0, hud = (uint8_t)(need_hud & b);
    strips_done = 0;
    if (need_room & b) {
        place_ball();
        room_draw(room);
        /* the HUD strips under the ball (it can bounce into the top one); the
           bottom one only when its text changes (the room stops above it) */
        hud_strip(0);
        if (hud) hud_strip(1);
        if (screen == SC_TITLE) {          /* the logo behind the ball */
            uint16_t sc = title_zoom >= 32 ? 0x200 : (uint16_t)title_zoom << 4;
            if (sc) logo_draw(HUD_TOP + 3, sc, 6 + 2 * 3, 6);
        }
        need_ball |= b;
        drew = full = strips_done = 1;
        if (single) markers_save(MK_XL, MK_XR);
    }
    if (need_ball & b) {
        if (screen == SC_TITLE) {
            uint8_t ph = (uint8_t)((spin16 >> 8) % TB_PHASES);
            title_ball_draw(ball_cx, ball_cy, ph);
        } else
            ball_draw(ball_cx, ball_cy, ball_scale);
        drew = 1;
    }
    if (hud || full) {
        hud_top();
        panels();
        drew = 1;
    }
    if (hud) hud_bottom();
    if (single && !paused) draw_markers(full);
    need_room &= (uint8_t)~b;
    need_ball &= (uint8_t)~b;
    need_hud &= (uint8_t)~b;
    return drew;
}

void main(void)
{
    uint16_t last;
    uint8_t n, i;
    gfx_init();
    gfx_set_rate(50);
    snd_init();
    ball_init();
    title_ball_init();
    LOG("boot"); END();
    room = 0;
    to_title();
    last = frame_count;
    for (;;) {
        n = (uint8_t)(frame_count - last);
        last += n;
        if (n > 8) n = 8;
        input();
        for (i = 0; i < n; ++i) { tick(); snd_tick(); }
        bt_idle = (uint8_t)(anim_move < 0 && qh == qt && !auto_solve && off16 == target16);
        if (screen != SC_TITLE) render_ball();
        else if (need_room) need_ball = ALL;
        if (single) {
            frame_wait();                  /* draw in place just after the vertical blank */
            draw_frame();
        } else if (draw_frame()) {
            gfx_present();
            cur_buf ^= 1;
            drv_page ^= 1;
        } else
            frame_wait();
    }
}
