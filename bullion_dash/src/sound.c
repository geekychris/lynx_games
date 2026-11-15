/*
 * Bullion Dash - music and sound effects on Mikey (the original's modplay.c
 * and sound.c).
 *
 * The music is the original's: its six 16-row patterns in the song order
 * 0 1 4 5 2 3 0 1, speed 6 (a row every 6 frames at 50 Hz), four channels:
 * bass, lead, arpeggio and drums. port: Paula played looping samples
 * (triangle bass, pulse lead, saw arpeggio); Mikey plays square waves at
 * the pitches those samples made (a 64-byte bass cycle: Paula's clock /
 * period / 64; lead and arpeggio / 32), held until the channel's next note,
 * as a looping sample is. The drum samples (kick, snare, hi-hat) become
 * noise bursts on channel 3. Volumes are the original's (0-64) on Mikey's
 * 0-127 scale (four square waves at full volume are harsh), the arpeggio
 * at 3/4.
 *
 * Effects take channel 3 for a few frames (the drums are skipped meanwhile),
 * as the original's modplay_sfx does. port: the original builds dig, gold,
 * death, level and trap samples but never plays them; this plays them (the
 * Spectrum and Atari 8-bit ports do too): each sample's notes are a frame
 * each, at the pitch it made.
 */
#pragma static-locals (on)
#include "lynxgame.h"
#include "game.h"
#include "sound.h"

/* Paula's PAL clock / (period * bytes a cycle) */
#define PAULA 3546895UL
#define NOTE(per) SND_PERIOD(PAULA / ((per) * 32UL))
#define BASS(per) SND_PERIOD(PAULA / ((per) * 64UL))
#define PERIODS(M) \
    M(428), M(404), M(381), M(360), M(339), M(320), M(302), M(285), M(269), M(254), M(240), M(226), \
    M(214), M(202), M(190), M(180), M(170), M(160), M(151), M(143), M(135), M(127), M(120), M(113), \
    M(107), M(95), M(85), M(80), M(71), M(64), M(57)
static const uint16_t note_p[31] = { PERIODS(NOTE) };
static const uint16_t bass_p[31] = { PERIODS(BASS) };

/* note names: the original's period table indexes */
enum { C_3, Cs3, D_3, Ds3, E_3, F_3, Fs3, G_3, Gs3, A_3, As3, B_3,
       C_4, Cs4, D_4, Ds4, E_4, F_4, Fs4, G_4, Gs4, A_4, As4, B_4,
       C_5, D_5, E_5, F_5, G_5, A_5, B_5 };
enum { KICK = 1, SNARE, HIHAT };

/* a pattern channel: (row << 8 | note) pairs + volume, ended by 0xFF */
typedef struct { uint8_t row, note, vol; } ev;
#define END { 0xFF, 0, 0 }

/* ---- the original's patterns (modplay.c build_pattern0..5) ---- */
static const ev p0_bass[] = { {0,C_3,56}, {4,Ds3,56}, {8,G_3,56}, {12,F_3,52}, END };
static const ev p0_lead[] = { {0,G_4,58}, {2,As4,58}, {4,C_5,60}, {6,Ds4,54}, {8,D_4,58}, {10,E_4,60},
                              {12,F_4,62}, {14,G_4,58}, END };
static const ev p0_arp[]  = { {0,C_4,40}, {1,Ds4,38}, {2,G_4,40}, {3,C_5,38}, {4,Ds4,40}, {5,G_4,38},
                              {6,C_5,40}, {7,G_4,38}, {8,G_3,40}, {9,B_3,38}, {10,D_4,40}, {11,G_4,38},
                              {12,F_3,40}, {13,A_3,38}, {14,C_4,40}, {15,F_4,38}, END };
static const ev p0_drum[] = { {0,KICK,60}, {2,HIHAT,36}, {4,SNARE,52}, {6,HIHAT,36}, {8,KICK,60},
                              {10,HIHAT,36}, {12,SNARE,52}, {14,HIHAT,36}, END };

static const ev p1_bass[] = { {0,Ds3,58}, {4,F_3,58}, {8,G_3,58}, {12,As3,54}, END };
static const ev p1_lead[] = { {0,Ds4,60}, {1,F_4,58}, {2,G_4,62}, {4,As4,64}, {5,G_4,58}, {6,F_4,60},
                              {8,G_4,62}, {9,As4,60}, {10,C_5,64}, {12,As4,58}, {13,G_4,56}, {14,F_4,54},
                              {15,Ds4,52}, END };
static const ev p1_arp[]  = { {0,Ds4,42}, {1,G_4,40}, {2,As4,42}, {3,Ds4,40}, {4,F_4,42}, {5,A_4,40},
                              {6,C_5,42}, {7,F_4,40}, {8,G_4,42}, {9,B_4,40}, {10,D_5,42}, {11,G_4,40},
                              {12,As3,42}, {13,D_4,40}, {14,F_4,42}, {15,As4,40}, END };
static const ev p1_drum[] = { {0,KICK,62}, {2,HIHAT,38}, {3,KICK,50}, {4,SNARE,54}, {6,HIHAT,38},
                              {8,KICK,62}, {10,HIHAT,38}, {12,SNARE,54}, {14,HIHAT,38}, {15,KICK,48}, END };

static const ev p2_bass[] = { {0,C_3,58}, {4,Cs3,58}, {8,D_3,58}, {12,Ds3,60}, END };
static const ev p2_lead[] = { {0,Ds4,56}, {2,D_4,54}, {4,E_4,58}, {6,Ds4,54}, {8,F_4,58}, {10,Fs4,60},
                              {12,G_4,62}, {14,Gs4,64}, END };
static const ev p2_arp[]  = { {0,C_4,38}, {1,Ds4,36}, {2,Fs4,38}, {3,C_4,36}, {4,Cs4,38}, {5,E_4,36},
                              {6,G_4,38}, {7,Cs4,36}, {8,D_4,38}, {9,F_4,36}, {10,A_4,38}, {11,D_4,36},
                              {12,Ds4,40}, {13,G_4,38}, {14,As4,40}, {15,Ds4,38}, END };
static const ev p2_drum[] = { {0,KICK,62}, {1,KICK,50}, {2,HIHAT,36}, {4,SNARE,56}, {6,HIHAT,36},
                              {8,KICK,62}, {9,KICK,50}, {10,HIHAT,36}, {12,SNARE,58}, {13,KICK,48},
                              {14,HIHAT,38}, END };

/* the breakdown: no lead or arpeggio notes (their last notes ring on, as
   the original's looping samples do) */
static const ev p3_bass[] = { {0,C_3,62}, {8,G_3,58}, {12,Ds3,60}, END };
static const ev p3_none[] = { END };
static const ev p3_drum[] = { {0,KICK,64}, {4,SNARE,58}, {6,HIHAT,32}, {8,KICK,64}, {10,HIHAT,32},
                              {12,SNARE,58}, {14,KICK,52}, END };

static const ev p4_lead[] = { {0,C_5,60}, {2,As4,58}, {4,G_4,56}, {6,F_4,58}, {8,Ds4,56}, {10,F_4,58},
                              {12,G_4,62}, {14,As4,60}, END };

static const ev p5_bass[] = { {0,Ds3,58}, {4,F_3,58}, {8,G_3,58}, {12,C_3,60}, END };
static const ev p5_lead[] = { {0,Ds4,58}, {1,F_4,58}, {2,G_4,60}, {3,As4,60}, {4,C_5,64}, {6,As4,58},
                              {8,G_4,60}, {10,As4,62}, {12,C_5,64}, {14,G_4,56}, END };
static const ev p5_arp[]  = { {0,Ds4,42}, {1,G_4,40}, {2,As4,42}, {3,Ds4,40}, {4,F_4,42}, {5,A_4,40},
                              {6,C_5,42}, {7,F_4,40}, {8,C_4,42}, {9,Ds4,40}, {10,G_4,42}, {11,C_5,40},
                              {12,C_4,42}, {13,G_4,40}, {14,Ds4,42}, {15,C_4,40}, END };
static const ev p5_drum[] = { {0,KICK,62}, {2,HIHAT,38}, {4,SNARE,54}, {6,HIHAT,38}, {8,KICK,62},
                              {10,SNARE,48}, {11,SNARE,44}, {12,SNARE,50}, {13,SNARE,52}, {14,KICK,60},
                              {15,KICK,56}, END };

static const ev * const patterns[6][4] = {
    { p0_bass, p0_lead, p0_arp, p0_drum },
    { p1_bass, p1_lead, p1_arp, p1_drum },
    { p2_bass, p2_lead, p2_arp, p2_drum },
    { p3_bass, p3_none, p3_none, p3_drum },
    { p0_bass, p4_lead, p0_arp, p0_drum },      /* the verse's bass, arpeggio, drums */
    { p5_bass, p5_lead, p5_arp, p5_drum },
};
static const uint8_t order[8] = { 0, 1, 4, 5, 2, 3, 0, 1 };

#define SPEED 6
static uint8_t tick, row, pos, sfx_frames;
static const ev *at[4];             /* each channel's next event in the pattern */
uint8_t music_row;                  /* (for the test: rows played, mod 256) */

static void pattern_start(void)
{
    uint8_t c;
    for (c = 0; c < 4; ++c) at[c] = patterns[order[pos]][c];
}

void sound_init(void)
{
    snd_init();
    tick = row = pos = sfx_frames = 0;
    pattern_start();
}

static void play_row(void)
{
    static uint8_t c;
    static const ev *e;
    for (c = 0; c < 4; ++c) {
        e = at[c];
        if (e->row != row) continue;
        at[c] = e + 1;
        if (c == 3) {
            if (sfx_frames) continue;           /* channel 3 is the effect's */
            switch (e->note) {
            case KICK:  snd_noise(3, 6, e->vol * 2, 24); break;
            case SNARE: snd_noise(3, 3, e->vol * 2, 20); break;
            default:    snd_noise(3, 0, e->vol, 18); break;
            }
        } else if (c == 0) {
            snd_tone_p(0, bass_p[e->note], e->vol, 0);
        } else if (c == 1) {
            snd_tone_p(1, note_p[e->note], e->vol, 0);
        } else {
            snd_tone_p(2, note_p[e->note], (uint8_t)(e->vol * 3 / 4), 0);
        }
    }
}

/* ---- effects: (period, volume) a frame each ---- */
typedef struct { uint16_t period; uint8_t vol; } step;
#define F(hz) SND_PERIOD(hz)
/* the samples' pitches: Paula clock / play period / triangle cycle */
static const step fx_gold[]  = { {F(1231), 100}, {F(1642), 110}, {F(1970), 120}, {F(1970), 70}, {0, 0} };
static const step fx_death[] = { {F(1007), 100}, {F(806), 90}, {F(672), 75}, {F(504), 65}, {F(504), 30}, {0, 0} };
static const step fx_level[] = { {F(792), 80}, {F(1008), 90}, {F(1232), 100}, {F(1583), 110},
                                 {F(2015), 120}, {F(2015), 90}, {F(2015), 60}, {0, 0} };
static const step *fx;

void sound_sfx(uint8_t which)
{
    fx = 0;
    switch (which) {
    case SFX_DIG:  snd_noise(3, 2, 100, 40); sfx_frames = 3; return;  /* crunch */
    case SFX_TRAP: snd_noise(3, 5, 96, 32); sfx_frames = 3; return;   /* thud */
    case SFX_GOLD:  fx = fx_gold; break;
    case SFX_DEATH: fx = fx_death; break;
    case SFX_LEVEL: fx = fx_level; break;
    default: return;
    }
    for (sfx_frames = 1; fx[sfx_frames - 1].vol; ++sfx_frames) ;
}

void sound_tick(void)
{
    if (fx) {
        if (fx->vol) {
            snd_tone_p(3, fx->period, fx->vol, 0);
            ++fx;
        } else {
            snd_off(3);
            fx = 0;
        }
    }
    if (sfx_frames) --sfx_frames;
    snd_tick();
    if (tick == 0) {
        play_row();
        ++music_row;
    }
    if (++tick >= SPEED) {
        tick = 0;
        if (++row >= 16) {
            row = 0;
            if (++pos >= 8) pos = 0;
            pattern_start();
        }
    }
}
