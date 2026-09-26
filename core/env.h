#pragma once
/* ADSR envelopes shared by synth voices and FM operators. Level is Q24 (0..ENV_MAX), rates are per-sample steps,
   advanced a block at a time; env_gain() is the level on a perceptual (squared) curve in Q15. */
#include <stdint.h>

#define ENV_MAX ((1 << 24) - 1)
enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_DONE };

struct adsr { int32_t a, d, s, r; };                 /* a/d/r per-sample steps, s the sustain level */

static inline int32_t ms_to_step(uint32_t ms, uint32_t rate) {
    uint32_t samples = ms * rate / 1000;             /* fits 32 bits for any ms < 89 s at 48 kHz */
    if (samples < 1) samples = 1;
    int32_t step = (int32_t)((1u << 24) / samples);
    return step < 1 ? 1 : step;
}

/* advance n samples; a sustain level of 0 ends the envelope, as does the end of the release */
static inline int32_t env_advance(int32_t e, uint8_t *stage, const struct adsr *x, int n) {
    switch (*stage) {
    case ENV_ATTACK:  e += x->a * n; if (e >= ENV_MAX) { e = ENV_MAX; *stage = ENV_DECAY; } break;
    case ENV_DECAY:   e -= x->d * n; if (e <= x->s) { e = x->s; *stage = ENV_SUSTAIN; } break;
    case ENV_SUSTAIN: if (e <= 0) { e = 0; *stage = ENV_DONE; } break;
    case ENV_RELEASE: e -= x->r * n; if (e <= 0) { e = 0; *stage = ENV_DONE; } break;
    default:          e = 0; break;
    }
    return e;
}

static inline int32_t env_gain(int32_t e) { int32_t q = e >> 9; return (q * q) >> 15; }

/* 32768 / n for block lengths 1..32: per-sample ramp steps without a division */
extern const int32_t block_recip[33];
static inline int32_t ramp_step(int32_t from, int32_t to, int n) { return ((to - from) * block_recip[n]) >> 15; }
