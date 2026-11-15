/* Bullion Dash - the original's music and effects on Mikey (sound.c) */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>

void sound_init(void);
/* SFX_* (game.h): takes channel 3 from the drums for a few frames */
void sound_sfx(uint8_t which);
/* once a frame: the effect, the music (a row every 6 frames), decays */
void sound_tick(void);
extern uint8_t music_row;

#endif
