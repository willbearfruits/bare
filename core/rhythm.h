#pragma once
/* The omnichord's rhythm section, after the OM-108: its ten patterns in two sets — rock 1, rock 2, slow rock, country,
   swing; disco, hip hop, funk, bossanova, waltz — and BARE!'s own (pop, 16 beat, reggae, the sieves). A pattern is a
   bar of lanes: drums, percussion, a bass and the chord in degrees of the chord the buttons hold (they play with CHORD
   AUTO). It runs on the sample clock like the sequencer, at its tempo, and falls in step with it when both play; a new
   pattern starts on the next bar. The OM-108's own drum kit, or the OM-84's three drums (CLASSIC). */
#include <stdint.h>
#include <stdbool.h>

#define RHYTHM_PATTERNS 14
#define RHYTHM_SIEVE    8                 /* its lanes are the sieves (core/sieve.h) */
/* the sets on the PLAY page, as the OM-108 has them (its PATTERN buttons, upper and lower), and BARE!'s */
#define RHYTHM_SETS 3
extern const uint8_t rhythm_sets[RHYTHM_SETS][5];   /* 0xFF: no pattern there */
extern const char *const rhythm_set_names[RHYTHM_SETS];

/* the drums: the kit's sounds, played by the patterns, the keyboard mode's buttons and strings, and MIDI channel 10 */
enum { DR_BD, DR_SD, DR_CH, DR_OH, DR_HT, DR_LT, DR_FT, DR_CC, DR_HC, DR_RIM, DR_TAMB, DR_CONGA, DR_MARACAS, DR_CLAVES,
       DR_BD808, DRUMS };

struct rhythm_state {
    bool     playing;
    bool     classic;                     /* the OM-84's kit: KICK, SNARE and HAT for everything */
    uint8_t  pattern, next;               /* next: the pattern waiting for the bar, 0xFF none */
    int16_t  pos;                         /* current step, -1 before the first */
    uint8_t  level;                       /* RHYTHM volume, 0..127 */
    uint8_t  mute;                        /* a bit per lane: 1 kick, 2 snare, 4 hat, 8 bass, 16 percussion, 32 chord */
    volatile uint16_t pads;               /* keyboard mode: drums held on the strings, played on every step (DR_* bits) */
};
extern struct rhythm_state rhythm;
extern const char *const rhythm_names[RHYTHM_PATTERNS];

void     rhythm_init(uint32_t rate);
void     rhythm_play(bool on);
void     rhythm_select(int pattern);      /* the next bar's (at once when stopped) */
int      rhythm_steps(void);              /* steps in a bar: 16, or 12 for the triplet and 3/4 patterns */
int      rhythm_beat_steps(void);         /* steps per beat: 4, or 3 */
bool     rhythm_hit(int lane, int step);  /* lane 0 kick, 1 snare, 2 hat, 3 bass, 4 percussion, 5 chord: plays there? */
void     rhythm_drum(int kind, int vel);  /* a drum now (the kit, MIDI channel 10) */
void     rhythm_pad(int kind, bool down); /* keyboard mode's strings: at once when stopped, else on every step while held */
void     rhythm_pads_clear(void);
/* the audio loop's clock, like seq_run_events / seq_next_event / seq_advance */
void     rhythm_run_events(void);
uint32_t rhythm_next_event(void);
void     rhythm_advance(uint32_t frames);
/* Link: a start the given frames from now; where the steps are (Q16, since the start); a nudge of the next step */
void     rhythm_start_in(uint32_t frames);
int64_t  rhythm_steps_q16(void);
uint32_t rhythm_step_q16(void);
void     rhythm_nudge(int32_t q16);
