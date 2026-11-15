/*
 * SKY KNIGHTS - game definitions (port of amiga_mcp/examples/sky_knights/game.h,
 * by way of the Atari 8-bit port's 6502 speed-ups).
 *
 * The rules run in the original's 320 x 256 world; main.c draws it at
 * x / 2, y * 2 / 5 (160 x 102). Changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "lynxrnd.h"

typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int32_t LONG;

/* Screen dimensions (the original's world) */
#define SCREEN_W    320
#define SCREEN_H    256

/* port: positions and speeds are 16-bit with 6 fraction bits (1/64 pixel)
   instead of 16.16 in 32 bits, as in the 8-bit ports: every constant
   is exact at 1/64 except gravity (0.3 = 19.2/64), added as 19 four frames
   in five and 20 the fifth */
typedef int16_t Fixed;
#undef FIX
#undef FIX_INT
#define FIX_SHIFT    6
#define FIX(x)       ((Fixed)((x) * 64))
#define FIX_INT(x)   ((WORD)((x) >> FIX_SHIFT))

#define GRAVITY_LO      19      /* 0.3 = 19.2/64 */
#define FLAP_IMPULSE   (-192)   /* -3.0 */
#define MAX_FALL_VEL    256     /* 4.0 */
#define MAX_RISE_VEL   (-320)   /* -5.0 */
#define MOVE_SPEED      128     /* 2.0 */
#define FRICTION        8       /* 0.125 */
#define ENEMY_SLOW      64      /* 1.0 */
#define ENEMY_MED       96      /* 1.5 */
#define ENEMY_FAST      128     /* 2.0 */

/* Platforms */
#define MAX_PLATFORMS   8
#define GROUND_Y        236
#define PLAT_THICK      8
extern const WORD plat_x[7], plat_y[7], plat_w[7];
#define NUM_PLATFORMS   7

/* Knight dimensions (player and enemy, same size) */
#define PLAYER_W    20
#define PLAYER_H    20

/* Enemy types */
#define ETYPE_SWOOPER    0
#define ETYPE_RAIDER     1
#define ETYPE_WRAITH     2

#define MAX_ENEMIES     8
#define MAX_EGGS        8
#define MAX_PLAYERS     2

/* port: the bodies (the original's Player and Enemy) as arrays of fields:
   0..1 are the players, 2..9 the enemies, so one physics routine does both */
#define NBODIES     (MAX_PLAYERS + MAX_ENEMIES)
#define EB          MAX_PLAYERS                  /* the first enemy's body */
extern Fixed b_x[NBODIES], b_y[NBODIES], b_dx[NBODIES], b_dy[NBODIES];
extern uint8_t b_on_ground[NBODIES], b_facing[NBODIES], b_anim_frame[NBODIES], b_anim_timer[NBODIES];
/* players */
extern WORD b_px[NBODIES];
extern uint8_t b_py[NBODIES];
extern uint8_t p_active[MAX_PLAYERS], p_alive[MAX_PLAYERS], p_flapping[MAX_PLAYERS];
extern int8_t p_lives[MAX_PLAYERS];
extern WORD p_respawn_timer[MAX_PLAYERS];
/* enemies (index i is body EB + i) */
extern uint8_t e_type[MAX_ENEMIES], e_active[MAX_ENEMIES];
extern uint8_t e_flap_timer[MAX_ENEMIES];
/* pods */
extern Fixed g_x[MAX_EGGS], g_y[MAX_EGGS], g_dy[MAX_EGGS];
extern uint8_t g_active[MAX_EGGS], g_on_ground[MAX_EGGS], g_hatch_type[MAX_EGGS];
extern WORD g_timer[MAX_EGGS];

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_WAVE_INTRO 2
#define STATE_DYING      3
#define STATE_GAMEOVER   4

/* Sound event flags */
#define EV_FLAP      1
#define EV_KILL      2
#define EV_EGG       4
#define EV_DIE       8
#define EV_WAVE      16

/* Input bits */
#define INP_LEFT     1
#define INP_RIGHT    2
#define INP_FLAP     4

typedef struct {
    LONG      score[MAX_PLAYERS];
    WORD      wave;
    WORD      state;
    WORD      state_timer;
    WORD      frame;
    WORD      num_players;    /* 1 or 2 */
    WORD      enemies_alive;
    WORD      ev_flags;       /* sound event flags */
} GameState;

extern GameState gs;

/* port: the score is also kept as 7 decimal digits for the status line (no
   32-bit divides to print it); add_score adds to both */
extern char score_str[8];
void __fastcall__ add_score(uint16_t pts);
extern uint16_t ev_kill_pts[4];         /* this frame's jousts won: their points */
extern uint8_t ev_nkill, ev_npod;       /* and the pods taken */

/* Pod timing */
#define EGG_HATCH_TIME  300
#define EGG_WOBBLE_TIME 240

/* Scoring */
#define SCORE_SWOOPER   500
#define SCORE_RAIDER    750
#define SCORE_WRAITH    1000
#define SCORE_EGG       250
#define SCORE_WAVE      1000

void game_init(void);
void game_start(void);
void game_update(uint8_t in1, uint8_t in2);
void game_start_wave(void);

#endif
