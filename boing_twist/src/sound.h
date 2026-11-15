/* sound.c: the original's four sounds on Mikey */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
enum { SFX_THUD, SFX_CLICK, SFX_FLIP, SFX_SOLVED };
void sfx(uint8_t which);
#endif
