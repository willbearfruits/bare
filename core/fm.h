#pragma once
/* 4-operator FM (phase modulation), Ableton-Operator style: algorithms, per-op wave/ratio/level/ADSR, feedback. */
#include <stdint.h>
#include <stdbool.h>
#include "env.h"

#define FM_OPS 4
#define FM_PATCHES 4
#define FM_ALGOS 8

struct fm_op {
    uint8_t  wave;          /* 0 sine 1 tri 2 saw 3 square */
    uint8_t  ratio_x2;      /* frequency ratio ×2: 1 = 0.5, 2 = 1.0 … 32 = 16 */
    uint8_t  fine;          /* + fine/100 of the ratio */
    uint8_t  level;         /* 0..100 */
    uint16_t a_ms, d_ms, r_ms;
    uint8_t  s_pct;
    uint8_t  vel;           /* how much softer a soft note makes it, 0..100 (on a modulator: darker) */
    uint8_t  key;           /* key scaling: quieter up the keyboard, 0..100 (up to -12 dB four octaves above C3) */
};
enum { LFO_SINE, LFO_TRI, LFO_SAW, LFO_SQUARE, LFO_SH, LFO_WAVES };
struct fm_patch {
    char     name[12];
    uint8_t  algo;          /* 0..FM_ALGOS-1 */
    uint8_t  feedback;      /* op 4 self-modulation 0..100 */
    struct fm_op op[FM_OPS];
    /* the LFO, one per note: rate 0..100 (0.1 to 20 Hz), wave, and how far it moves pitch (up to a semitone),
       amplitude (tremolo) and the modulators' levels (the timbre) */
    uint8_t  lfo_rate, lfo_wave, lfo_pitch, lfo_amp, lfo_mod;
};
struct fm_algo { const char *name; uint8_t mod_mask[FM_OPS]; uint8_t carriers; };

extern struct fm_patch fm_bank[FM_PATCHES];
/* patches that don't change: the omnichord's FM PIANO, CELESTE and VIBES (a preset's src FM_PATCHES + i) */
#define FM_FIXED 5
const struct fm_patch *fm_patch_of(int src);      /* the user's bank below FM_PATCHES, the fixed ones above */
extern const struct fm_algo fm_algos[FM_ALGOS];

/* per-voice state, owned by the synth voice */
struct fm_voice {
    uint32_t ph[FM_OPS], inc[FM_OPS];
    int32_t  env[FM_OPS];            /* Q24 */
    int32_t  gain[FM_OPS];           /* env_gain at the end of the last block (Q15); ramped across the next */
    uint8_t  stage[FM_OPS];
    struct adsr adsr[FM_OPS];
    int32_t  fb_out;
    uint32_t bend_q16;               /* the tracker's pitch offset on every operator, 65536 = none */
    int32_t  scale[FM_OPS];          /* velocity and key scaling of each operator's level, Q15 */
    uint32_t lfo_ph, lfo_inc, rng; int32_t sh;
};

void fm_init(void);
void fm_patch_sanitize(struct fm_patch *p);          /* clamp every field into range (loaded projects) */
void fm_voice_start(struct fm_voice *v, const struct fm_patch *p, uint32_t base_inc_q32, uint32_t rate, uint8_t note, uint8_t vel);
void fm_voice_release(struct fm_voice *v, int32_t fast_step);   /* fast_step 0: each op's own release */
bool fm_voice_done(const struct fm_voice *v, const struct fm_patch *p);
void fm_voice_render(struct fm_voice *v, const struct fm_patch *p, int32_t *out, int n);   /* n mono Q15 samples */
