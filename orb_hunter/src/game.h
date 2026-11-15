/*
 * Orb Hunter - game definitions (port of amiga_mcp/examples/orb_hunter/game.h,
 * by way of the Atari 8-bit port, the same cc65).
 *
 * port: the original keeps positions and speeds in 16.16 (32-bit). Every
 * value its rules use is a multiple of 1/16 of a pixel (speeds 1/2, 1/4,
 * 1/8 ..., gravity 1/4, a jump halved when the button is let go), so here
 * they are 12.4 in 16 bits: the same numbers, a sixth of the work on a
 * 6502. The structs' fields are arrays and the hunter's are globals (cc65
 * is slow through pointers). The rules are the original's, step for step.
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

typedef int16_t WORD;
typedef int32_t LONG;
typedef uint8_t UBYTE;

/* Screen dimensions */
#define SCREEN_W    320
#define SCREEN_H    256
#define TILE_W      16
#define TILE_H      16
#define HUD_H       16

/* Room/world dimensions */
#define ROOM_W      20    /* tiles per room width */
#define ROOM_H      16    /* tiles per room height */
#define WORLD_W     8     /* rooms across */
#define WORLD_H     6     /* rooms tall */

/* port: 12.4 fixed point (the original's 16.16 values / 4096) */
typedef int16_t Fixed;
#define FIX(x)       ((Fixed)((x) * 16))
#define FIX_INT(x)   ((WORD)((x) >> 4))

/* Tile types */
#define TILE_EMPTY       0
#define TILE_ROCK        1
#define TILE_BRICK       2
#define TILE_METAL       3
#define TILE_PLATFORM    4   /* one-way platform */
#define TILE_LAVA        5   /* damage tile */
#define TILE_DOOR_LOCKED 6   /* requires missile to open */
#define TILE_DOOR_OPEN   7
#define TILE_ITEM_BLOCK  8   /* contains item */
#define TILE_SAVE_POINT  9
#define NUM_TILE_TYPES   10

/* Tile properties (bit flags) */
#define TPROP_SOLID     1   /* blocks movement */
#define TPROP_PLATFORM  2   /* solid from top only */
#define TPROP_DAMAGE    4   /* hurts player */
#define TPROP_DOOR      8   /* door tile */

extern const UBYTE tile_props[NUM_TILE_TYPES];

/* Hunter (player) dimensions */
#define HUNTER_W        14
#define HUNTER_H        16
#define HUNTER_ROLL_H   8    /* height when in roll form */
#define HUNTER_SPEED    FIX(2)
#define HUNTER_JUMP_VEL FIX(-4)
#define HUNTER_HIGH_JUMP FIX(-5)
#define HUNTER_GRAVITY  (FIX(1) / 4)   /* 0.25 fixed */
#define HUNTER_MAX_FALL FIX(5)
#define GUN_COOLDOWN    8
#define MAX_ENERGY      99
#define START_ENERGY    30
#define INVULN_TIME     60

/* the hunter (the original's Hunter struct) */
extern Fixed h_x, h_y, h_dx, h_dy;
extern uint8_t h_on_ground, h_facing, h_alive, h_jump_held, h_anim_frame, h_rolled;
extern WORD h_health, h_max_energy, h_missiles, h_max_missiles;
extern uint8_t h_gun_timer, h_invuln_timer, h_bomb_timer, h_anim_timer;
extern uint8_t h_has_roll_form, h_has_bombs, h_has_missiles, h_has_long_beam, h_has_ice_beam,
               h_has_spin_strike, h_has_high_jump, h_has_heat_suit;

#define BULLET_TYPE_NORMAL  0
#define BULLET_TYPE_MISSILE 1
#define BULLET_TYPE_ICE     2

#define MAX_BULLETS     6
#define BULLET_SPEED    FIX(5)
#define MISSILE_SPEED   FIX(4)
#define BULLET_LIFE     30

extern Fixed b_x[MAX_BULLETS], b_y[MAX_BULLETS], b_dx[MAX_BULLETS], b_dy[MAX_BULLETS];
extern uint8_t b_life[MAX_BULLETS], b_on[MAX_BULLETS], b_type[MAX_BULLETS], b_power[MAX_BULLETS];

#define MAX_BOMBS       3
#define BOMB_FUSE       30
#define BOMB_RADIUS     24

extern Fixed bm_x[MAX_BOMBS], bm_y[MAX_BOMBS];
extern uint8_t bm_timer[MAX_BOMBS], bm_on[MAX_BOMBS];

#define ENEMY_CRAWLER       0
#define ENEMY_FLYER         1
#define ENEMY_HOPPER        2
#define ENEMY_TURRET        3
#define ENEMY_BOSS_BEAST    4
#define ENEMY_BOSS_DRAKE    5

#define MAX_ENEMIES         4    /* port: 12 in the original; no room has more than 2 */
#define MAX_ENEMY_BULLETS   6

extern Fixed e_x[MAX_ENEMIES], e_y[MAX_ENEMIES], e_dx[MAX_ENEMIES], e_dy[MAX_ENEMIES];
extern uint8_t e_type[MAX_ENEMIES], e_on[MAX_ENEMIES], e_facing[MAX_ENEMIES], e_state[MAX_ENEMIES],
               e_anim_frame[MAX_ENEMIES];
extern int8_t e_health[MAX_ENEMIES];
extern WORD e_timer[MAX_ENEMIES], e_fire_timer[MAX_ENEMIES];

extern Fixed eb_x[MAX_ENEMY_BULLETS], eb_y[MAX_ENEMY_BULLETS], eb_dx[MAX_ENEMY_BULLETS], eb_dy[MAX_ENEMY_BULLETS];
extern uint8_t eb_life[MAX_ENEMY_BULLETS], eb_on[MAX_ENEMY_BULLETS];

#define MAX_PARTICLES   24
extern Fixed p_x[MAX_PARTICLES], p_y[MAX_PARTICLES], p_dx[MAX_PARTICLES], p_dy[MAX_PARTICLES];
extern uint8_t p_life[MAX_PARTICLES], p_color[MAX_PARTICLES], p_on[MAX_PARTICLES];

#define ITEM_ENERGY_TANK    0
#define ITEM_MISSILE_PACK   1
#define ITEM_ROLL_FORM      2
#define ITEM_BOMBS          3
#define ITEM_LONG_BEAM      4
#define ITEM_ICE_BEAM       5
#define ITEM_HIGH_JUMP      6
#define ITEM_SPIN_STRIKE    7
#define ITEM_HEAT_SUIT      8
#define ITEM_MISSILE_DOOR   9
#define MAX_ITEMS           24

typedef struct {
    int8_t room_x, room_y;
    int8_t tile_x, tile_y;
    uint8_t type;
    uint8_t collected;
} ItemSpawn;

#define STATE_TITLE         0
#define STATE_PLAYING       1
#define STATE_DEAD          2
#define STATE_GAMEOVER      3
#define STATE_ITEM_GET      4
#define STATE_ROOM_TRANSITION 5

typedef struct {
    int8_t room_x, room_y;
    LONG score;
    WORD state;
    WORD state_timer;
    WORD frame;
    uint16_t items_collected;   /* bitmask of collected items (13) */
    WORD doors_opened;
    char item_name[16];
    uint8_t room_changed;       /* port: a new room was loaded (the screen repaints) */
} GameState;

extern GameState gs;

/* The world (port: run-length coded rooms, the current one in room[][],
   changes (opened doors) kept in a list; world.c, world_data.c) */
extern UBYTE room[ROOM_H][ROOM_W];
void world_reset(void);
void world_load_room(int8_t rx, int8_t ry);
void world_set_tile(uint8_t tx, uint8_t ty, UBYTE t);

extern const ItemSpawn items_start[];
extern const uint8_t items_count;
extern ItemSpawn g_items[MAX_ITEMS];
extern uint8_t g_num_items;
void items_init(void);

void game_init(void);
void game_load_room(void);
void game_update(uint8_t inp_left, uint8_t inp_right, uint8_t inp_up, uint8_t inp_down,
                 uint8_t inp_jump, uint8_t inp_fire);
void game_spawn_particles(Fixed x, Fixed y, uint8_t count, uint8_t color);

/* sound for the events (sound.c) */
void sfx_shoot(void);
void sfx_hit(void);
void sfx_explode(void);
void sfx_powerup(void);
void sfx_jump(void);
void sfx_player_hit(void);
void sfx_missile(void);
void sfx_door(void);

#endif
