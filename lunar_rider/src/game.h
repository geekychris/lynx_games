/*
 * Lunar Rider (Lynx) - the game's rules: the Amiga original's game.h.
 *
 * The world keeps the original's 320 x 256 coordinates and units (the
 * buggy's 1/100-pixel velocity, the scroll in 1/100 pixels); main.c shows
 * it at half width and 2/5 height. port: the original's GameState struct
 * and its arrays of structs are separate globals and struct-of-arrays
 * (cc65 reaches `en_x[i]` in two instructions, a field through a pointer in
 * twenty), WORDs that fit are bytes.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* Screen constants (the original's) */
#define SCREEN_W    320
#define SCREEN_H    256

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_DYING      2
#define STATE_GAMEOVER   3
#define STATE_CHECKPOINT 4

/* Terrain constants */
#define TERRAIN_LEN     640     /* terrain buffer length (2 screens ahead) */
#define GROUND_Y        200     /* ground baseline Y */
#define TERRAIN_FLAT    0
#define TERRAIN_CRATER_SM 1     /* small crater */
#define TERRAIN_CRATER_LG 2     /* large crater */
#define TERRAIN_ROCK    3       /* boulder obstacle */
#define TERRAIN_HILL    4       /* cosmetic bump */
#define TERRAIN_MINE    5       /* ground mine */

/* Buggy constants */
#define BUGGY_W         24
#define BUGGY_H         12
#define BUGGY_X         80      /* fixed screen X position */
#define BUGGY_GROUND_Y  (GROUND_Y - BUGGY_H)
#define JUMP_VEL        (-600)  /* initial jump velocity (fixed point /100) */
#define GRAVITY         40      /* gravity per frame (/100) */

/* Scroll speeds (fixed point /100) */
#define SPEED_NORMAL    150
#define SPEED_FAST      300
#define SPEED_SLOW      50

/* Limits */
#define MAX_FWD_BULLETS 2
#define MAX_UP_BULLETS  2
#define MAX_BOMBS       8
#define MAX_ENEMIES     8
#define MAX_EXPLOSIONS  6

/* Enemy types */
#define ENEMY_NONE      0
#define ENEMY_UFO_SM    1
#define ENEMY_UFO_LG    2
#define ENEMY_METEOR    3

/* Checkpoint distance */
#define CHECKPOINT_DIST 500

/* Colours (palette indices, the original's) */
#define COL_BLACK       0
#define COL_WHITE       1
#define COL_DKPURPLE    2
#define COL_PURPLE      3
#define COL_BROWN       4
#define COL_DKBROWN     5
#define COL_YELLOW      6
#define COL_GRAY        7
#define COL_RED         8
#define COL_ORANGE      9
#define COL_CYAN        10
#define COL_GREEN       11
#define COL_BLUE        12
#define COL_BRYELLOW    13
#define COL_DKGRAY      14
#define COL_PINK        15

/* port: why the buggy died (for the log only) */
#define CAUSE_CRATER 1
#define CAUSE_ROCK   2
#define CAUSE_MINE   3
#define CAUSE_BOMB   4
#define CAUSE_ENEMY  5

/* ---- the state (the original's GameState) ---- */
extern uint16_t scroll_speed;           /* current speed (/100) */
extern uint8_t  scroll_frac;            /* scroll_x % 100 */
extern uint16_t scroll_px;              /* scroll_x / 100, low 16 bits (world x of the screen's left) */
extern uint16_t scroll_ring;            /* scroll_x / 100 % TERRAIN_LEN */
extern uint8_t  terrain[TERRAIN_LEN];   /* terrain type at each X pixel offset */
extern uint8_t  terrain_h[TERRAIN_LEN]; /* terrain height (y) at each X pixel offset */

extern int16_t  buggy_y, buggy_vy;
extern uint8_t  buggy_on_ground, buggy_alive, buggy_wheel;

extern int16_t  fb_x[MAX_FWD_BULLETS], fb_y[MAX_FWD_BULLETS];
extern uint8_t  fb_active[MAX_FWD_BULLETS];
extern int16_t  ub_x[MAX_UP_BULLETS], ub_y[MAX_UP_BULLETS];
extern uint8_t  ub_active[MAX_UP_BULLETS];

extern int16_t  en_x[MAX_ENEMIES], en_y[MAX_ENEMIES];
extern int8_t   en_dx[MAX_ENEMIES], en_dy[MAX_ENEMIES];
extern uint8_t  en_type[MAX_ENEMIES], en_active[MAX_ENEMIES], en_hp[MAX_ENEMIES];
extern uint8_t  en_bomb[MAX_ENEMIES], en_flash[MAX_ENEMIES];

extern int16_t  bm_x[MAX_BOMBS], bm_y[MAX_BOMBS];
extern uint8_t  bm_active[MAX_BOMBS];

extern int16_t  ex_x[MAX_EXPLOSIONS], ex_y[MAX_EXPLOSIONS];
extern uint8_t  ex_radius[MAX_EXPLOSIONS], ex_life[MAX_EXPLOSIONS], ex_active[MAX_EXPLOSIONS];

extern uint16_t score_h;                /* the score in hundreds (every score is) */
extern uint16_t hi_h;                   /* the session's high score, hundreds (game_init keeps it) */
extern uint8_t  lives, state, state_timer;
extern uint16_t frame;
extern uint8_t  checkpoint_cur;         /* current checkpoint letter (0=A) */
extern int16_t  checkpoint_dist;        /* distance to next checkpoint (world pixels) */
extern uint8_t  difficulty;             /* increases each loop */

/* Sound event flags - set by game, consumed by main loop */
extern uint8_t ev_shoot, ev_explode, ev_jump, ev_checkpoint, ev_death;
extern uint8_t death_cause;             /* port: CAUSE_* of the last death */
extern uint8_t score_why;               /* port: what scored last (log) */

/* port: the terrain's features as a list, for drawing them as sprites
   (the original drew the terrain array column by column). FIFO in world
   order; the terrain arrays stay the rules' only truth. */
#define FEAT_N 256
extern uint8_t  feat_wx_lo[FEAT_N], feat_wx_hi[FEAT_N];      /* world x (low 16 bits) */
extern uint8_t  feat_ring_lo[FEAT_N], feat_ring_hi[FEAT_N];  /* its index in the terrain ring */
extern uint8_t  feat_type[FEAT_N], feat_w[FEAT_N];
extern uint8_t  feat_head, feat_tail;

/* Input */
#define INP_LEFT  1
#define INP_RIGHT 2
#define INP_JUMP  4
#define INP_FIRE  8

void game_init(void);
void game_update(uint8_t inp);

#endif
