/*
 * SKY KNIGHTS - game logic (port of amiga_mcp/examples/sky_knights/game.c,
 * taken from the Atari 8-bit port, which made it fast on a 6502).
 * Physics, collision, enemy AI, wave management.
 *
 * port: the same rules; the changes are for the 6502 and marked "port:" -
 * 16-bit fixed point (game.h), the structs as arrays of fields (players
 * and enemies share the body arrays, so one physics step does both), static
 * locals (cc65 reaches them directly), the SDK's xorshift random numbers,
 * the per-frame physics in physics.s.
 */
#include <string.h>
#include "game.h"

GameState gs;
char score_str[8] = "0000000";
/* port: what scored this frame, for the log (main.c clears them) */
uint16_t ev_kill_pts[4];
uint8_t ev_nkill, ev_npod;

/* port: the score as digits too (main.c shows them); one player on the Lynx */
static void score_add(uint8_t j, uint16_t pts)
{
    gs.score[j] += pts;
    if (j == 0) add_score(pts);
}

/* add pts (a multiple of 10, < 10000) to the 7 digits, no divides */
void __fastcall__ add_score(uint16_t pts)
{
    static uint8_t add[4], v, carry;
    static int8_t d;
    add[0] = add[1] = add[2] = add[3] = 0;          /* thousands, hundreds, tens, units */
    while (pts >= 1000) { pts -= 1000; ++add[0]; }
    while (pts >= 100) { pts -= 100; ++add[1]; }
    while (pts >= 10) { pts -= 10; ++add[2]; }
    carry = 0;
    for (d = 6; d >= 0; --d) {
        v = (uint8_t)(score_str[d] - '0' + carry + (d >= 3 ? add[d - 3] : 0));
        carry = v >= 10;
        if (carry) v -= 10;
        score_str[d] = (char)('0' + v);
    }
}

Fixed b_x[NBODIES], b_y[NBODIES], b_dx[NBODIES], b_dy[NBODIES];
/* port: the bodies' whole-pixel positions, kept by body_step (physics.s) for
   the collisions and the screen (y is always 10..236) */
WORD b_px[NBODIES];
uint8_t b_py[NBODIES];
uint8_t b_on_ground[NBODIES], b_facing[NBODIES], b_anim_frame[NBODIES], b_anim_timer[NBODIES];
uint8_t p_active[MAX_PLAYERS], p_alive[MAX_PLAYERS], p_flapping[MAX_PLAYERS];
int8_t p_lives[MAX_PLAYERS];
WORD p_respawn_timer[MAX_PLAYERS];
uint8_t e_type[MAX_ENEMIES], e_active[MAX_ENEMIES];
uint8_t e_flap_timer[MAX_ENEMIES];   /* port: a byte (15..69) */
Fixed g_x[MAX_EGGS], g_y[MAX_EGGS], g_dy[MAX_EGGS];
uint8_t g_active[MAX_EGGS], g_on_ground[MAX_EGGS], g_hatch_type[MAX_EGGS];
WORD g_timer[MAX_EGGS];

/* Platform layout: ground, lower left/right, middle, upper left/right, top */
const WORD plat_x[7] = {0, 20, 220, 110, 0, 250, 130};
const WORD plat_y[7] = {GROUND_Y, 180, 180, 140, 100, 100, 60};
const WORD plat_w[7] = {320, 80, 80, 100, 70, 70, 60};

/* port: the original's 32-bit LCG costs a 32-bit multiply per call and the
   swoopers call it every frame; the SDK's xorshift, 0..0x7FFF */
#define rng() ((WORD)(rnd16() >> 1))
/* port: rng() % n with Suzy's divider (cc65's 16-bit modulo costs ~2k cycles
   a call: a wave of 8 enemies took half a frame) */
#define MATH_(a) (*(volatile uint8_t *)(a))
static uint8_t rng_mod(uint8_t n)
{
    static uint16_t r;
    r = (uint16_t)rng();
    MATH_(0xFC56) = n;                  /* MATHP, MATHN: the divisor */
    MATH_(0xFC57) = 0;
    MATH_(0xFC60) = (uint8_t)r;         /* MATHH..MATHE: the dividend */
    MATH_(0xFC61) = (uint8_t)(r >> 8);
    MATH_(0xFC62) = 0;
    MATH_(0xFC63) = 0;                  /* starts */
    while (MATH_(0xFC92) & 0x80) ;      /* SPRSYS: busy */
    return MATH_(0xFC6C);               /* MATHM: the remainder's low byte (n < 256) */
}

/* port: 0..n-1 from a byte, no division (the flap delays): physics.s's dice8 */
uint8_t dice8(void);
extern uint8_t dice8_seed;
static uint8_t rnd_below(uint8_t n)
{
    uint8_t r = dice8();
    while (r >= n) r -= n;
    return r;
}


/* port: gravity 0.3 px/frame^2 is 19.2/64: 19, and 20 every fifth frame */
Fixed gravity;
static void plat_tables(void);

static void set_pos(uint8_t b)
{
    b_px[b] = FIX_INT(b_x[b]);
    b_py[b] = (uint8_t)FIX_INT(b_y[b]);
}
static uint8_t gravity_phase;

void game_init(void)
{
    uint8_t i;
    memset(&gs, 0, sizeof(GameState));
    memcpy(score_str, "0000000", 7);

    gs.state = STATE_TITLE;
    gs.wave = 0;
    gs.num_players = 1;
    plat_tables();

    for (i = 0; i < MAX_PLAYERS; i++) {
        p_lives[i] = 3;
        p_active[i] = 0;
        p_alive[i] = 0;
        gs.score[i] = 0;
    }
}

static void spawn_player(uint8_t idx)
{
    b_x[idx] = (idx == 0) ? FIX(80) : FIX(220);
    b_y[idx] = FIX(GROUND_Y - PLAYER_H);
    set_pos(idx);
    b_dx[idx] = 0;
    b_dy[idx] = 0;
    p_alive[idx] = 1;
    p_active[idx] = 1;
    b_facing[idx] = (idx == 0) ? 0 : 1;
    b_anim_frame[idx] = 0;
    b_anim_timer[idx] = 0;
    b_on_ground[idx] = 1;
    p_respawn_timer[idx] = 0;
    p_flapping[idx] = 0;
}

void game_start(void)
{
    uint8_t i;

    gs.wave = 0;
    gs.state = STATE_WAVE_INTRO;
    gs.state_timer = 100;

    memset(e_active, 0, sizeof e_active);
    memset(g_active, 0, sizeof g_active);

    /* Reset players */
    for (i = 0; i < gs.num_players; i++) {
        p_lives[i] = 3;
        gs.score[i] = 0;
        spawn_player(i);
        memcpy(score_str, "0000000", 7);
    }

    gs.wave = 1;
    gs.ev_flags |= EV_WAVE;
    dice8_seed = (uint8_t)(rnd16() | 1);
}

static void spawn_enemy(uint8_t type)
{
    uint8_t i, b, pi;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!e_active[i]) {
            b = (uint8_t)(EB + i);
            e_active[i] = 1;
            e_type[i] = type;
            b_facing[b] = (uint8_t)(rng() & 1);

            /* Spawn at random platform */
            pi = (uint8_t)(1 + rng_mod(NUM_PLATFORMS - 1)); /* not ground */
            b_x[b] = FIX(plat_x[pi] + (WORD)rng_mod((uint8_t)plat_w[pi]));
            b_y[b] = FIX(plat_y[pi] - PLAYER_H);
            set_pos(b);

            b_dx[b] = b_facing[b] ? -ENEMY_SLOW : ENEMY_SLOW;
            b_dy[b] = 0;
            b_anim_frame[b] = 0;
            b_anim_timer[b] = 0;
            b_on_ground[b] = 0;
            e_flap_timer[i] = (uint8_t)(20 + rnd_below(30));

            /* Adjust speed by type */
            if (type == ETYPE_RAIDER) {
                b_dx[b] = b_facing[b] ? -ENEMY_MED : ENEMY_MED;
            } else if (type == ETYPE_WRAITH) {
                b_dx[b] = b_facing[b] ? -ENEMY_FAST : ENEMY_FAST;
            }
            return;
        }
    }
}

void game_start_wave(void)
{
    uint8_t i, num_enemies, swoopers, raiders, wraiths;

    /* Clear old enemies and pods */
    memset(e_active, 0, sizeof e_active);
    memset(g_active, 0, sizeof g_active);

    /* Calculate enemy counts based on wave */
    num_enemies = (uint8_t)(gs.wave < 6 ? 2 + gs.wave : MAX_ENEMIES);
    if (num_enemies > MAX_ENEMIES) num_enemies = MAX_ENEMIES;

    swoopers = raiders = wraiths = 0;

    if (gs.wave <= 2) {
        swoopers = num_enemies;
    } else if (gs.wave <= 4) {
        swoopers = num_enemies / 2;
        raiders = num_enemies - swoopers;
    } else {
        swoopers = num_enemies / 3;
        raiders = num_enemies / 3;
        wraiths = num_enemies - swoopers - raiders;
        if (wraiths < 1) wraiths = 1;
    }

    for (i = 0; i < swoopers; i++) spawn_enemy(ETYPE_SWOOPER);
    for (i = 0; i < raiders; i++)  spawn_enemy(ETYPE_RAIDER);
    for (i = 0; i < wraiths; i++)  spawn_enemy(ETYPE_WRAITH);
}

/* port: body_step is physics.s; the ledges as its tables (see there) */
uint8_t plat_rows[256];
uint8_t plat_xa_lo[NUM_PLATFORMS], plat_xa_hi[NUM_PLATFORMS];
uint8_t plat_xb_lo[NUM_PLATFORMS], plat_xb_hi[NUM_PLATFORMS];
uint8_t plat_ut_lo[NUM_PLATFORMS], plat_ut_hi[NUM_PLATFORMS];
uint8_t plat_tp_lo[NUM_PLATFORMS], plat_tp_hi[NUM_PLATFORMS];
extern uint8_t bs_above;
uint8_t __fastcall__ body_step(uint8_t b);
void update_enemies(void);
void check_combat(void);

/* the ledge under a pod (px, py = its bottom, 8 wide): its y, or -1 (the
   original's check_platform, through the table) */
static WORD plat_at(WORD px, WORD py)
{
    static uint8_t m, i;
    if ((uint16_t)py > 255) return -1;
    m = plat_rows[(uint8_t)py];
    for (i = 0; m; ++i, m >>= 1)
        if ((m & 1) && px + 8 > plat_x[i] && px < plat_x[i] + plat_w[i])
            return plat_y[i];
    return -1;
}

static void plat_tables(void)
{
    uint8_t i, r;
    WORD v;
    memset(plat_rows, 0, sizeof plat_rows);
    for (i = 0; i < NUM_PLATFORMS; i++) {
        for (r = 0; r <= PLAT_THICK; r++)
            plat_rows[plat_y[i] + r] |= (uint8_t)(1 << i);
        v = plat_x[i] - PLAYER_W + 1;
        plat_xa_lo[i] = (uint8_t)v;
        plat_xa_hi[i] = (uint8_t)(v >> 8);
        v = plat_x[i] + plat_w[i];
        plat_xb_lo[i] = (uint8_t)v;
        plat_xb_hi[i] = (uint8_t)(v >> 8);
        v = FIX(plat_y[i] + PLAT_THICK);
        plat_ut_lo[i] = (uint8_t)v;
        plat_ut_hi[i] = (uint8_t)(v >> 8);
        v = FIX(plat_y[i] - PLAYER_H);
        plat_tp_lo[i] = (uint8_t)v;
        plat_tp_hi[i] = (uint8_t)(v >> 8);
    }
}

static void update_player(uint8_t i, uint8_t input)
{
    if (!p_alive[i]) {
        if (p_respawn_timer[i] > 0) {
            if (--p_respawn_timer[i] == 0 && p_lives[i] > 0)
                spawn_player(i);
        }
        return;
    }

    /* Horizontal input */
    if (input & INP_LEFT) {
        b_dx[i] = -MOVE_SPEED;
        b_facing[i] = 1;
    } else if (input & INP_RIGHT) {
        b_dx[i] = MOVE_SPEED;
        b_facing[i] = 0;
    } else {
        /* Friction */
        if (b_dx[i] > 0) {
            b_dx[i] -= FRICTION;
            if (b_dx[i] < 0) b_dx[i] = 0;
        } else if (b_dx[i] < 0) {
            b_dx[i] += FRICTION;
            if (b_dx[i] > 0) b_dx[i] = 0;
        }
    }

    /* Flap */
    if (input & INP_FLAP) {
        b_dy[i] += FLAP_IMPULSE / 4; /* spread over frames for smoother feel */
        if (b_dy[i] < MAX_RISE_VEL) b_dy[i] = MAX_RISE_VEL;
        p_flapping[i] = 1;
        b_on_ground[i] = 0;
        gs.ev_flags |= EV_FLAP;
    } else {
        p_flapping[i] = 0;
    }

    bs_above = 1;
    body_step(i);

    /* Animation */
    if (++b_anim_timer[i] > 6) {
        b_anim_timer[i] = 0;
        b_anim_frame[i] ^= 1;
    }
}

/* AI: flap */
void enemy_flap(uint8_t i)
{
    uint8_t b = (uint8_t)(EB + i);
    b_dy[b] += FLAP_IMPULSE / 3;
    if (b_dy[b] < MAX_RISE_VEL) b_dy[b] = MAX_RISE_VEL;
    /* Vary flap timing by type */
    if (e_type[i] == ETYPE_SWOOPER)
        e_flap_timer[i] = (uint8_t)(30 + rnd_below(40));
    else if (e_type[i] == ETYPE_RAIDER)
        e_flap_timer[i] = (uint8_t)(20 + rnd_below(30));
    else
        e_flap_timer[i] = (uint8_t)(15 + rnd_below(20));
}

/* raiders/wraiths seek players */
void enemy_seek(uint8_t i)
{
    static uint8_t b, k, best_dir;
    static Fixed best_dist, dist;
    b = (uint8_t)(EB + i);
    best_dist = 0x7FFF;
    best_dir = b_facing[b];
    /* Find nearest active player */
    for (k = 0; k < gs.num_players; k++) {
        if (p_alive[k]) {
            dist = b_x[k] - b_x[b];
            if (dist < 0) dist = -dist;
            if (dist < best_dist) {
                best_dist = dist;
                best_dir = (b_x[k] < b_x[b]) ? 1 : 0;
            }
        }
    }
    b_facing[b] = best_dir;
    {
        Fixed spd = ENEMY_SLOW;
        if (e_type[i] == ETYPE_RAIDER) spd = ENEMY_MED;
        else if (e_type[i] == ETYPE_WRAITH) spd = ENEMY_FAST;
        b_dx[b] = b_facing[b] ? -spd : spd;
    }
}

/* port: update_enemy for each enemy is physics.s's update_enemies */

static void spawn_egg(Fixed x, Fixed y, uint8_t hatch_type)
{
    uint8_t i;
    for (i = 0; i < MAX_EGGS; i++) {
        if (!g_active[i]) {
            g_active[i] = 1;
            g_x[i] = x;
            g_y[i] = y;
            g_dy[i] = 0;
            g_timer[i] = EGG_HATCH_TIME;
            g_on_ground[i] = 0;
            g_hatch_type[i] = hatch_type;
            return;
        }
    }
}

static void update_eggs(void)
{
    static uint8_t i, j;
    static WORD plat_yv, ex, ey, px, py;
    for (i = 0; i < MAX_EGGS; i++) {
        if (!g_active[i]) continue;

        /* Gravity */
        if (!g_on_ground[i]) {
            g_dy[i] += gravity;
            if (g_dy[i] > MAX_FALL_VEL) g_dy[i] = MAX_FALL_VEL;
            g_y[i] += g_dy[i];

            /* Platform check */
            plat_yv = plat_at(FIX_INT(g_x[i]), FIX_INT(g_y[i]) + 8);
            if (plat_yv >= 0 && g_dy[i] >= 0) {
                g_y[i] = FIX(plat_yv - 8);
                g_dy[i] = 0;
                g_on_ground[i] = 1;
            }

            /* Floor */
            if (FIX_INT(g_y[i]) + 8 >= SCREEN_H) {
                g_y[i] = FIX(SCREEN_H - 8);
                g_dy[i] = 0;
                g_on_ground[i] = 1;
            }
        }

        /* Hatch timer: a stronger enemy */
        if (--g_timer[i] <= 0) {
            uint8_t t = g_hatch_type[i];
            if (t < ETYPE_WRAITH) t++;
            spawn_enemy(t);
            g_active[i] = 0;
            continue;   /* skip pickup check on this now-hatched pod */
        }

        /* Player pickup collision */
        ex = FIX_INT(g_x[i]);
        ey = FIX_INT(g_y[i]);
        for (j = 0; j < gs.num_players; j++) {
            if (!p_alive[j]) continue;
            px = b_px[j];
            py = b_py[j];
            if (px + PLAYER_W > ex && px < ex + 8 &&
                py + PLAYER_H > ey && py < ey + 8) {
                score_add(j, SCORE_EGG);
                if (ev_npod < 255) ++ev_npod;
                g_active[i] = 0;
                gs.ev_flags |= EV_EGG;
                break;  /* only one player collects this pod */
            }
        }
    }
}

/* A joust: player jo_i and enemy jo_e (body EB + e) overlap. Returns 1 if
   the player went down (no more jousts for them this frame). port: called
   from physics.s's check_combat, its arguments in statics */
extern uint8_t jo_i, jo_e;
uint8_t joust(void)
{
    static uint8_t i, e, b, k, all_dead;
    static WORD px, py, ex, ey;
    i = jo_i;
    e = jo_e;
    b = (uint8_t)(EB + e);
    px = b_px[i];
    py = b_py[i];
    ex = b_px[b];
    ey = b_py[b];

    /* Whoever is higher wins */
    if (py < ey - 2) {
        /* Player wins */
        WORD pts = SCORE_SWOOPER;
        if (e_type[e] == ETYPE_RAIDER) pts = SCORE_RAIDER;
        else if (e_type[e] == ETYPE_WRAITH) pts = SCORE_WRAITH;
        score_add(i, pts);
        if (ev_nkill < 4) ev_kill_pts[ev_nkill++] = pts;

        spawn_egg(b_x[b], b_y[b], e_type[e]);
        e_active[e] = 0;
        gs.ev_flags |= EV_KILL;

        /* Bounce player up */
        b_dy[i] = FLAP_IMPULSE / 2;
    } else if (ey < py - 2) {
        /* Enemy wins - player dies */
        p_alive[i] = 0;
        p_lives[i]--;
        p_respawn_timer[i] = 100; /* 2 seconds */
        gs.ev_flags |= EV_DIE;

        if (p_lives[i] <= 0) {
            p_active[i] = 0;
            /* Check if all players dead */
            all_dead = 1;
            for (k = 0; k < gs.num_players; k++) {
                if (p_lives[k] > 0 || p_alive[k]) {
                    all_dead = 0;
                    break;
                }
            }
            if (all_dead) {
                gs.state = STATE_GAMEOVER;
                gs.state_timer = 200;
            }
        }
        /* Player is dead this frame; stop checking further enemies
         * so overlapping enemies can't decrement extra lives. */
        return 1;
    } else {
        /* Equal height - bounce off */
        b_dx[i] = (px < ex) ? -MOVE_SPEED : MOVE_SPEED;
        b_dx[b] = (ex < px) ? -ENEMY_SLOW : ENEMY_SLOW;
        b_dy[i] = FLAP_IMPULSE / 3;
        b_dy[b] = FLAP_IMPULSE / 3;
    }
    return 0;
}

/* port: check_combat is physics.s's: the overlap tests, then joust() */

void game_update(uint8_t in1, uint8_t in2)
{
    uint8_t i, alive;

    gs.frame++;
    if (++gravity_phase == 5) gravity_phase = 0;
    gravity = gravity_phase ? GRAVITY_LO : GRAVITY_LO + 1;

    switch (gs.state) {
    case STATE_WAVE_INTRO:
        if (--gs.state_timer <= 0) {
            game_start_wave();
            gs.state = STATE_PLAYING;
        }
        break;

    case STATE_PLAYING:
        /* Update players */
        update_player(0, in1);
        if (gs.num_players > 1) update_player(1, in2);

        /* Update enemies */
        update_enemies();

        /* Update eggs */
        update_eggs();

        /* Combat */
        check_combat();

        /* Check wave complete */
        alive = 0;
        for (i = 0; i < MAX_ENEMIES; i++) alive += e_active[i];
        /* Also count eggs as "alive" enemies (they can hatch) */
        for (i = 0; i < MAX_EGGS; i++) alive += g_active[i];
        gs.enemies_alive = alive;

        if (alive == 0) {
            /* Wave complete */
            for (i = 0; i < gs.num_players; i++) {
                if (p_alive[i]) score_add(i, SCORE_WAVE);
            }
            gs.wave++;
            gs.state = STATE_WAVE_INTRO;
            gs.state_timer = 100;
            gs.ev_flags |= EV_WAVE;
        }
        break;

    case STATE_GAMEOVER:
        if (--gs.state_timer <= 0) game_init();
        break;
    }
}
