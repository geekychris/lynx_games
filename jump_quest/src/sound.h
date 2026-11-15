#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>

extern uint8_t music_playing;
void sound_music_start(void);
void sound_music_stop(void);
void sound_music_tick(void);
void sound_jump(void);
void sound_stomp(void);
void sound_collect(void);
void sound_powerup(void);
void sound_hurt(void);
void sound_die(void);
void sound_levelwin(void);
/* once a frame: effect sequences, decays and sweeps */
void sound_tick(void);
void sound_all_off(void);

#endif
