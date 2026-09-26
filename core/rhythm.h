#pragma once
/* The Omnichord's rhythm section: drum patterns and a bass that follows the held chord (auto bass). It runs on the
   sample clock like the sequencer, at the sequencer's tempo, and falls in step with the sequencer when both play. */
#include <stdint.h>
#include <stdbool.h>

#define RHYTHM_PATTERNS 9
#define RHYTHM_SIEVE    8               /* the last: its lanes are the sieves (core/sieve.h) */

struct rhythm_state {
    bool    playing, bass;                /* bass: the auto bass follows the chord buttons */
    uint8_t pattern;
    int16_t pos;                          /* current step, -1 before the first */
    uint8_t level;                        /* 0..127: the drums' and bass's velocity */
    uint8_t mute;                         /* a bit per lane: 1 kick, 2 snare, 4 hat, 8 bass */
};
extern struct rhythm_state rhythm;
extern const char *const rhythm_names[RHYTHM_PATTERNS];

void     rhythm_init(uint32_t rate);
void     rhythm_play(bool on);
int      rhythm_steps(void);              /* steps in a bar: 16, or 12 for the triplet and 3/4 patterns */
int      rhythm_beat_steps(void);         /* steps per beat: 4, or 3 */
bool     rhythm_hit(int lane, int step);  /* lane 0 kick, 1 snare, 2 hat, 3 bass: does the pattern play there (this bar) */
/* the audio loop's clock, like seq_run_events / seq_next_event / seq_advance */
void     rhythm_run_events(void);
uint32_t rhythm_next_event(void);
void     rhythm_advance(uint32_t frames);
/* Link: a start the given frames from now; where the steps are (Q16, since the start); a nudge of the next step */
void     rhythm_start_in(uint32_t frames);
int64_t  rhythm_steps_q16(void);
uint32_t rhythm_step_q16(void);
void     rhythm_nudge(int32_t q16);
