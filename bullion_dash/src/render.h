/*
 * Bullion Dash - the Lynx screen (replaces the original's render.c/gfx.c)
 *
 * The whole 28 x 16 grid is on screen at once: 5 x 6 pixel tiles make a
 * 140 x 96 playfield (x 10..149, y 0..95), the status line under it (y
 * 96..101), the original's dark blue side borders either side. Tiles and
 * people are Suzy sprites. The screen isn't cleared each frame: each screen
 * buffer keeps a list of the cells to repaint before it is drawn again (the
 * cells the people covered two frames ago, and the tiles that changed).
 */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

#define PF_X   10           /* playfield's left edge */
#define HUD_Y  96

void render_init(void);
/* the play screen from scratch (both buffers: the next two frames) */
void render_room(void);
/* a tile changed (level.c's tile_set) */
void render_tile_changed(uint8_t x, uint8_t y);
/* one frame of the play screen into the back buffer: repaint the cells,
   then the people (entities = 0: not drawn, the death blink), then the
   status line if it changed */
void render_frame(uint8_t entities);
/* the status line changed */
void render_status_changed(void);
/* a message box over the playfield (both buffers); 0 = no line */
void render_msg(const char *big, uint8_t big_col, const char *l1, const char *l2, uint8_t col2);
/* the title screen: render_title_new() when it is shown, then
   render_title() every frame */
void render_title_new(void);
void render_title(uint8_t blink, uint32_t hiscore);
/* gfx_present() and the buffer bookkeeping: call once a frame */
void render_present(void);
/* a 32-bit number as decimal digits (zero-padded to digits) */
char *render_num(uint32_t v, uint8_t digits);

#endif
