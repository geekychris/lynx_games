/* made by tools/gen_data.py - do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>

#define TEX_W 64
#define TEX_H 32
#define TEX_ROW 96
#define NR 16
#define NA 64
#define FLIP_FILE_SIZE 3289
#define FLIP_ROWS_AT 2048
#define BALL_X0 -32   /* the ball sprite's top-left, from the ball's middle */
#define BALL_Y0 -32
#define BALL_W 64
#define BALL_H 64
#define BALL_R 32
#define SCR_CX 80
#define SCR_CY 51
#define HUD_TOP 7
#define HUD_BOT 95
#define FOCAL_X16 1771   /* pixels per unit at distance 1, x16 */
#define FLOOR_Y 77   /* the back wall meets the floor */

typedef struct { const char *title; uint8_t level, room; uint16_t col[3]; } Pic;
#define NPICS 18   /* 0: the Boing ball, made by the game */
extern const Pic pics[NPICS];
typedef struct { const char *name; uint16_t pal[7]; uint8_t wall; } Room;
/* each room's shapes are a cartridge file (ROOM_FILE + n), read into room_buf */
#define ROOM_BUF_SIZE 609
#define FLIP_FILE NPICS
#define ROOM_FILE (NPICS + 1)
#define TITLE_FILE (NPICS + 1 + NROOMS)
#define TITLE_SIZE 1582
#define NROOMS 10
extern const Room rooms[NROOMS];
/* the bands' rows (from the ball sprite's top) down its middle, per grid (4, 6, 8 bands) */
extern const uint8_t band_top[3][8], band_bot[3][8];
/* the title's Boing ball: sub-sprites (palette-cycled, the light in zones) */
#define TB_N 46
#define TB_LEVELS 5
#define TB_PHASES 14
#define TB_X0 -26
#define TB_Y0 -26
#define TB_W 52
#define TB_H 52
/* the sub-sprites' data is a cartridge file (TITLE_FILE), read into the texture's place */
typedef struct { int8_t dx, dy; uint8_t level; uint16_t off; } TbSub;
extern const TbSub tb_subs[TB_N];   /* sorted by level */
extern const uint16_t tb_red[TB_LEVELS], tb_white[TB_LEVELS];
/* the title's logo (2 bpp) and the shadow's disc (1 bpp, SHADOW_N square) */
#define LOGO_W 65
#define LOGO_H 8
extern const uint8_t logo_data[];
#define SHADOW_N 32
extern const uint8_t shadow_data[];
/* shadows: on the wall, (x + 0.85, y - 0.15) on the back wall, radius x16; on the floor,
   the half-axes x16 of the ellipse for a ball 4.4 away and its middle's row */
#define WALL_SH_DX16 232
#define WALL_SH_DY16 41
#define WALL_SH_R16 313
#define FLOOR_SH_RX16 402
#define FLOOR_SH_RY16 104
#define FLOOR_SH_Y 88
#define BALL_R_TITLE 26

#endif
