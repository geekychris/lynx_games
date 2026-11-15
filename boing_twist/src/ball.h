/* ball.c: the texture, the picture, the moves' animations, the sprite */
#ifndef BT_BALL_H
#define BT_BALL_H
#include <stdint.h>
#include "lynxgame.h"
#include "bt_core.h"
#include "gfx_data.h"

extern uint8_t texl[], pic_buf[];

/* While playing the screen is single-buffered and the second buffer holds
   play's data. Of $E018-$FFF7 the CPU sees RAM at $E018-$FBFF and
   $FE00-$FFF7 ($FC00-$FDFF are Suzy's and Mikey's registers; the display
   reads the RAM under them).
     $E018  the flip's tables (cart file FLIP_FILE, cart/flip.bin), 3289 bytes
     $ED00  1 KB shared by the flip's polar copy of the half (ballasm.s
            FL_P), a twisting band's rows and bt_start_level's signatures
            (bt_core.c): never needed at once
     $F100  the BtPuzzle (2810 bytes, BT_HIST 760)
     $FE00  the move queue (128 + 256 bytes) */
#define HI_FLIP    ((uint8_t *)0xE018)
#define HI_POLAR   0xED00
#define HI_TWSAVE  ((uint8_t *)0xED00)
#define HI_PUZZLE  0xF100
#define HI_QKIND   ((uint8_t *)0xFE00)
#define HI_QMOVE   ((int16_t *)0xFE80)
extern uint8_t ball_pic_colours;
extern SCB_REHV_PAL ball_scb;

void ball_init(void);
void ball_picture(uint8_t pic, uint8_t bands, uint8_t segs);
void ball_signatures(BtPuzzle *p);
void ball_texture(const BtPuzzle *p, uint8_t ident);
void ball_texture_rows(const BtPuzzle *p, uint8_t v0, uint8_t v1, uint8_t ident);
void ball_texture_half(const BtPuzzle *p, uint8_t k);
void ball_band_rows(uint8_t band, uint8_t *v0, uint8_t *v1);
void ball_twist_begin(uint8_t band);
void ball_twist_shift(int8_t texels);
uint8_t ball_half_turn(uint8_t k);
uint8_t ball_cur_segs(void);
void ball_palette(const uint16_t *col);
void ball_draw(int16_t cx, int16_t cy, uint16_t scale);

/* the ball at turn t into ball_spr (ballasm.s, tables in view.s) */
extern uint8_t ball_spr[];
void __fastcall__ view_play(uint8_t turn);

/* ballasm.s */
typedef struct { uint8_t v0, v1, ident; const uint8_t *slots; uint8_t s0, s1; } TxArgs;
extern TxArgs tx_args;
void tex_rows(void);                      /* texture rows v0..v1-1 from the slots (ball.c's tables) */
typedef struct { const uint8_t *src; uint8_t v0, n, shift; } TwArgs;
extern TwArgs tw_args;
void band_rotate(void);                   /* tw_args.n rows from v0: texture = src rotated by shift */
extern uint8_t fl_turn, fl_ang;
void flip_begin(void);                    /* the half facing (turn fl_turn) into fl_p, round the flip axis */
void flip_frame(void);                    /* the half turned fl_ang (64 = a full turn) into the texture */
#endif
