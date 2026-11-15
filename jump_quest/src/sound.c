/*
 * Jump Quest (Lynx) - sound: the original's tune and effects on Mikey.
 *
 * The tune is the original's (sound.c): two four-pattern voices, melody and
 * bass, a row every 3 ticks in the song order 0 0 1 2 0 1 2 3, on Mikey
 * channels 0 and 1 (Paula channels 0 and 1 there). The notes are the
 * original's Paula periods (a 32-sample cycle: 3546895 / (period * 32) Hz)
 * turned into Mikey timer values at compile time (SND_PERIOD).
 *
 * The effects: the original computed a sample for each (sound.c) and played
 * it on Paula channel 2 (3 for collect). port: here they are Mikey tones
 * and noise of the same shape: the jump's rising blip, the stomp's thud and
 * falling tone, collect's rising sparkle, the power-up's rising fanfare, the
 * hurt's descending buzz, the long sad fall of a death, the level's fanfare.
 * The original never stopped its samples (Paula repeated them until the
 * next one); these end.
 */
#include "lynxgame.h"
#include "sound.h"

#pragma static-locals (on)

/* ------------------------------------------------------------ the tune */

#define AMIGA_HZ(per) (3546895UL / ((unsigned long)(per) * 32UL))
#define NP(per) SND_PERIOD(AMIGA_HZ(per))

/* the original's note_periods[1..34] (C-2 .. A-4) */
static const uint16_t note_period[35] = {
    0,
    NP(856), NP(808), NP(762), NP(720), NP(678), NP(640), NP(604), NP(570), NP(538), NP(508),
    NP(480), NP(453), NP(428), NP(404), NP(381), NP(360), NP(339), NP(320), NP(302), NP(285),
    NP(269), NP(254), NP(240), NP(226), NP(214), NP(202), NP(190), NP(180), NP(170), NP(160),
    NP(151), NP(143), NP(135), NP(127),
};

#define PATTERN_LEN 64
#define NUM_PATTERNS 4
#define SONG_LEN 8

static const uint8_t melody[NUM_PATTERNS][PATTERN_LEN] = {
    { 13, 0,17, 0,20, 0,25, 0, 20, 0,17, 0,13, 0, 0, 0,
      15, 0,18, 0,22, 0,27, 0, 22, 0,18, 0,15, 0, 0, 0,
      17, 0,20, 0,25, 0,29, 0, 25, 0,20, 0,17, 0, 0, 0,
      13, 0,17, 0,20, 0,25, 0, 20, 0,17, 0,13, 0, 0, 0 },
    { 25, 0,22, 0,20, 0,17, 0, 13, 0,17, 0,20, 0, 0, 0,
      27, 0,25, 0,22, 0,20, 0, 17, 0,20, 0,22, 0, 0, 0,
      29, 0,25, 0,22, 0,20, 0, 17, 0,13, 0,17, 0, 0, 0,
      25, 0,22, 0,20, 0,17, 0, 20, 0,22, 0,25, 0, 0, 0 },
    { 20, 0, 0,20, 0, 0,22, 0, 0,22, 0, 0,25, 0, 0, 0,
      20, 0, 0,20, 0, 0,22, 0, 0,22, 0, 0,27, 0, 0, 0,
      17, 0, 0,17, 0, 0,20, 0, 0,20, 0, 0,22, 0, 0, 0,
      17, 0, 0,17, 0, 0,20, 0, 0,22, 0, 0,25, 0, 0, 0 },
    { 13, 0,17, 0,20, 0,25, 0, 20, 0,17, 0,13, 0, 0, 0,
      15, 0,18, 0,22, 0,27, 0, 22, 0,18, 0,15, 0, 0, 0,
      13, 0,17, 0,20, 0,25, 0, 29, 0,25, 0,20, 0,17, 0,
      13, 0, 0, 0,17, 0, 0, 0, 20, 0, 0, 0,25, 0, 0, 0 },
};

static const uint8_t bassline[NUM_PATTERNS][PATTERN_LEN] = {
    { 1, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0,
      3, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0, 6, 0, 0, 0,
      5, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0,
      1, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0 },
    { 1, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0,
      3, 0, 0, 0, 6, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0,
      5, 0, 0, 0, 8, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0,
      1, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0, 5, 0, 0, 0 },
    { 8, 0, 0, 0, 8, 0, 0, 0, 10, 0, 0, 0, 10, 0, 0, 0,
      8, 0, 0, 0, 8, 0, 0, 0, 10, 0, 0, 0, 12, 0, 0, 0,
      5, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0,
      5, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0, 10, 0, 0, 0 },
    { 1, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0,
      3, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0, 6, 0, 0, 0,
      1, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0,
      1, 0, 0, 0, 5, 0, 0, 0, 8, 0, 0, 0,12, 0, 0, 0 },
};

static const uint8_t song_order[SONG_LEN] = { 0, 0, 1, 2, 0, 1, 2, 3 };

uint8_t music_playing;
static uint8_t song_pos, pattern_row, tick_count;
#define SPEED 3                       /* ticks a row */
/* the original's volumes 40 and 48 of 64 */
#define VOL_MELODY 34
#define VOL_BASS   40

static void voice(uint8_t ch, uint8_t note, uint8_t vol)
{
    uint16_t p = note_period[note];
    LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(ch, LYNX_AUD_FEED) = 0x01;         /* square wave */
    LYNX_AUD(ch, LYNX_AUD_SHIFT) = 0;
    LYNX_AUD(ch, LYNX_AUD_CTLB) = 0;
    LYNX_AUD(ch, LYNX_AUD_VOL) = vol;
    LYNX_AUD(ch, LYNX_AUD_BKUP) = (uint8_t)p;
    LYNX_AUD(ch, LYNX_AUD_CTLA) = (uint8_t)(0x18 | (p >> 8));
}

void sound_music_stop(void)
{
    music_playing = 0;
    LYNX_AUD(0, LYNX_AUD_VOL) = 0;
    LYNX_AUD(1, LYNX_AUD_VOL) = 0;
    LYNX_AUD(0, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(1, LYNX_AUD_CTLA) = 0;
}

void sound_music_start(void)
{
    song_pos = 0;
    pattern_row = 0;
    music_playing = 1;
    tick_count = SPEED - 1;                 /* the next tick plays row 0 */
    sound_music_tick();
}

void sound_music_tick(void)
{
    uint8_t pat, note;
    if (!music_playing) return;
    if (++tick_count < SPEED) return;
    tick_count = 0;
    pat = song_order[song_pos];
    note = melody[pat][pattern_row];
    if (note) voice(0, note, VOL_MELODY);
    else LYNX_AUD(0, LYNX_AUD_VOL) = 0;
    note = bassline[pat][pattern_row];
    if (note) voice(1, note, VOL_BASS);
    else LYNX_AUD(1, LYNX_AUD_VOL) = 0;
    if (++pattern_row >= PATTERN_LEN) {
        pattern_row = 0;
        if (++song_pos >= SONG_LEN) song_pos = 0;
    }
}

/* ------------------------------------------------------------ effects */

/* a few effects are sequences: (Hz, frames) steps on channel 2 */
static const uint16_t *seq;
static uint8_t seq_left, seq_vol;

static const uint16_t seq_powerup[] = { 523, 3, 659, 3, 784, 3, 1047, 6, 0 };
static const uint16_t seq_level[] = { 523, 6, 659, 6, 784, 6, 1047, 6, 784, 4, 1047, 16, 0 };

static void seq_start(const uint16_t *s, uint8_t vol)
{
    seq = s;
    seq_vol = vol;
    seq_left = 0;
}

void sound_jump(void)    { seq = 0; snd_tone(2, 700, 60, 5); snd_sweep(2, 90); }
void sound_stomp(void)   { seq = 0; snd_noise(3, 3, 70, 12); snd_tone(2, 500, 60, 4); snd_sweep(2, -25); }
void sound_collect(void) { snd_tone(3, 1300, 50, 4); snd_sweep(3, 120); }
void sound_powerup(void) { seq_start(seq_powerup, 56); }
void sound_hurt(void)    { seq = 0; snd_tone(2, 900, 64, 3); snd_sweep(2, -40); }
void sound_die(void)     { seq = 0; snd_tone(2, 600, 70, 1); snd_sweep(2, -6); }
void sound_levelwin(void){ seq_start(seq_level, 56); }

void sound_tick(void)
{
    if (seq && !seq_left) {
        if (!seq[0]) { seq = 0; snd_off(2); }
        else {
            snd_tone(2, seq[0], seq_vol, 0);
            seq_left = (uint8_t)seq[1];
            seq += 2;
        }
    }
    if (seq_left) --seq_left;
    snd_tick();
}

void sound_all_off(void)
{
    seq = 0;
    snd_all_off();
    sound_music_stop();
}
