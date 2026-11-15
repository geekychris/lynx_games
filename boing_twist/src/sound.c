/*
 * sound.c - Boing Twist's sounds on Mikey, after the original's synthesis
 * (sound_ahi.c): the Boing ball's thud (a falling low tone and a knock), a
 * click for a twist, a whoosh for a flip, a little chord for a solved ball.
 * port: the original synthesises samples for AHI; these are Mikey's square
 * waves and noise of the same shape. Like the original, no music.
 */
#include "lynxgame.h"
#include "sound.h"

#pragma warn (const-comparison, off)     /* SND_PERIOD is worked out by the compiler */

void sfx(uint8_t which)
{
    switch (which) {
    case SFX_THUD:                        /* 95 Hz falling, and a knock */
        snd_tone(0, 95, 90, 6);
        snd_sweep(0, -3);
        snd_noise(1, 6, 60, 12);
        break;
    case SFX_CLICK:
        snd_tone_p(2, SND_PERIOD(1400), 60, 15);
        break;
    case SFX_FLIP:                        /* a whoosh: noise, fading */
        snd_noise(1, 1, 70, 3);
        snd_tone(2, 300, 30, 2);
        snd_sweep(2, -6);
        break;
    case SFX_SOLVED:                      /* C E G */
        snd_tone_p(1, SND_PERIOD(523), 70, 2);
        snd_tone_p(2, SND_PERIOD(659), 70, 2);
        snd_tone_p(3, SND_PERIOD(784), 70, 2);
        break;
    }
}
