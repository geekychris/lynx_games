/* Orb Hunter - sound (sound.c): the tune and the effects on Mikey. The
   effects (sfx_*) are declared in game.h, as the original's. */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void sound_init(void);
void sound_tick(void);              /* once a 50 Hz tick */
void sound_pause(uint8_t on);
#endif
