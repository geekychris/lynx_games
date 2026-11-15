/* art.h: made by tools/mkart.py - don't edit, re-run it. */
#ifndef ART_H
#define ART_H
#include <stdint.h>

#define HERO_W 7
#define HERO_H_RJ 13
#define HERO_H_DALE 11
extern const unsigned char * const art_tiles[16];
extern const unsigned char * const art_hero[2][3];
extern const unsigned char * const art_enemy[4][2];
extern const unsigned char * const art_item[15];
extern const unsigned char * const art_dot;
/* runs of tiles 1, 2, 3, 5, 10 (ground, grass, brick, stone, platform) */
extern const unsigned char * const art_strip[5][3];
extern const unsigned char art_heart[13];
extern const uint16_t art_palette[16];   /* 0xRGB, the original's (gfx.c) */

#endif
