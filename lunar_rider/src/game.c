/*
 * Lunar Rider (Lynx) - the game's rules: the Amiga original's game.c.
 *
 * Scrolling, physics, collision, terrain generation, enemies, checkpoints,
 * as the original, in its units (320 x 256 world, 1/100 pixel speeds and
 * velocities). Changes are marked `port:`; the ones for the 6502:
 *
 * - struct-of-arrays globals instead of GameState (game.h);
 * - the scroll as whole pixels + hundredths (scroll_px, scroll_ring,
 *   scroll_frac) instead of one 32-bit count of 1/100 pixels divided by 100
 *   wherever it is used: the same values;
 * - the random numbers: the SDK's 16-bit xorshift (the original's 32-bit
 *   LCG costs ~1,000 cycles a call), `% n` as a table / Suzy-free
 *   multiply-shift; the courses differ from the Amiga's, not their rules;
 * - the terrain generator's loop is in 6502 (gen.s), and a new chunk (64
 *   pixels) is generated 16 pixels a frame over four frames instead of in
 *   one (it is 420 pixels ahead, off the screen);
 * - the terrain's features are also kept as a list for drawing (game.h).
 */
#include <string.h>
#include "lynxgame.h"
#include "game.h"

#pragma static-locals (on)

uint16_t scroll_speed;
uint8_t  scroll_frac;
uint16_t scroll_px;
uint16_t scroll_ring;
uint8_t  terrain[TERRAIN_LEN];
uint8_t  terrain_h[TERRAIN_LEN];

int16_t  buggy_y, buggy_vy;
uint8_t  buggy_on_ground, buggy_alive, buggy_wheel;

int16_t  fb_x[MAX_FWD_BULLETS], fb_y[MAX_FWD_BULLETS];
uint8_t  fb_active[MAX_FWD_BULLETS];
int16_t  ub_x[MAX_UP_BULLETS], ub_y[MAX_UP_BULLETS];
uint8_t  ub_active[MAX_UP_BULLETS];

int16_t  en_x[MAX_ENEMIES], en_y[MAX_ENEMIES];
int8_t   en_dx[MAX_ENEMIES], en_dy[MAX_ENEMIES];
uint8_t  en_type[MAX_ENEMIES], en_active[MAX_ENEMIES], en_hp[MAX_ENEMIES];
uint8_t  en_bomb[MAX_ENEMIES], en_flash[MAX_ENEMIES];

int16_t  bm_x[MAX_BOMBS], bm_y[MAX_BOMBS];
uint8_t  bm_active[MAX_BOMBS];

int16_t  ex_x[MAX_EXPLOSIONS], ex_y[MAX_EXPLOSIONS];
uint8_t  ex_radius[MAX_EXPLOSIONS], ex_life[MAX_EXPLOSIONS], ex_active[MAX_EXPLOSIONS];

uint16_t score_h;
uint8_t  lives, state, state_timer;
uint16_t frame;
uint8_t  checkpoint_cur;
int16_t  checkpoint_dist;
uint8_t  difficulty;

uint8_t ev_shoot, ev_explode, ev_jump, ev_checkpoint, ev_death;
uint8_t death_cause, score_why;

uint8_t  feat_wx_lo[FEAT_N], feat_wx_hi[FEAT_N];
uint8_t  feat_ring_lo[FEAT_N], feat_ring_hi[FEAT_N];
uint8_t  feat_type[FEAT_N], feat_w[FEAT_N];
uint8_t  feat_head, feat_tail;

/* port: frame % 60, kept as a counter (a 16-bit modulo is slow here) */
static uint8_t spawn_ctr;

/* ---- random numbers ---- */

/* gen.s: rnd8_fast() is the SDK's xorshift16 in asm (the C one costs ~330
   cycles); rng() % 100 is pct[rnd8_fast()] (each of 0..99 two or three times in
   256) */
uint8_t rnd8_fast(void);
extern const uint8_t pct[256];

/* rng() % n for small n */
static uint8_t rng_mod(uint8_t n)
{
    return (uint8_t)(((uint16_t)rnd8_fast() * n) >> 8);
}

/* ---- Terrain generation ---- */

/* The chunk being generated: ring positions gen_x..gen_end, gen_wx the
   world x (low 16 bits) of gen_x. gen.s runs the loop (gen_steps). */
uint16_t gen_x, gen_end, gen_wx;
uint8_t  gen_busy, gen_open;            /* gen_open: the start area check applies */
static int16_t  gen_ahead;              /* terrain_gen_x - scroll_x / 100 */
static uint16_t gen_next;               /* terrain_gen_x % TERRAIN_LEN */
void __fastcall__ gen_steps(uint8_t n);

/* start generate_terrain_chunk(start, start + len) at world x wx */
static void gen_start(uint16_t start, uint16_t len, uint16_t wx)
{
    static uint16_t lim;
    gen_x = start;
    gen_end = start + len;
    gen_wx = wx;
    gen_busy = 1;
    /* port: forget the drawn features whose pixels this chunk replaces
       (world x below wx - 640 + 64) */
    lim = wx - (TERRAIN_LEN - 64);
    while (feat_head != feat_tail &&
           (int16_t)((feat_wx_lo[feat_head] | (feat_wx_hi[feat_head] << 8)) - lim) < 0) ++feat_head;
}

/* ---- Enemy spawning ---- */

static void try_spawn_enemy(void)
{
    static uint8_t i, type_r;
    uint8_t spawn_chance = 2 + difficulty * 2;
    uint8_t r = pct[rnd8_fast()];

    if (r >= spawn_chance) return;

    /* Find free slot */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!en_active[i]) {
            type_r = pct[rnd8_fast()];

            en_active[i] = 1;
            en_flash[i] = 0;
            en_bomb[i] = 60 + rng_mod(60);

            if (type_r < 40) {
                /* Small UFO */
                en_type[i] = ENEMY_UFO_SM;
                en_x[i] = SCREEN_W + 10;
                en_y[i] = 30 + rng_mod(40);
                en_dx[i] = -2 - (int8_t)rng_mod(2);
                en_dy[i] = 0;
                en_hp[i] = 1;
            } else if (type_r < 65) {
                /* Large UFO */
                en_type[i] = ENEMY_UFO_LG;
                en_x[i] = SCREEN_W + 10;
                en_y[i] = 20 + rng_mod(30);
                en_dx[i] = -1;
                en_dy[i] = 0;
                en_hp[i] = 2;
            } else {
                /* Meteor */
                en_type[i] = ENEMY_METEOR;
                en_x[i] = SCREEN_W + rng_mod(60);
                en_y[i] = -10;
                en_dx[i] = -(1 + (int8_t)rng_mod(2));
                en_dy[i] = 2 + (int8_t)rng_mod(2);
                en_hp[i] = 1;
            }
            break;
        }
    }
}

/* ---- Initialization ---- */

void game_init(void)
{
    lives = 3;
    score_h = 0;
    state = STATE_TITLE;
    frame = 0;
    spawn_ctr = 0;
    difficulty = 0;

    scroll_px = 0;
    scroll_ring = 0;
    scroll_frac = 0;
    scroll_speed = SPEED_NORMAL;

    /* Init buggy */
    buggy_y = BUGGY_GROUND_Y;
    buggy_vy = 0;
    buggy_on_ground = 1;
    buggy_alive = 1;
    buggy_wheel = 0;

    memset(fb_active, 0, sizeof fb_active);
    memset(ub_active, 0, sizeof ub_active);
    memset(en_active, 0, sizeof en_active);
    memset(bm_active, 0, sizeof bm_active);
    memset(ex_active, 0, sizeof ex_active);

    checkpoint_cur = 0;
    checkpoint_dist = CHECKPOINT_DIST;

    /* Generate initial terrain: one chunk of the whole ring */
    feat_head = feat_tail = 0;
    gen_open = 1;
    gen_start(0, TERRAIN_LEN, 0);
    while (gen_busy) gen_steps(255);
    gen_open = 0;
    gen_ahead = TERRAIN_LEN;
    gen_next = 0;

    ev_shoot = ev_explode = ev_jump = ev_checkpoint = ev_death = 0;
}

/* ---- Spawn explosion ---- */

static void spawn_explosion(int16_t x, int16_t y)
{
    static uint8_t i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!ex_active[i]) {
            ex_x[i] = x;
            ex_y[i] = y;
            ex_radius[i] = 2;
            ex_life[i] = 12;
            ex_active[i] = 1;
            break;
        }
    }
}

/* the buggy dies (the original repeats these lines at each place) */
static void buggy_dies(uint8_t cause)
{
    buggy_alive = 0;
    state = STATE_DYING;
    state_timer = 40;
    ev_death = 1;
    death_cause = cause;
}

static void add_score(uint8_t hundreds, uint8_t why)
{
    score_h += hundreds;
    score_why = why;
}

/* ---- Update functions ---- */

static uint16_t ring_add(uint16_t a)
{
    a += scroll_ring;
    if (a >= TERRAIN_LEN) a -= TERRAIN_LEN;
    return a;
}

static void update_buggy(uint8_t inp)
{
    static uint16_t idx;
    static int16_t ground_h;
    static uint8_t i, fired, t;

    if (!buggy_alive) return;

    /* Speed control */
    if (inp & INP_RIGHT) scroll_speed = SPEED_FAST;
    else if (inp & INP_LEFT) scroll_speed = SPEED_SLOW;
    else scroll_speed = SPEED_NORMAL;

    /* Jump */
    if ((inp & INP_JUMP) && buggy_on_ground) {
        buggy_vy = JUMP_VEL;
        buggy_on_ground = 0;
        ev_jump = 1;
    }

    /* Gravity */
    if (!buggy_on_ground) buggy_vy += GRAVITY;

    /* Apply vertical velocity (fixed point /100) */
    buggy_y += buggy_vy / 100;

    /* Get ground height at buggy position */
    idx = ring_add(BUGGY_X + BUGGY_W / 2);
    ground_h = terrain_h[idx] - BUGGY_H;

    /* Check ground collision */
    if (buggy_y >= ground_h) {
        t = terrain[idx];
        if (t == TERRAIN_CRATER_SM || t == TERRAIN_CRATER_LG) {
            /* Falling into crater = death (only if not jumping over) */
            if (buggy_on_ground || buggy_vy > 0) {
                if (buggy_y >= GROUND_Y - BUGGY_H + 5) {
                    buggy_dies(CAUSE_CRATER);
                    spawn_explosion(BUGGY_X + BUGGY_W / 2, buggy_y + BUGGY_H / 2);
                    return;
                }
            }
        } else {
            buggy_y = ground_h;
            buggy_vy = 0;
            buggy_on_ground = 1;
        }
    } else {
        buggy_on_ground = 0;
    }

    /* Check rock collision (under the buggy's front) */
    idx = ring_add(BUGGY_X + BUGGY_W);
    if (terrain[idx] == TERRAIN_ROCK && buggy_on_ground) {
        buggy_dies(CAUSE_ROCK);
        spawn_explosion(BUGGY_X + BUGGY_W / 2, buggy_y + BUGGY_H / 2);
        return;
    }

    /* Check mine collision (under its centre) */
    idx = ring_add(BUGGY_X + BUGGY_W / 2);
    if (terrain[idx] == TERRAIN_MINE && buggy_on_ground) {
        buggy_dies(CAUSE_MINE);
        ev_explode = 1;
        spawn_explosion(BUGGY_X + BUGGY_W / 2, GROUND_Y);
        terrain[idx] = TERRAIN_FLAT;        /* Remove the mine */
        return;
    }

    /* Wheel animation: (scroll_x / 200) & 1 */
    buggy_wheel = (uint8_t)(scroll_px >> 1) & 1;

    /* Fire bullets: one forward, one upward, if a slot is free */
    if (inp & INP_FIRE) {
        fired = 0;
        for (i = 0; i < MAX_FWD_BULLETS; i++) {
            if (!fb_active[i]) {
                fb_x[i] = BUGGY_X + BUGGY_W;
                fb_y[i] = buggy_y + 4;
                fb_active[i] = 1;
                fired = 1;
                break;
            }
        }
        for (i = 0; i < MAX_UP_BULLETS; i++) {
            if (!ub_active[i]) {
                ub_x[i] = BUGGY_X + BUGGY_W / 2;
                ub_y[i] = buggy_y;
                ub_active[i] = 1;
                fired = 1;
                break;
            }
        }
        if (fired) ev_shoot = 1;
    }
}

static void update_bullets(void)
{
    static uint8_t i;
    static uint16_t tidx;

    for (i = 0; i < MAX_FWD_BULLETS; i++) {
        if (!fb_active[i]) continue;
        fb_x[i] += 5;
        if (fb_x[i] > SCREEN_W + 10) {
            fb_active[i] = 0;
            continue;
        }
        /* Check if bullet hits a rock.
           port: original bug - the original took the bullet's world x as
           a 16-bit WORD, so after 32768 pixels (~65 zones) it looked at the
           wrong terrain; here it is the ring index. */
        tidx = ring_add(fb_x[i]);
        if (terrain[tidx] == TERRAIN_ROCK && fb_y[i] >= GROUND_Y - 12) {
            fb_active[i] = 0;
            terrain[tidx] = TERRAIN_FLAT;
            add_score(1, TERRAIN_ROCK);
            ev_explode = 1;
            spawn_explosion(fb_x[i], GROUND_Y - 6);
        }
    }

    for (i = 0; i < MAX_UP_BULLETS; i++) {
        if (!ub_active[i]) continue;
        ub_y[i] -= 5;
        if (ub_y[i] < -10) ub_active[i] = 0;
    }
}

static void update_enemies(void)
{
    static uint8_t i, j, t;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!en_active[i]) continue;

        en_x[i] += en_dx[i];
        en_y[i] += en_dy[i];
        en_flash[i]++;

        /* Off screen? */
        if (en_x[i] < -30 || en_y[i] > SCREEN_H + 10) {
            en_active[i] = 0;
            continue;
        }

        t = en_type[i];
        /* UFO bomb dropping */
        if (t != ENEMY_METEOR && en_bomb[i] > 0) {
            if (--en_bomb[i] == 0) {
                for (j = 0; j < MAX_BOMBS; j++) {
                    if (!bm_active[j]) {
                        bm_x[j] = en_x[i] + 4;
                        bm_y[j] = en_y[i] + 10;
                        bm_active[j] = 1;
                        break;
                    }
                }
                /* Large UFO drops multiple bombs, the small one one */
                if (t == ENEMY_UFO_LG) en_bomb[i] = 40 + rng_mod(30);
            }
        }

        /* Meteor hits ground */
        if (t == ENEMY_METEOR && en_y[i] >= GROUND_Y - 4) {
            en_active[i] = 0;
            ev_explode = 1;
            spawn_explosion(en_x[i], GROUND_Y - 4);
        }
    }

    /* Try spawning new enemies periodically (frame % 60 == 0) */
    if (spawn_ctr == 0) try_spawn_enemy();
}

static void update_bombs(void)
{
    static uint8_t i;
    static int16_t dx;

    for (i = 0; i < MAX_BOMBS; i++) {
        if (!bm_active[i]) continue;

        bm_y[i] += 2;

        /* Hit ground */
        if (bm_y[i] >= GROUND_Y) {
            bm_active[i] = 0;
            ev_explode = 1;
            spawn_explosion(bm_x[i], GROUND_Y);

            /* Check proximity to buggy */
            if (buggy_alive) {
                dx = bm_x[i] - (BUGGY_X + BUGGY_W / 2);
                if (dx < 0) dx = -dx;
                if (dx < 20) {
                    buggy_dies(CAUSE_BOMB);
                    spawn_explosion(BUGGY_X + BUGGY_W / 2, buggy_y + BUGGY_H / 2);
                }
            }
            continue;
        }

        /* Direct hit on buggy */
        if (buggy_alive) {
            if (bm_x[i] >= BUGGY_X && bm_x[i] <= BUGGY_X + BUGGY_W &&
                bm_y[i] >= buggy_y && bm_y[i] <= buggy_y + BUGGY_H) {
                bm_active[i] = 0;
                buggy_dies(CAUSE_BOMB);
                ev_explode = 1;
                spawn_explosion(BUGGY_X + BUGGY_W / 2, buggy_y + BUGGY_H / 2);
            }
        }
    }
}

static const uint8_t enemy_score[4] = { 0, 2, 5, 3 };   /* hundreds: small UFO 200, large 500, meteor 300 */

/* one bullet against the enemies; 1 if it hit one */
static uint8_t bullet_hits(int16_t bx, int16_t by)
{
    static uint8_t j, ew, eh;
    static int16_t x, y;
    for (j = 0; j < MAX_ENEMIES; j++) {
        if (!en_active[j]) continue;
        if (en_type[j] == ENEMY_UFO_LG) { ew = 20; eh = 14; }
        else { ew = 12; eh = 10; }
        x = en_x[j];
        y = en_y[j];
        if (bx >= x && bx <= x + ew && by >= y && by <= y + eh) {
            if (--en_hp[j] == 0) {
                en_active[j] = 0;
                ev_explode = 1;
                spawn_explosion(x + ew / 2, y + eh / 2);
                add_score(enemy_score[en_type[j]], 10 + en_type[j]);
            }
            return 1;
        }
    }
    return 0;
}

static void check_bullet_enemy_collisions(void)
{
    static uint8_t i;
    /* Forward bullets vs enemies */
    for (i = 0; i < MAX_FWD_BULLETS; i++)
        if (fb_active[i] && bullet_hits(fb_x[i], fb_y[i])) fb_active[i] = 0;
    /* Upward bullets vs enemies */
    for (i = 0; i < MAX_UP_BULLETS; i++)
        if (ub_active[i] && bullet_hits(ub_x[i], ub_y[i])) ub_active[i] = 0;
}

static void check_enemy_buggy_collision(void)
{
    static uint8_t i, ew;
    static int16_t x, y;

    if (!buggy_alive) return;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!en_active[i]) continue;
        ew = (en_type[i] == ENEMY_UFO_LG) ? 20 : 12;
        x = en_x[i];
        y = en_y[i];
        /* Check overlap */
        if (BUGGY_X + BUGGY_W > x && BUGGY_X < x + ew &&
            buggy_y + BUGGY_H > y && buggy_y < y + 10) {
            buggy_dies(CAUSE_ENEMY);
            en_active[i] = 0;
            ev_explode = 1;
            spawn_explosion(BUGGY_X + BUGGY_W / 2, buggy_y + BUGGY_H / 2);
            spawn_explosion(x + ew / 2, y + 5);
            break;
        }
    }
}

static void update_explosions(void)
{
    static uint8_t i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!ex_active[i]) continue;
        --ex_life[i];
        if (ex_life[i] > 6) ex_radius[i] += 2;
        if (ex_life[i] == 0) ex_active[i] = 0;
    }
}

static void update_scrolling(void)
{
    static uint16_t f;
    static uint8_t whole;

    /* scroll_x += scroll_speed, as whole pixels and hundredths. The
       original's checkpoint count accumulated the same fraction separately
       (scroll_frac): the same whole pixels. */
    f = scroll_frac + scroll_speed;
    whole = 0;
    while (f >= 100) { f -= 100; ++whole; }
    scroll_frac = (uint8_t)f;
    scroll_px += whole;
    scroll_ring += whole;
    if (scroll_ring >= TERRAIN_LEN) scroll_ring -= TERRAIN_LEN;

    /* Generate new terrain as needed: scroll_x / 100 + SCREEN_W + 100 >
       terrain_gen_x */
    gen_ahead -= whole;
    if (gen_ahead < SCREEN_W + 100 && !gen_busy) {
        gen_start(gen_next, 64, scroll_px + gen_ahead);
        gen_ahead += 64;
        gen_next += 64;
        if (gen_next >= TERRAIN_LEN) gen_next = 0;
    }
    /* port: 16 pixels of the chunk a frame */
    if (gen_busy) gen_steps(16);

    /* Checkpoint tracking */
    checkpoint_dist -= whole;
    if (checkpoint_dist <= 0) {
        checkpoint_cur++;
        if (checkpoint_cur >= 26) {
            /* Completed course - loop with higher difficulty */
            checkpoint_cur = 0;
            difficulty++;
        }
        checkpoint_dist = CHECKPOINT_DIST;
        add_score(10, 1);
        ev_checkpoint = 1;
        state = STATE_CHECKPOINT;
        state_timer = 60;
    }
}

/* ---- Main update ---- */

void game_update(uint8_t inp)
{
    frame++;
    if (++spawn_ctr == 60) spawn_ctr = 0;

    /* Clear event flags */
    ev_shoot = ev_explode = ev_jump = ev_checkpoint = ev_death = 0;

    switch (state) {
    case STATE_PLAYING:
        update_scrolling();
        update_buggy(inp);
        update_bullets();
        update_enemies();
        update_bombs();
        check_bullet_enemy_collisions();
        check_enemy_buggy_collision();
        update_explosions();
        break;

    case STATE_DYING:
        update_explosions();
        if (--state_timer == 0) {
            lives--;
            if (lives == 0) {
                state = STATE_GAMEOVER;
                state_timer = 180;
            } else {
                /* Respawn buggy (where it died, as the original) */
                buggy_alive = 1;
                buggy_y = BUGGY_GROUND_Y;
                buggy_vy = 0;
                buggy_on_ground = 1;
                scroll_speed = SPEED_NORMAL;
                state = STATE_PLAYING;
            }
        }
        break;

    case STATE_GAMEOVER:
        /* Stay in gameover until player presses fire or timer runs out */
        if (--state_timer == 0) state = STATE_TITLE;
        break;

    case STATE_CHECKPOINT:
        /* Brief celebration, then back to playing (the enemies and bombs
           wait, as in the original) */
        update_scrolling();
        update_buggy(inp);
        update_bullets();
        update_explosions();
        if (--state_timer == 0) state = STATE_PLAYING;
        break;

    case STATE_TITLE:
        /* Handled in main loop */
        break;
    }
}
