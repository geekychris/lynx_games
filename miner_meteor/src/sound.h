/* Miner Meteor - sound on Mikey (sound.c). */
#ifndef MM_SOUND_H
#define MM_SOUND_H
#include <stdint.h>

void sound_init(void);
void sound_jump(void);
void sound_key(void);
void sound_death(void);
void sound_levelwin(void);
void sound_gameover(void);
void sound_win(void);
void sound_all_off(void);
void sound_tick(void);          /* once a frame */

#endif
