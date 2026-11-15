/*
 * Orb Hunter - game logic (port of amiga_mcp/examples/orb_hunter/game.c, by
 * way of the Atari 8-bit port: the same cc65, so the same C).
 *
 * The original's rules in their order, with its 16.16 values in 12.4 (see
 * game.h: the same numbers), the structs as arrays of fields, the hunter's
 * fields as globals and hot locals static (cc65 reaches globals and
 * statics directly, a pointer's fields twenty times slower). Changes
 * beyond that are marked "port:".
 */
#include <string.h>
#pragma static-locals (on)
#include "lynxrnd.h"
#include "game.h"

GameState gs;

/* port: gs.frame / 3 kept as a count (cc65's division is ~400 cycles; the
   animations use frame / 3 and frame / 6 every frame); frame / 4, / 8 as
   unsigned shifts */
static uint8_t frame_c3;
static uint16_t frame_q3;
#define FRAME_U ((uint16_t)gs.frame)

/* Tile properties table */
const UBYTE tile_props[NUM_TILE_TYPES] = {
    0,                          /* EMPTY */
    TPROP_SOLID,                /* ROCK */
    TPROP_SOLID,                /* BRICK */
    TPROP_SOLID,                /* METAL */
    TPROP_PLATFORM,             /* PLATFORM */
    TPROP_DAMAGE,               /* LAVA */
    TPROP_SOLID | TPROP_DOOR,   /* DOOR_LOCKED */
    0,                          /* DOOR_OPEN */
    TPROP_SOLID,                /* ITEM_BLOCK */
    0                           /* SAVE_POINT */
};

/* the hunter */
Fixed h_x, h_y, h_dx, h_dy;
uint8_t h_on_ground, h_facing, h_alive, h_jump_held, h_anim_frame, h_rolled;
WORD h_health, h_max_energy, h_missiles, h_max_missiles;
uint8_t h_gun_timer, h_invuln_timer, h_bomb_timer, h_anim_timer;
uint8_t h_has_roll_form, h_has_bombs, h_has_missiles, h_has_long_beam, h_has_ice_beam,
        h_has_spin_strike, h_has_high_jump, h_has_heat_suit;
/* the rest */
Fixed e_x[MAX_ENEMIES], e_y[MAX_ENEMIES], e_dx[MAX_ENEMIES], e_dy[MAX_ENEMIES];
uint8_t e_type[MAX_ENEMIES], e_on[MAX_ENEMIES], e_facing[MAX_ENEMIES], e_state[MAX_ENEMIES],
        e_anim_frame[MAX_ENEMIES];
int8_t e_health[MAX_ENEMIES];
WORD e_timer[MAX_ENEMIES], e_fire_timer[MAX_ENEMIES];
/* the shots, bombs and sparks */
Fixed b_x[MAX_BULLETS], b_y[MAX_BULLETS], b_dx[MAX_BULLETS], b_dy[MAX_BULLETS];
uint8_t b_life[MAX_BULLETS], b_on[MAX_BULLETS], b_type[MAX_BULLETS], b_power[MAX_BULLETS];
Fixed bm_x[MAX_BOMBS], bm_y[MAX_BOMBS];
uint8_t bm_timer[MAX_BOMBS], bm_on[MAX_BOMBS];
Fixed eb_x[MAX_ENEMY_BULLETS], eb_y[MAX_ENEMY_BULLETS], eb_dx[MAX_ENEMY_BULLETS], eb_dy[MAX_ENEMY_BULLETS];
uint8_t eb_life[MAX_ENEMY_BULLETS], eb_on[MAX_ENEMY_BULLETS];
Fixed p_x[MAX_PARTICLES], p_y[MAX_PARTICLES], p_dx[MAX_PARTICLES], p_dy[MAX_PARTICLES];
uint8_t p_life[MAX_PARTICLES], p_color[MAX_PARTICLES], p_on[MAX_PARTICLES];

/* --- Collision helpers --- */

/* (tx, ty in tiles; out of the room is solid rock) */
static UBYTE tile_at(WORD tx, WORD ty)
{
    if ((uint16_t)tx >= ROOM_W || (uint16_t)ty >= ROOM_H)
        return TILE_ROCK; /* out of bounds = solid */
    return room[ty][tx];
}



#define tile_is_solid(tx, ty)    (tile_props[tile_at(tx, ty)] & TPROP_SOLID)
#define tile_is_platform(tx, ty) (tile_props[tile_at(tx, ty)] & TPROP_PLATFORM)

static WORD tdiv(WORD v);

/* port: the tile properties at a pixel (px / 16, py / 16, as the
   original's tile_is_solid(gs, px / TILE_W, py / TILE_H)): one call where
   the original has the divisions at every test; props.s */
extern WORD ppx, ppy;
extern UBYTE props_asm(void);           /* props.s */
#define props_px(px, py) (ppx = (px), ppy = (py), props_asm())

/* port: pixels to tiles: x / 16 as the original's WORD division (truncating
   towards zero, so -1 / 16 is 0) */
static WORD tdiv(WORD v)
{
    return v < 0 ? -(WORD)((uint16_t)(-v) >> 4) : (WORD)((uint16_t)v >> 4);
}

/* --- Enemy spawn data per room --- */

typedef struct {
    int8_t room_x, room_y;
    int8_t tile_x, tile_y;
    int8_t type;
} EnemySpawn;

static const EnemySpawn room_enemies[] = {
    /* Cavern rooms */
    { 1, 1,  5, 13, ENEMY_CRAWLER }, { 1, 1, 14, 13, ENEMY_FLYER },
    { 2, 1,  8, 13, ENEMY_CRAWLER }, { 2, 1, 15, 10, ENEMY_HOPPER },
    { 3, 1,  6, 13, ENEMY_CRAWLER }, { 3, 1, 12, 13, ENEMY_TURRET },
    { 1, 2,  5, 13, ENEMY_CRAWLER }, { 1, 2, 16, 13, ENEMY_CRAWLER },
    { 3, 2,  4, 13, ENEMY_HOPPER },  { 3, 2, 15, 13, ENEMY_FLYER },
    { 1, 3,  8, 13, ENEMY_CRAWLER }, { 1, 3, 14, 10, ENEMY_TURRET },
    { 2, 3, 10, 13, ENEMY_HOPPER },  { 2, 3, 16, 13, ENEMY_CRAWLER },
    /* Magma rooms */
    { 5, 1,  6, 12, ENEMY_FLYER },   { 5, 1, 14, 12, ENEMY_CRAWLER },
    { 6, 1,  8, 12, ENEMY_TURRET },  { 6, 1, 15, 12, ENEMY_FLYER },
    { 5, 2,  5, 12, ENEMY_HOPPER },  { 5, 2, 12, 12, ENEMY_CRAWLER },
    { 6, 2,  7, 10, ENEMY_FLYER },   { 6, 2, 14, 12, ENEMY_TURRET },
    { 5, 3,  6, 12, ENEMY_CRAWLER }, { 5, 3, 13, 12, ENEMY_HOPPER },
    /* Corridor rooms */
    { 4, 1,  8, 13, ENEMY_CRAWLER }, { 4, 2, 10, 13, ENEMY_FLYER },
    { 3, 3,  8, 13, ENEMY_CRAWLER },
    /* Citadel rooms */
    { 3, 4,  6, 13, ENEMY_TURRET },  { 3, 4, 14, 13, ENEMY_CRAWLER },
    { 4, 4,  5, 13, ENEMY_HOPPER },  { 4, 4, 15, 13, ENEMY_TURRET },
    { 5, 4,  8, 13, ENEMY_FLYER },   { 5, 4, 14, 13, ENEMY_CRAWLER },
    /* Roll form area */
    { 1, 4,  8,  9, ENEMY_CRAWLER },
    /* Boss rooms */
    { 4, 3, 10,  8, ENEMY_BOSS_BEAST },
    { 6, 3, 10,  6, ENEMY_BOSS_DRAKE },
    /* Sentinel */
    { -1, -1, -1, -1, -1 }
};

/* --- random numbers: port: only the sparks use them, so the SDK's 16-bit
   xorshift instead of the original's 32-bit LCG (its tables cost 1.8K) --- */
#define rng_next() (rnd16() & 0x7FFF)

/* --- Particles --- */

static uint8_t n_parts;                       /* port: how many are on */

void game_spawn_particles(Fixed x, Fixed y, uint8_t count, uint8_t color)
{
    uint8_t i, spawned = 0;
    for (i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!p_on[i]) {
            p_x[i] = x;
            p_y[i] = y;
            /* port: (rng() % 512 - 256) * 128 in 16.16 is 1/512 pixels;
               12.4 keeps it in 1/16 (sparks only, they touch nothing) */
            p_dx[i] = ((WORD)(rng_next() & 511) - 256) >> 5;
            p_dy[i] = ((WORD)(rng_next() & 511) - 256) >> 5;
            p_life[i] = (uint8_t)(10 + (rng_next() % 20));
            p_color[i] = color;
            p_on[i] = 1;
            ++n_parts;
            spawned++;
        }
    }
}

static void update_particles(void)
{
    static uint8_t i;
    if (!n_parts) return;                     /* (most frames there are none) */
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (!p_on[i]) continue;
        p_x[i] += p_dx[i];
        p_y[i] += p_dy[i];
        p_dy[i] += FIX(1) / 8;
        if (--p_life[i] == 0) { p_on[i] = 0; --n_parts; }
    }
}

/* --- Init --- */

void game_init(void)
{
    uint8_t i;

    memset(&gs, 0, sizeof(GameState));
    frame_c3 = 0;
    frame_q3 = 0;
    h_x = h_y = h_dx = h_dy = 0;
    h_on_ground = h_facing = h_alive = h_jump_held = h_anim_frame = h_rolled = 0;
    h_health = h_max_energy = h_missiles = h_max_missiles = 0;
    h_gun_timer = h_invuln_timer = h_bomb_timer = h_anim_timer = 0;
    h_has_roll_form = h_has_bombs = h_has_missiles = h_has_long_beam = h_has_ice_beam = 0;
    h_has_spin_strike = h_has_high_jump = h_has_heat_suit = 0;

    /* Regenerate all mutable world state so opened doors, destroyed tiles,
       and every prior collected item are reset for the new game. */
    world_reset();
    items_init();
    for (i = 0; i < g_num_items; i++) {
        g_items[i].collected = 0;
    }

    /* Start in room (2,2) - the starting area */
    gs.room_x = 2;
    gs.room_y = 2;

    /* Init hunter */
    h_x = FIX(5 * TILE_W);
    /* Place hunter on the ground floor - floor is 2 tiles thick at bottom,
       so top of floor = (ROOM_H-2)*TILE_H. Hunter bottom should be there. */
    h_y = FIX((ROOM_H - 2) * TILE_H - HUNTER_H);
    h_dy = FIX(1); /* give a tiny push down so collision snaps us */
    h_alive = 1;
    h_health = START_ENERGY;
    h_max_energy = START_ENERGY;
    h_max_missiles = 0;
    h_facing = 0;
    h_invuln_timer = INVULN_TIME;

    gs.state = STATE_PLAYING;
    game_load_room();
}

/* port: the items in the current room (found when it is loaded: the
   original looks through all of them every frame) */
static uint8_t room_items[4], n_room_items;

void game_load_room(void)
{
    uint8_t i;
    uint8_t ei = 0;

    world_load_room(gs.room_x, gs.room_y);
    gs.room_changed = 1;
    n_room_items = 0;
    for (i = 0; i < g_num_items && n_room_items < 4; i++)
        if (g_items[i].room_x == gs.room_x && g_items[i].room_y == gs.room_y)
            room_items[n_room_items++] = i;

    /* Clear entities */
    memset(b_on, 0, sizeof b_on);
    memset(bm_on, 0, sizeof bm_on);
    memset(e_on, 0, sizeof e_on);
    memset(eb_on, 0, sizeof eb_on);
    memset(p_on, 0, sizeof p_on);
    n_parts = 0;

    /* Spawn enemies for this room */
    for (i = 0; room_enemies[i].room_x >= 0 && ei < MAX_ENEMIES; i++) {
        if (room_enemies[i].room_x == gs.room_x &&
            room_enemies[i].room_y == gs.room_y) {
            e_x[ei] = FIX(room_enemies[i].tile_x * TILE_W);
            e_y[ei] = FIX(room_enemies[i].tile_y * TILE_H);
            e_type[ei] = room_enemies[i].type;
            e_on[ei] = 1;
            e_facing[ei] = 1;
            e_state[ei] = 0;
            e_timer[ei] = 0;
            e_anim_frame[ei] = 0;
            e_fire_timer[ei] = 0;
            e_dx[ei] = e_dy[ei] = 0;

            switch (e_type[ei]) {
                case ENEMY_CRAWLER:     e_health[ei] = 3;  break;
                case ENEMY_FLYER:       e_health[ei] = 4;  break;
                case ENEMY_HOPPER:      e_health[ei] = 3;  break;
                case ENEMY_TURRET:      e_health[ei] = 5;  break;
                case ENEMY_BOSS_BEAST:  e_health[ei] = 20; break;
                case ENEMY_BOSS_DRAKE:  e_health[ei] = 30; break;
            }
            ei++;
        }
    }
}

static void hunter_dies(void)
{
    h_alive = 0;
    gs.state = STATE_DEAD;
    gs.state_timer = 120;
    game_spawn_particles(h_x, h_y, 16, 8);
    sfx_explode();
}

/* --- Hunter update --- */

static void update_hunter(uint8_t inp_left, uint8_t inp_right, uint8_t inp_up, uint8_t inp_down,
                          uint8_t inp_jump, uint8_t inp_fire)
{
    static WORD cur_h, px_left, px_right, px_top, px_bot;
    static Fixed new_x, new_y;

    if (!h_alive) return;

    /* Invulnerability countdown */
    if (h_invuln_timer > 0) h_invuln_timer--;

    /* Gun cooldown */
    if (h_gun_timer > 0) h_gun_timer--;

    /* Bomb timer */
    if (h_bomb_timer > 0) h_bomb_timer--;

    cur_h = h_rolled ? HUNTER_ROLL_H : HUNTER_H;

    /* Roll form toggle: press down when on ground and have roll form */
    if (inp_down && h_on_ground && h_has_roll_form && !inp_left && !inp_right) {
        if (!h_rolled) {
            h_rolled = 1;
            /* Adjust Y so hunter doesn't float */
            h_y += FIX(HUNTER_H - HUNTER_ROLL_H);
        }
    }
    /* Un-roll: press up when rolled */
    if (inp_up && h_rolled && h_on_ground) {
        /* Check ceiling clearance */
        WORD cy = FIX_INT(h_y) - (HUNTER_H - HUNTER_ROLL_H);
        if (!((props_px(FIX_INT(h_x), cy) | props_px(FIX_INT(h_x) + HUNTER_W - 1, cy)) & TPROP_SOLID)) {
            h_y -= FIX(HUNTER_H - HUNTER_ROLL_H);
            h_rolled = 0;
        }
    }

    /* Horizontal movement */
    if (inp_left) {
        h_dx = -HUNTER_SPEED;
        h_facing = 1;
    } else if (inp_right) {
        h_dx = HUNTER_SPEED;
        h_facing = 0;
    } else {
        h_dx = 0;
    }

    /* Jump (not when rolled) */
    if (inp_jump && h_on_ground && !h_jump_held && !h_rolled) {
        if (h_has_high_jump) {
            h_dy = HUNTER_HIGH_JUMP;
        } else {
            h_dy = HUNTER_JUMP_VEL;
        }
        h_on_ground = 0;
        h_jump_held = 1;
        sfx_jump();
    }
    if (!inp_jump) {
        h_jump_held = 0;
        if (h_dy < 0) {
            h_dy = -(Fixed)((uint16_t)(-h_dy) >> 1);   /* dy / 2 (truncated, as C's) */
        }
    }

    /* Gravity */
    h_dy += HUNTER_GRAVITY;
    if (h_dy > HUNTER_MAX_FALL) h_dy = HUNTER_MAX_FALL;

    /* Horizontal collision */
    new_x = h_x + h_dx;

    /* Only clamp the right edge here; leaving new_x negative lets
       check_room_transition() detect a left-room transition. */
    if (FIX_INT(new_x) + HUNTER_W > ROOM_W * TILE_W)
        new_x = FIX(ROOM_W * TILE_W - HUNTER_W);

    if (new_x < 0) {
        /* Moving off left edge: let position stand for the transition
           check but skip tile collision on negative coords. */
        h_x = new_x;
    } else {
        px_left = FIX_INT(new_x);
        px_right = px_left + HUNTER_W - 1;
        px_top = FIX_INT(h_y);
        px_bot = px_top + cur_h - 1;

        /* (the four corners' tiles: tile_is_solid(px / 16, py / 16)) */
        if ((props_px(px_left, px_top) | props_px(px_left, px_bot) |
             props_px(px_right, px_top) | props_px(px_right, px_bot)) & TPROP_SOLID) {
            h_dx = 0;
        } else {
            h_x = new_x;
        }
    }

    /* Vertical collision */
    new_y = h_y + h_dy;
    px_left = FIX_INT(h_x);
    px_right = px_left + HUNTER_W - 1;
    px_top = FIX_INT(new_y);
    px_bot = px_top + cur_h - 1;

    h_on_ground = 0;

    if (h_dy >= 0) {
        /* Falling - check floor: which tile row are the feet in? */
        if ((props_px(px_left, px_bot) | props_px(px_right, px_bot)) & (TPROP_SOLID | TPROP_PLATFORM)) {
            /* Snap: place hunter so bottom pixel is 1 above the tile top */
            h_y = FIX(tdiv(px_bot) * TILE_H - cur_h);
            h_dy = 0;
            h_on_ground = 1;
        } else {
            h_y = new_y;
        }
    } else {
        /* Rising - let a negative new_y through so check_room_transition()
           can detect a top-room exit before we clamp against the ceiling. */
        if (px_top < 0) {
            h_y = new_y;
        } else if ((props_px(px_left, px_top) | props_px(px_right, px_top)) & TPROP_SOLID) {
            h_y = FIX((tdiv(px_top) + 1) * TILE_H);
            h_dy = 0;
        } else {
            h_y = new_y;
        }
    }

    /* Lava damage */
    {
        WORD px = FIX_INT(h_x) + HUNTER_W / 2;
        WORD py = FIX_INT(h_y) + cur_h;
        if ((props_px(px, py) & TPROP_DAMAGE) && h_invuln_timer == 0) {
            h_health -= h_has_heat_suit ? 1 : 5;
            h_invuln_timer = INVULN_TIME / 2;
            sfx_player_hit();
            if (h_health <= 0) hunter_dies();
        }
    }

    /* Fire weapon */
    if (inp_fire && h_gun_timer == 0) {
        uint8_t bi;
        if (h_rolled) {
            /* Lay bomb if rolled and has bombs */
            if (h_has_bombs && h_bomb_timer == 0) {
                for (bi = 0; bi < MAX_BOMBS; bi++) {
                    if (!bm_on[bi]) {
                        bm_x[bi] = h_x + FIX(HUNTER_W / 2);
                        bm_y[bi] = h_y + FIX(HUNTER_ROLL_H / 2);
                        bm_timer[bi] = BOMB_FUSE;
                        bm_on[bi] = 1;
                        h_bomb_timer = 15;
                        break;
                    }
                }
            }
        } else {
            /* Shoot beam or missile */
            uint8_t use_missile = (inp_up && h_has_missiles && h_missiles > 0);
            for (bi = 0; bi < MAX_BULLETS; bi++) {
                if (!b_on[bi]) {
                    b_x[bi] = h_x + (h_facing ? 0 : FIX(HUNTER_W));
                    b_y[bi] = h_y + FIX(6);
                    if (use_missile) {
                        b_dx[bi] = h_facing ? -MISSILE_SPEED : MISSILE_SPEED;
                        b_type[bi] = BULLET_TYPE_MISSILE;
                        b_power[bi] = 5;
                        h_missiles--;
                        sfx_missile();
                    } else {
                        b_dx[bi] = h_facing ? -BULLET_SPEED : BULLET_SPEED;
                        if (h_has_ice_beam) {
                            b_type[bi] = BULLET_TYPE_ICE;
                            b_power[bi] = 2;
                        } else {
                            b_type[bi] = BULLET_TYPE_NORMAL;
                            b_power[bi] = h_has_long_beam ? 2 : 1;
                        }
                        sfx_shoot();
                    }
                    b_dy[bi] = 0;
                    b_life[bi] = h_has_long_beam ? 45 : BULLET_LIFE;
                    b_on[bi] = 1;
                    h_gun_timer = GUN_COOLDOWN;
                    break;
                }
            }
        }
    }

    /* Animation */
    if (h_rolled) {
        h_anim_frame = (uint8_t)(frame_q3 & 3);           /* (gs.frame / 3) & 3 */
    } else if (h_dx != 0) {
        h_anim_frame = (uint8_t)((FRAME_U >> 2) & 3);
    } else {
        h_anim_frame = 0;
    }
}

/* --- Room transitions --- */

static void check_room_transition(void)
{
    WORD cur_h = h_rolled ? HUNTER_ROLL_H : HUNTER_H;

    /* Right edge */
    if (FIX_INT(h_x) + HUNTER_W >= ROOM_W * TILE_W) {
        if (gs.room_x < WORLD_W - 1) {
            gs.room_x++;
            h_x = FIX(2);
            game_load_room();
        } else {
            h_x = FIX(ROOM_W * TILE_W - HUNTER_W - 1);
        }
        return;
    }

    /* Left edge */
    if (FIX_INT(h_x) < 0) {
        if (gs.room_x > 0) {
            gs.room_x--;
            h_x = FIX(ROOM_W * TILE_W - HUNTER_W - 2);
            game_load_room();
        } else {
            h_x = 0;
        }
        return;
    }

    /* Bottom edge */
    if (FIX_INT(h_y) + cur_h >= ROOM_H * TILE_H) {
        if (gs.room_y < WORLD_H - 1) {
            gs.room_y++;
            h_y = FIX(2);
            game_load_room();
        } else {
            h_y = FIX(ROOM_H * TILE_H - cur_h - 1);
        }
        return;
    }

    /* Top edge */
    if (FIX_INT(h_y) < 0) {
        if (gs.room_y > 0) {
            gs.room_y--;
            h_y = FIX(ROOM_H * TILE_H - cur_h - 2);
            game_load_room();
        } else {
            h_y = 0;
        }
        return;
    }
}

/* --- Bullet update --- */

static void update_bullets(void)
{
    static uint8_t i;
    static WORD px, py, tx, ty;
    static UBYTE tt;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!b_on[i]) continue;

        b_x[i] += b_dx[i];
        b_y[i] += b_dy[i];

        if (--b_life[i] == 0) { b_on[i] = 0; continue; }

        /* Tile collision */
        px = FIX_INT(b_x[i]);
        py = FIX_INT(b_y[i]);
        tx = tdiv(px);
        ty = tdiv(py);
        tt = tile_at(tx, ty);

        /* Missile opens locked doors */
        if ((tile_props[tt] & TPROP_DOOR) && b_type[i] == BULLET_TYPE_MISSILE) {
            world_set_tile((uint8_t)tx, (uint8_t)ty, TILE_DOOR_OPEN);
            b_on[i] = 0;
            sfx_door();
            game_spawn_particles(b_x[i], b_y[i], 6, 4);
            continue;
        }
        if (tile_props[tt] & TPROP_SOLID) {
            b_on[i] = 0;
            game_spawn_particles(b_x[i], b_y[i], 3, 10);
        }

        /* Off room bounds */
        if (px < 0 || px >= ROOM_W * TILE_W || py < 0 || py >= ROOM_H * TILE_H) {
            b_on[i] = 0;
        }
    }
}

/* --- Bomb update --- */

static void update_bombs(void)
{
    uint8_t i, j;
    WORD edx, edy;
    for (i = 0; i < MAX_BOMBS; i++) {
        if (!bm_on[i]) continue;

        if (--bm_timer[i] == 0) {
            /* Detonate */
            bm_on[i] = 0;
            sfx_explode();
            game_spawn_particles(bm_x[i], bm_y[i], 10, 9);

            /* Damage nearby enemies */
            for (j = 0; j < MAX_ENEMIES; j++) {
                if (!e_on[j]) continue;
                edx = FIX_INT(bm_x[i]) - FIX_INT(e_x[j]) - 8;
                edy = FIX_INT(bm_y[i]) - FIX_INT(e_y[j]) - 8;
                if (edx > -BOMB_RADIUS && edx < BOMB_RADIUS &&
                    edy > -BOMB_RADIUS && edy < BOMB_RADIUS) {
                    e_health[j] -= 3;
                }
            }

            /* Bomb jump: push hunter up if nearby */
            edx = FIX_INT(bm_x[i]) - FIX_INT(h_x) - HUNTER_W / 2;
            edy = FIX_INT(bm_y[i]) - FIX_INT(h_y) - HUNTER_ROLL_H / 2;
            if (edx > -BOMB_RADIUS && edx < BOMB_RADIUS &&
                edy > -BOMB_RADIUS && edy < BOMB_RADIUS) {
                h_dy = FIX(-3);
            }
        }
    }
}

/* --- Enemy AI --- */

static void enemy_fire(uint8_t e)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!eb_on[i]) {
            eb_x[i] = e_x[e] + FIX(8);
            eb_y[i] = e_y[e] + FIX(4);
            eb_dx[i] = h_x < e_x[e] ? -FIX(2) : FIX(2);
            eb_dy[i] = 0;
            eb_life[i] = 50;
            eb_on[i] = 1;
            break;
        }
    }
}

static void update_enemies(void)
{
    static uint8_t i, ef, es;
    static WORD px, py, bob, et, eft;
    static Fixed ex, ey, edx, edy;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!e_on[i]) continue;

        /* Kill dead enemies */
        if (e_health[i] <= 0) {
            e_on[i] = 0;
            sfx_explode();
            game_spawn_particles(e_x[i] + FIX(8), e_y[i] + FIX(8), 10, 9);
            gs.score += (e_type[i] >= ENEMY_BOSS_BEAST) ? 2000 : 100;
            continue;
        }

        /* (the creature's fields in statics for the step: cc65 reaches an
           int array's element through a pointer every time) */
        ex = e_x[i]; ey = e_y[i]; edx = e_dx[i]; edy = e_dy[i];
        ef = e_facing[i]; es = e_state[i];
        et = ++e_timer[i];
        eft = ++e_fire_timer[i];

        switch (e_type[i]) {
            case ENEMY_CRAWLER:
                edx = ef ? -FIX(1) : FIX(1);
                ex += edx;
                /* Wall check */
                px = FIX_INT(ex) + (ef ? 0 : 15);
                py = FIX_INT(ey) + 8;
                if (props_px(px, py) & TPROP_SOLID)
                    ef ^= 1;
                /* Edge check */
                px = FIX_INT(ex) + (ef ? 0 : 15);
                py = FIX_INT(ey) + 17;
                if (!(props_px(px, py) & (TPROP_SOLID | TPROP_PLATFORM)))
                    ef ^= 1;
                e_anim_frame[i] = (uint8_t)((frame_q3 >> 1) & 3);  /* (gs.frame / 6) & 3 */
                break;

            case ENEMY_FLYER:
                edx = ef ? -FIX(1) : FIX(1);
                ex += edx;
                /* Sine bob */
                bob = (et * 4) & 0xFF;
                if (bob < 64) edy = FIX(1);
                else if (bob < 192) edy = -FIX(1);
                else edy = FIX(1);
                ey += edy / 4;
                /* Chase player Y gently */
                if (h_y < ey) ey -= FIX(1) / 8;
                else ey += FIX(1) / 8;
                /* Bounds */
                px = FIX_INT(ex);
                if (px < TILE_W || px > (ROOM_W - 2) * TILE_W)
                    ef ^= 1;
                /* Shoot */
                if (eft > 80) {
                    e_x[i] = ex; e_y[i] = ey; enemy_fire(i);
                    eft = 0;
                }
                e_anim_frame[i] = (uint8_t)((FRAME_U >> 2) & 3);
                break;

            case ENEMY_HOPPER:
                if (es == 0) {
                    /* On ground, waiting */
                    if (et > 30) {
                        ef = (h_x < ex) ? 1 : 0;
                        edx = ef ? -FIX(2) : FIX(2);
                        edy = -FIX(3);
                        es = 1;
                        et = 0;
                    }
                } else {
                    /* In air */
                    ex += edx;
                    edy += HUNTER_GRAVITY;
                    ey += edy;
                    /* Landing */
                    py = FIX_INT(ey) + 15;
                    px = FIX_INT(ex) + 8;
                    if (edy > 0 && (props_px(px, py) & (TPROP_SOLID | TPROP_PLATFORM))) {
                        ey = FIX(tdiv(py) * TILE_H - 16);
                        edy = 0;
                        edx = 0;
                        es = 0;
                        et = 0;
                    }
                }
                e_anim_frame[i] = (es == 1) ? 1 : 0;
                break;

            case ENEMY_TURRET:
                ef = (h_x < ex) ? 1 : 0;
                if (eft > 50) {
                    e_x[i] = ex; e_y[i] = ey; enemy_fire(i);
                    eft = 0;
                }
                e_anim_frame[i] = (uint8_t)((FRAME_U >> 3) & 1);
                break;

            case ENEMY_BOSS_BEAST:
                /* Slow movement, fires spread projectiles */
                edx = (es == 0) ? FIX(1) / 2 : -FIX(1) / 2;
                if (et > 80) { es ^= 1; et = 0; }
                ex += edx;
                {
                    WORD fr = 25 + e_health[i];
                    if (fr < 20) fr = 20;
                    if (eft > fr) {
                        uint8_t k, bi;
                        for (k = 0; k < 3; k++) {
                            for (bi = 0; bi < MAX_ENEMY_BULLETS; bi++) {
                                if (!eb_on[bi]) {
                                    eb_x[bi] = ex + FIX(8);
                                    eb_y[bi] = ey + FIX(k * 10);
                                    eb_dx[bi] = (h_x < ex) ? -FIX(2) : FIX(2);
                                    eb_dy[bi] = FIX((WORD)k - 1);
                                    eb_life[bi] = 50;
                                    eb_on[bi] = 1;
                                    break;
                                }
                            }
                        }
                        eft = 0;
                    }
                }
                e_anim_frame[i] = (uint8_t)((FRAME_U >> 2) & 3);
                break;

            case ENEMY_BOSS_DRAKE:
                /* Flying boss, swoops at player */
                if (es == 0) {
                    ey += (et & 32) ? FIX(1) / 4 : -FIX(1) / 4;
                    ef = (h_x < ex) ? 1 : 0;
                    if (et > 60) {
                        es = 1;
                        et = 0;
                        edx = (h_x < ex) ? -FIX(3) : FIX(3);
                        edy = FIX(2);
                    }
                    if (eft > 40) { e_x[i] = ex; e_y[i] = ey; enemy_fire(i); eft = 0; }
                } else if (es == 1) {
                    ex += edx;
                    ey += edy;
                    if (et > 30) { es = 2; et = 0; edy = -FIX(2); }
                } else {
                    ex += edx / 2;
                    ey += edy;
                    if (et > 20) { es = 0; et = 0; edx = 0; edy = 0; }
                }
                if (FIX_INT(ex) < TILE_W * 2) { ex = FIX(TILE_W * 2); edx = -edx; }
                if (FIX_INT(ex) > (ROOM_W - 3) * TILE_W) { ex = FIX((ROOM_W - 3) * TILE_W); edx = -edx; }
                if (FIX_INT(ey) < TILE_W * 2) ey = FIX(TILE_W * 2);
                e_anim_frame[i] = (uint8_t)(frame_q3 & 3);         /* (gs.frame / 3) & 3 */
                break;
        }
        e_x[i] = ex; e_y[i] = ey; e_dx[i] = edx; e_dy[i] = edy;
        e_facing[i] = ef; e_state[i] = es; e_timer[i] = et; e_fire_timer[i] = eft;
    }
}

/* --- Enemy bullet update --- */

static void update_enemy_bullets(void)
{
    static uint8_t i;
    static WORD cur_h, half, dx, dy;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!eb_on[i]) continue;

        eb_x[i] += eb_dx[i];
        eb_y[i] += eb_dy[i];

        if (--eb_life[i] == 0) { eb_on[i] = 0; continue; }

        /* Tile collision */
        if (props_px(FIX_INT(eb_x[i]), FIX_INT(eb_y[i])) & TPROP_SOLID)
            eb_on[i] = 0;

        /* Hit player */
        if (h_alive && h_invuln_timer == 0) {
            cur_h = h_rolled ? HUNTER_ROLL_H : HUNTER_H;
            half = cur_h >> 1;
            dx = FIX_INT(eb_x[i]) - FIX_INT(h_x) - HUNTER_W / 2;
            dy = FIX_INT(eb_y[i]) - FIX_INT(h_y) - half;
            if (dx > -10 && dx < 10 && dy > -half && dy < half) {
                eb_on[i] = 0;
                h_health -= 5;
                h_invuln_timer = INVULN_TIME;
                sfx_player_hit();
                if (h_health <= 0) hunter_dies();
            }
        }
    }
}

/* --- Collision: bullets vs enemies --- */

static void check_bullet_enemy(void)
{
    static uint8_t i, j;
    static WORD rad, dx, dy;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!b_on[i]) continue;

        for (j = 0; j < MAX_ENEMIES; j++) {
            if (!e_on[j]) continue;

            dx = FIX_INT(b_x[i]) - FIX_INT(e_x[j]) - 8;
            dy = FIX_INT(b_y[i]) - FIX_INT(e_y[j]) - 8;
            rad = (e_type[j] >= ENEMY_BOSS_BEAST) ? 16 : 10;

            if (dx > -rad && dx < rad && dy > -rad && dy < rad) {
                e_health[j] -= b_power[i];
                b_on[i] = 0;
                sfx_hit();
                game_spawn_particles(b_x[i], b_y[i], 4, 10);
                break;
            }
        }
    }
}

/* --- Collision: hunter vs enemies --- */

static void check_hunter_enemy(void)
{
    static uint8_t i;
    static WORD cur_h, half, dx, dy, hxc, hyc;

    if (!h_alive || h_invuln_timer > 0) return;

    cur_h = h_rolled ? HUNTER_ROLL_H : HUNTER_H;
    half = cur_h >> 1;                      /* (cur_h / 2: 4 or 8) */

    /* Spin strike kills enemies on contact */
    hxc = FIX_INT(h_x) + HUNTER_W / 2 - 8;
    hyc = FIX_INT(h_y) + half - 8;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!e_on[i]) continue;

        dx = hxc - FIX_INT(e_x[i]);
        dy = hyc - FIX_INT(e_y[i]);
        if (dx > -14 && dx < 14 && dy > -half && dy < half) {
            if (h_has_spin_strike && !h_on_ground && !h_rolled) {
                /* Spin strike kills on contact */
                e_health[i] = 0;
                sfx_explode();
                game_spawn_particles(e_x[i] + FIX(8), e_y[i] + FIX(8), 10, 9);
            } else {
                h_health -= 5;
                h_invuln_timer = INVULN_TIME;
                sfx_player_hit();
                if (h_health <= 0) hunter_dies();
            }
            break;
        }
    }
}

/* --- Item collection --- */

static const char *const item_names[9] = {
    "POWER CELL", "ROCKET PACK", "ROLL FORM", "BOMBS", "FAR SHOT",
    "FROST RAY", "SPRING BOOTS", "SPIN STRIKE", "HEAT SUIT",
};

static void check_items(void)
{
    uint8_t i, k;
    WORD cur_h, dx, dy;

    if (!h_alive) return;

    cur_h = h_rolled ? HUNTER_ROLL_H : HUNTER_H;

    for (k = 0; k < n_room_items; k++) {
        ItemSpawn *it = &g_items[i = room_items[k]];

        if (it->collected) continue;

        dx = FIX_INT(h_x) + HUNTER_W / 2 - it->tile_x * TILE_W - TILE_W / 2;
        dy = FIX_INT(h_y) + cur_h / 2 - it->tile_y * TILE_H - TILE_H / 2;

        if (dx > -16 && dx < 16 && dy > -16 && dy < 16) {
            it->collected = 1;
            gs.items_collected |= (uint16_t)1 << i;
            sfx_powerup();
            game_spawn_particles(FIX(it->tile_x * TILE_W), FIX(it->tile_y * TILE_H), 8, 4);

            switch (it->type) {
                case ITEM_ENERGY_TANK:
                    h_max_energy += 30;
                    h_health = h_max_energy;
                    break;
                case ITEM_MISSILE_PACK:
                    h_has_missiles = 1;
                    h_max_missiles += 5;
                    h_missiles = h_max_missiles;
                    break;
                case ITEM_ROLL_FORM:   h_has_roll_form = 1;   break;
                case ITEM_BOMBS:       h_has_bombs = 1;       break;
                case ITEM_LONG_BEAM:   h_has_long_beam = 1;   break;
                case ITEM_ICE_BEAM:    h_has_ice_beam = 1;    break;
                case ITEM_HIGH_JUMP:   h_has_high_jump = 1;   break;
                case ITEM_SPIN_STRIKE: h_has_spin_strike = 1; break;
                case ITEM_HEAT_SUIT:   h_has_heat_suit = 1;   break;
            }
            strcpy(gs.item_name, it->type < 9 ? item_names[it->type] : "UNKNOWN");

            gs.state = STATE_ITEM_GET;
            gs.state_timer = 90;
            return;
        }
    }
}

/* --- Main update --- */



void game_update(uint8_t inp_left, uint8_t inp_right, uint8_t inp_up, uint8_t inp_down,
                 uint8_t inp_jump, uint8_t inp_fire)
{
    gs.frame++;
    if (++frame_c3 == 3) { frame_c3 = 0; ++frame_q3; }     /* frame_q3 = frame / 3 */

    switch (gs.state) {
        case STATE_PLAYING:
            update_hunter(inp_left, inp_right, inp_up, inp_down, inp_jump, inp_fire);
            update_bullets();
            update_bombs();
            update_enemies();
            update_enemy_bullets();
            update_particles();
            check_bullet_enemy();
            check_hunter_enemy();
            check_items();
            check_room_transition();
            break;

        case STATE_DEAD:
            update_particles();
            if (--gs.state_timer <= 0) {
                gs.state = STATE_GAMEOVER;
                gs.state_timer = 180;
            }
            break;

        case STATE_GAMEOVER:
            if (--gs.state_timer <= 0) {
                gs.state = STATE_TITLE;
            }
            break;

        case STATE_ITEM_GET:
            /* Brief pause when collecting a powerup */
            if (--gs.state_timer <= 0) {
                gs.state = STATE_PLAYING;
            }
            break;

        case STATE_ROOM_TRANSITION:
            if (--gs.state_timer <= 0) {
                gs.state = STATE_PLAYING;
            }
            break;
    }
}
