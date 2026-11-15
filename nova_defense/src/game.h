/*
 * Nova Defense (Atari Lynx) - game definitions. Port of
 * amiga_mcp/examples/nova_defense/game.h, by way of the Atari 8-bit port
 * (a8dev/projects/nova_defense: bit masks, struct-of-arrays, tables).
 *
 * The rules run in world pixels: 320 across, as the original, shown at half
 * size on the 160-pixel LCD. Vertically the world is 204 high (2 world
 * pixels a display line; the original's screen was 256), so the layout is
 * squeezed: the comet, the swarm, the shields, the gun and the status line
 * are closer together. Distances that matter to play keep the original's
 * timing: the swarm needs the same number of drops (12) to reach the
 * shields and (17) to reach the gun, and bullets take as many frames as the
 * original's to cross that space. Changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define SCREEN_W 320
#define WORLD_H  204            /* port: the original's screen was 256 high */

/* Alien swarm: staggered rows, odd rows shifted right */
#define ALIEN_COLS 8
#define ALIEN_ROWS 6
#define ALIEN_W 14
#define ALIEN_H 10
#define ALIEN_CELL_W 26
#define ALIEN_CELL_H 14         /* port: 15 (7 display lines) */
#define ALIEN_SHIFT 12          /* port: 13 (half a cell): even, so it is whole pixels at half size */
#define ALIEN_ROW_SHIFT(r) (((r) & 1) ? ALIEN_SHIFT : 0)
#define ALIEN_GRID_W (ALIEN_COLS * ALIEN_CELL_W + ALIEN_SHIFT)
#define ALIEN_START_X ((SCREEN_W - ALIEN_GRID_W) / 2)
#define ALIEN_START_Y 22        /* port: 46 */
#define ALIEN_DROP 4            /* port: 6; with the squeezed screen, the same number of drops */

#define ALIEN_TYPE_C 0          /* rows 0-1: eye pods, 30 points */
#define ALIEN_TYPE_B 1          /* rows 2-3: rays, 20 */
#define ALIEN_TYPE_A 2          /* rows 4-5: spore crawlers, 10 */

/* Shields: three crystal hexagons; their pixels are kept at half size
   (port: a pixel is 2 x 2 world pixels, as it is shown) */
#define SHIELD_COUNT 3
#define SHIELD_W 32
#define SHIELD_H 14
#define SHIELD_PW 16            /* pixels at half size */
#define SHIELD_PH 7
#define SHIELD_Y 150            /* port: 200 */

/* Player */
#define PLAYER_W 15
#define PLAYER_H 8
#define PLAYER_Y 170            /* port: 232 */
#define PLAYER_SPEED 3
#define PLAYER_MIN_X 8
#define PLAYER_MAX_X (SCREEN_W - 8 - PLAYER_W)

#define GROUND_Y 184            /* the status line; port: alien bullets end here (was the screen's bottom) */

/* Bullets. port: 10 and 3 world pixels a frame; the shorter screen with
   the same flight times */
#define MAX_ALIEN_BULLETS 3
#define PLAYER_BULLET_SPEED 8
#define ALIEN_BULLET_SPEED 2
#define BULLET_TOP (UFO_Y - 6)  /* the original's 30: 6 above the comet */

/* UFO (the comet) */
#define UFO_Y 10                /* port: 36 */
#define UFO_W 16
#define UFO_H 8                 /* port: 7 (4 display lines) */
#define UFO_SPEED 1

#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_DYING      2
#define STATE_GAMEOVER   3
#define STATE_WAVE_CLEAR 4

#define MAX_EXPLOSIONS 8

/* Colours (pen numbers). port: the Lynx's pen 0 is transparent, so the
   background is pen 1 and white moves to pen 2 (the original's unused
   green); the rest keep the original's numbers. */
#define COL_BG       1
#define COL_WHITE    2
#define COL_RED      4
#define COL_ORANGE   5
#define COL_CYAN     6
#define COL_MAGENTA  7
#define COL_YELLOW   8
#define COL_LTBLUE   10
#define COL_BRGREEN  12
#define COL_GRAY     13
#define COL_DKORANGE 14

/* port (speed): alive[][] as a bit mask per row (bit c = column c) */
extern uint8_t alive_mask[ALIEN_ROWS];
extern int16_t grid_x, grid_y;
extern int8_t swarm_dir;
extern uint8_t move_timer, move_delay, anim_frame, alive_count, fire_timer, fire_delay;

extern int16_t player_x;
extern int8_t lives;
extern uint32_t score, hiscore;
extern char score_str[7], hi_str[7];    /* the same, as 6 decimal digits */
extern uint8_t wave, state;
extern int16_t state_timer;
extern uint8_t title_blink;

/* bullets: [0] the player's, [1..3] the aliens' */
extern int16_t bul_x[1 + MAX_ALIEN_BULLETS], bul_y[1 + MAX_ALIEN_BULLETS];
extern uint8_t bul_active[1 + MAX_ALIEN_BULLETS];

extern int16_t ufo_x;
extern int8_t ufo_dir;
extern uint8_t ufo_active;
extern int16_t ufo_timer, ufo_score_val;

extern uint8_t shield_px[SHIELD_COUNT][SHIELD_PH][SHIELD_PW];
extern int16_t shield_x[SHIELD_COUNT];
extern uint8_t shield_dirty;            /* bit per shield changed (main.c redraws its sprite) */

extern int16_t ex_x[MAX_EXPLOSIONS], ex_y[MAX_EXPLOSIONS];
extern uint8_t ex_timer[MAX_EXPLOSIONS], ex_active[MAX_EXPLOSIONS], ex_color[MAX_EXPLOSIONS];

/* sound events, for the main loop */
extern uint8_t ev_shoot, ev_alien_hit, ev_player_hit, ev_ufo_hit, ev_march;
extern uint8_t march_note;
extern uint8_t hit_row;                 /* the row of the last alien hit */

void game_init(void);
void game_init_wave(void);
void game_update(uint8_t left, uint8_t right, uint8_t fire_pressed);

/* port (speed): column and row offsets from tables */
extern const uint8_t col_x[ALIEN_COLS + 1];     /* c * ALIEN_CELL_W */
extern const uint8_t row_y[ALIEN_ROWS];         /* r * ALIEN_CELL_H */
#define ALIEN_X(r, c) (grid_x + col_x[c] + ALIEN_ROW_SHIFT(r))
#define ALIEN_Y(r)    (grid_y + row_y[r])
#endif
