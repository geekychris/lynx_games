/*
 * Orb Hunter - sound on Mikey. Replaces the original's ptplayer: its tune
 * (rock_blaster.mod, which the original loads; music.c is the Rock Blaster
 * port's conversion of the same MOD) on channels 2 and 3, as the original
 * keeps two channels for music (mt_MusicChannels = 2) and two for effects.
 *
 * port: the original's eight sampled effects (main.c build_sfx) as Mikey
 * tones and noise with the same shapes and priorities: tones on channel 0
 * (a falling blip for a shot, a rising one for a jump, the item's three
 * rising notes, the door's falling sweep, a tick for a hit), noise on
 * channel 1 (explosions, the hunter hit, the rocket's thump). A new effect
 * replaces a playing one on its channel only with the same or a higher
 * priority (ptplayer's sfx_pri).
 */
#pragma static-locals (on)
#include "lynxgame.h"
#include "game.h"
#include "music.h"
#include "sound.h"

/* ------------------------------------------------------------ channel 0: tones */

/* a square wave of 1 MHz / 2^sel / (2 * (bkup + 1)) Hz whose timer value
   moves by `step` a frame (up: the pitch falls) */
static uint8_t t_bkup, t_sel, t_vol, t_decay, t_pri, t_left;
static int8_t t_step;
/* the item's notes: (bkup) after the first, 3 frames each */
static const uint8_t *t_seq;

static void tone(uint8_t bkup, uint8_t sel, uint8_t vol, uint8_t decay, int8_t step, uint8_t pri)
{
    if (t_vol && pri < t_pri) return;
    LYNX_AUD(0, LYNX_AUD_CTLA) = 0;
    LYNX_AUD(0, LYNX_AUD_FEED) = 0x01;
    LYNX_AUD(0, LYNX_AUD_SHIFT) = 0;
    LYNX_AUD(0, LYNX_AUD_CTLB) = 0;
    LYNX_AUD(0, LYNX_AUD_VOL) = t_vol = vol;
    LYNX_AUD(0, LYNX_AUD_BKUP) = t_bkup = bkup;
    LYNX_AUD(0, LYNX_AUD_CTLA) = (uint8_t)(0x18 | sel);
    t_sel = sel;
    t_decay = decay;
    t_step = step;
    t_pri = pri;
    t_seq = 0;
    t_left = 0;
}

static void tone_off(void)
{
    LYNX_AUD(0, LYNX_AUD_VOL) = 0;
    LYNX_AUD(0, LYNX_AUD_CTLA) = 0;
    t_vol = 0;
    t_seq = 0;
}

/* ------------------------------------------------------------ channel 1: noise */

static uint8_t n_pri;

static void noise(uint8_t pitch, uint8_t vol, uint8_t decay, uint8_t pri)
{
    if ((snd_active() & 2) && pri < n_pri) return;
    snd_noise(1, pitch, vol, decay);
    n_pri = pri;
}

/* ------------------------------------------------------------ the effects (main.c sfx_*) */

/* shoot: a quick high blip, falling (~1900 -> 1150 Hz) */
void sfx_shoot(void)      { tone(131, 1, 50, 16, 28, 30); }
/* jump: a quick rising tone (~875 -> 2600 Hz) */
void sfx_jump(void)       { tone(142, 2, 40, 10, -24, 20); }
/* enemy hit: a mid tick */
void sfx_hit(void)        { tone(131, 2, 48, 16, 0, 25); }
/* door open: a falling sweep (~1500 -> 500 Hz) */
void sfx_door(void)       { tone(82, 2, 50, 6, 21, 35); }
/* item: three rising notes (~1400, 1750, 2100 Hz) */
static const uint8_t item_notes[3] = { 142, 118, 0 };
void sfx_powerup(void)
{
    tone(178, 1, 64, 0, 0, 60);
    if (t_pri == 60) {
        t_seq = item_notes;
        t_left = 3;
    }
}
/* missile: a bass thump with noise */
void sfx_missile(void)
{
    tone(207, 4, 60, 6, 4, 40);
    noise(5, 40, 8, 40);
}
/* bomb / a creature / the hunter blown up: noise, decaying */
void sfx_explode(void)    { noise(6, 110, 5, 50); }
/* the hunter hit: a low noise burst */
void sfx_player_hit(void) { noise(7, 100, 10, 55); }

/* ------------------------------------------------------------ the tune */

static const uint8_t *mu_ptr[2];
static uint8_t mu_left[2], mu_vol[2];
static const uint8_t mu_vol0[2] = { 26, 30 };       /* lead, bass */
static uint8_t mu_on;

static void music_reset(void)
{
    mu_ptr[0] = music_lead;
    mu_ptr[1] = music_bass;
    mu_left[0] = mu_left[1] = 1;
}

static void music_silence(void)
{
    LYNX_AUD(2, LYNX_AUD_VOL) = 0;
    LYNX_AUD(3, LYNX_AUD_VOL) = 0;
    mu_vol[0] = mu_vol[1] = 0;
}

/* once a 50 Hz tick (the MOD's tick): the next note when one ends, and a
   short decay to a sustain level (as the Rock Blaster port) */
static void music_tick(void)
{
    static uint8_t c, n, ch;
    for (c = 0; c < 2; ++c) {
        ch = c + 2;
        if (--mu_left[c] == 0) {
            if (mu_ptr[c][1] == 0) { music_reset(); return; }   /* the end: again */
            n = mu_ptr[c][0];
            mu_left[c] = mu_ptr[c][1];
            mu_ptr[c] += 2;
            if (!n) { LYNX_AUD(ch, LYNX_AUD_VOL) = mu_vol[c] = 0; continue; }
            --n;
            LYNX_AUD(ch, LYNX_AUD_CTLA) = 0;
            LYNX_AUD(ch, LYNX_AUD_FEED) = 0x01;
            LYNX_AUD(ch, LYNX_AUD_SHIFT) = 0;
            LYNX_AUD(ch, LYNX_AUD_CTLB) = 0;
            LYNX_AUD(ch, LYNX_AUD_BKUP) = note_bkup[n];
            LYNX_AUD(ch, LYNX_AUD_CTLA) = (uint8_t)(0x18 | note_sel[n]);
            LYNX_AUD(ch, LYNX_AUD_VOL) = mu_vol[c] = mu_vol0[c];
        } else if (mu_vol[c] > mu_vol0[c] / 2) {
            LYNX_AUD(ch, LYNX_AUD_VOL) = --mu_vol[c];
        }
    }
}

/* ------------------------------------------------------------ */

void sound_init(void)
{
    snd_init();
    music_reset();
    mu_on = 1;
}

void sound_tick(void)
{
    snd_tick();
    if (t_vol) {
        if (t_seq) {
            if (--t_left == 0) {
                if (!*t_seq) {              /* the last note fades */
                    t_seq = 0;
                    t_decay = 8;
                } else {
                    LYNX_AUD(0, LYNX_AUD_BKUP) = t_bkup = *t_seq++;
                    t_left = 3;
                }
            }
        } else if (t_vol <= t_decay) {
            tone_off();
        } else {
            LYNX_AUD(0, LYNX_AUD_VOL) = t_vol -= t_decay;
            if (t_step) {
                if ((t_step > 0 && t_bkup > 255 - t_step) || (t_step < 0 && t_bkup < (uint8_t)-t_step)) {
                    tone_off();
                } else {
                    t_bkup += t_step;
                    LYNX_AUD(0, LYNX_AUD_BKUP) = t_bkup;
                }
            }
        }
    }
    if (mu_on) music_tick();
}

/* PAUSE: everything quiet; the tune goes on where it was */
void sound_pause(uint8_t on)
{
    mu_on = !on;
    if (on) {
        snd_all_off();
        tone_off();
        music_silence();
    }
}
