/* room.c: the room, the shadow, the HUD strips, the markers, the title's ball and logo */
#ifndef ROOM_H
#define ROOM_H
#include <stdint.h>

typedef struct { uint8_t on; int16_t cx, cy; uint16_t rx16, ry16; } RoomShadow;
extern RoomShadow room_shadow;            /* where room_draw puts the shadow */

void room_draw(uint8_t r);
void room_palette(uint8_t r);
void hud_strips(void);
void hud_strip(uint8_t bottom);
void panel(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t c);
void markers_save(uint8_t xl, uint8_t xr);
void markers_put(int16_t top, int16_t bot);
void title_ball_init(void);
void title_ball_palette(void);
void title_ball_draw(int16_t cx, int16_t cy, uint8_t ph);
void logo_draw(int16_t y, uint16_t scale, uint8_t c, uint8_t shade);
#endif
