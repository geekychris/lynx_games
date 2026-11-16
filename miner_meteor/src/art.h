/* art.h: made by tools/mkart.py - don't edit, re-run it. */
#ifndef ART_H
#define ART_H
#include <stdint.h>

/* 8 x 7 pictures, 4 bpp literal; tiles: pen 0 where the original is black */
extern const unsigned char art_empty[43];
extern const unsigned char art_solid_a[43];
extern const unsigned char art_solid_b[43];
extern const unsigned char art_collapse[43];
extern const unsigned char art_conv_l[43];
extern const unsigned char art_conv_r[43];
extern const unsigned char art_hazard[43];
extern const unsigned char art_key[43];
extern const unsigned char art_exit_locked[43];
extern const unsigned char art_exit_open[43];
extern const unsigned char art_collapse_dim[43];
extern const unsigned char art_player_r0[43];
extern const unsigned char art_player_r1[43];
extern const unsigned char art_player_r2[43];
extern const unsigned char art_player_r3[43];
extern const unsigned char art_player_r4[43];
extern const unsigned char art_player_r5[43];
extern const unsigned char art_player_r6[43];
extern const unsigned char art_player_r7[43];
extern const unsigned char art_player_l0[43];
extern const unsigned char art_player_l1[43];
extern const unsigned char art_player_l2[43];
extern const unsigned char art_player_l3[43];
extern const unsigned char art_player_l4[43];
extern const unsigned char art_player_l5[43];
extern const unsigned char art_player_l6[43];
extern const unsigned char art_player_l7[43];
extern const unsigned char art_player_jump[43];
extern const unsigned char art_player_fall[43];
extern const unsigned char art_guard_a0[43];
extern const unsigned char art_guard_a1[43];
extern const unsigned char art_guard_a2[43];
extern const unsigned char art_guard_a3[43];
extern const unsigned char art_guard_a4[43];
extern const unsigned char art_guard_a5[43];
extern const unsigned char art_guard_a6[43];
extern const unsigned char art_guard_a7[43];
extern const unsigned char art_guard_b0[43];
extern const unsigned char art_guard_b1[43];
extern const unsigned char art_guard_b2[43];
extern const unsigned char art_guard_b3[43];
extern const unsigned char art_guard_b4[43];
extern const unsigned char art_guard_b5[43];
extern const unsigned char art_guard_b6[43];
extern const unsigned char art_guard_b7[43];
extern const unsigned char art_guard_c0[43];
extern const unsigned char art_guard_c1[43];
extern const unsigned char art_guard_c2[43];
extern const unsigned char art_guard_c3[43];
extern const unsigned char art_guard_c4[43];
extern const unsigned char art_guard_c5[43];
extern const unsigned char art_guard_c6[43];
extern const unsigned char art_guard_c7[43];
extern const unsigned char art_black[43];
extern const unsigned char * const art_player[2][8];   /* [facing][walk frame] */
extern const unsigned char * const art_guard[3][8];    /* [colour 4/5/6][frame] */
extern const uint16_t art_palette[16];                 /* 0xRGB: the original's, 13 = black */
#define COL_BLACK2 13

#endif
