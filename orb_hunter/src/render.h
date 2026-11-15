/*
 * Orb Hunter - the Lynx screen (render.c): the original's draw.c redone
 * with Suzy sprites at x / 2, y * 3 / 8 (its 320 x 256 room, 20 x 16 tiles
 * of 16 x 16, as 160 x 96 under a 6-row HUD: tiles of 8 x 6).
 */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

void render_init(void);
/* the room was entered or something covered it: repaint it in full (both
   screen buffers) */
void render_room(void);
/* a tile of the current room changed (an opened door) */
void render_tile_changed(uint8_t tx, uint8_t ty);
/* one frame of play into the back buffer (the original's draw_frame) */
void render_play(void);
/* the item box over it (draw_item_get) */
void render_item_box(const char *name, uint8_t blink);
/* PAUSED over it */
void render_paused(void);
/* full screens: the title (draw_title) and GAME OVER (draw_gameover);
   render_screen_new() when one comes up */
void render_screen_new(void);
void render_title(uint8_t blink);
void render_gameover(uint8_t blink, const char *score);
/* show the back buffer at the next vertical blank */
void render_present(void);

/* counters for the perf log: cells repainted and objects drawn in the last
   frame, full repaints (a room entered) */
extern uint8_t rd_cells, rd_objs, rd_full;
#endif
