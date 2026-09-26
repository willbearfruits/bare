#pragma once
/* GENDY: Iannis Xenakis's dynamic stochastic synthesis (GENDY3, 1991; S.709, 1994) as a sound to play anywhere, like
   the FM patches. A period of the wave is a polygon of breakpoints; after every period each point's height and each
   segment's length take a random step from a chosen distribution, and mirrors throw back what crosses them. The height
   steps are themselves a random walk (Xenakis's second-order walk), so with INERTIA the wave drifts rather than jitters.
   The segments' lengths are shares of the note's period, so the note stays in tune while the timbre wanders; DRIFT lets
   the period walk too, away from the note and back, as in the late pieces. Every period's mean is taken out. */
#include <stdint.h>
#include <stdbool.h>

#define GENDY_PATCHES 4
#define GENDY_POINTS  16
enum { GD_LINEAR, GD_CAUCHY, GD_LOGIST, GD_HYPCOS, GD_ARCSIN, GD_EXPON, GD_DISTS };
extern const char *const gendy_dist_names[GD_DISTS];

struct gendy_patch {
    char     name[12];
    uint8_t  points;                 /* breakpoints a period, 2..16 */
    uint8_t  adist, astep;           /* the heights: the steps' distribution, and how big (0..100) */
    uint8_t  amirror;                /* where the heights' mirrors stand, 5..100 % of full scale */
    uint8_t  inertia;                /* 0..100: 0 a fresh step every period (jitter); up to 100, the step walks too (drift) */
    uint8_t  ddist, dstep;           /* the segments' lengths: distribution, step size (0..100) */
    uint8_t  dmirror;                /* how unequal they may get, 0..100 (0: all the same; 100: 64 to 1) */
    uint8_t  drift;                  /* the period's own walk, 0..100 (0: in tune; 100: two octaves either way) */
    uint8_t  level;                  /* 0..100 */
    uint8_t  cutoff, reso;           /* the voice's filter: 0..127 (127 open), 0..100 */
    uint16_t a_ms, d_ms, r_ms;
    uint8_t  s_pct;
};
extern struct gendy_patch gendy_bank[GENDY_PATCHES];

/* one voice's oscillator (owned by the synth voice, or a picture's) */
struct gendy_osc {
    uint32_t rng, ph, pmul;          /* the random state; where in the period (Q32); the drifted period's factor (Q16) */
    int32_t  lp, dc;                 /* the drift (1/256 semitones); this period's mean */
    uint8_t  n, seg;
    int16_t  amp[GENDY_POINTS + 1];  /* heights, Q15 (amp[n] = amp[0]: the last segment closes the period) */
    int16_t  vel[GENDY_POINTS];      /* the heights' steps */
    uint16_t len[GENDY_POINTS];      /* the segments' lengths, relative (256: the average) */
    uint32_t edge[GENDY_POINTS + 1]; /* where each segment starts in the period (Q32); edge[n] is its end */
    uint32_t recip[GENDY_POINTS];    /* 2^30 / (the segment's length >> 16) */
    uint32_t periods;
};

void gendy_init(void);                                    /* the four patches' defaults */
void gendy_patch_sanitize(struct gendy_patch *p);         /* every field into range (loaded projects) */
void gendy_osc_start(struct gendy_osc *g, const struct gendy_patch *p, uint32_t seed);
/* n samples (Q15, the period's mean taken out, at ¾ so the heights' extremes still fit) at inc a sample (Q32: 2^32 is a
   period), into out */
void gendy_render(struct gendy_osc *g, const struct gendy_patch *p, int32_t *out, int n, uint32_t inc);
void gendy_period(struct gendy_osc *g, const struct gendy_patch *p);   /* one period's walk (pictures step it themselves) */
