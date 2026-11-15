/*
 * Miner Meteor - sound. port: the original's audio.cpp is a stub that logs its
 * effect triggers (sfx_jump, sfx_key, sfx_death, sfx_levelwin) to the
 * bridge; audio.h plans chip-tune effects for them. Here they are short
 * Mikey jingles and noise, plus one for game over and one for the win.
 *   channel 0  jump       1  key, cavern cleared, game over, win (note lists)
 *   channel 2  death noise
 * Periods are compile-time constants (SND_PERIOD): no division at run time.
 */
#include "lynxsnd.h"
#include "sound.h"

#pragma static-locals (on)
#pragma warn (const-comparison, off)   /* SND_PERIOD's constant comparisons */

/* a note list: period, frames; per 0 = rest; {0, 0} ends */
typedef struct { uint16_t per; uint8_t len; } note;

static const note n_jump[] = { {SND_PERIOD(392), 2}, {SND_PERIOD(523), 2}, {SND_PERIOD(659), 3}, {0, 0} };
static const note n_key[] = { {SND_PERIOD(1319), 3}, {SND_PERIOD(1760), 6}, {0, 0} };
static const note n_level[] = {
    {SND_PERIOD(523), 5}, {SND_PERIOD(659), 5}, {SND_PERIOD(784), 5}, {SND_PERIOD(1047), 12}, {0, 0} };
static const note n_over[] = {
    {SND_PERIOD(392), 10}, {SND_PERIOD(349), 10}, {SND_PERIOD(311), 10}, {SND_PERIOD(262), 24}, {0, 0} };
static const note n_win[] = {
    {SND_PERIOD(523), 6}, {SND_PERIOD(659), 6}, {SND_PERIOD(784), 6}, {SND_PERIOD(1047), 8}, {1, 4},
    {SND_PERIOD(784), 5}, {SND_PERIOD(1047), 20}, {0, 0} };

static const note *seq[2];      /* channels 0 and 1 */
static uint8_t left[2], vols[2];

static void play(uint8_t ch, const note *n, uint8_t vol)
{
    seq[ch] = n;
    left[ch] = 0;
    vols[ch] = vol;
}

void sound_init(void) { snd_init(); }
void sound_jump(void) { play(0, n_jump, 40); }
void sound_key(void) { play(1, n_key, 60); }
void sound_levelwin(void) { play(1, n_level, 60); }
void sound_gameover(void) { play(1, n_over, 60); }
void sound_win(void) { play(1, n_win, 60); }
void sound_death(void) { snd_noise(2, 5, 100, 4); }

void sound_all_off(void)
{
    seq[0] = seq[1] = 0;
    snd_all_off();
}

void sound_tick(void)
{
    uint8_t ch;
    const note *n;
    for (ch = 0; ch < 2; ++ch) {
        n = seq[ch];
        if (!n) continue;
        if (left[ch]) { --left[ch]; continue; }
        if (!n->len) { snd_off(ch); seq[ch] = 0; continue; }
        if (n->per > 1) snd_tone_p(ch, n->per, vols[ch], 0);
        else snd_off(ch);
        left[ch] = n->len - 1;
        seq[ch] = n + 1;
    }
    snd_tick();
}
