#pragma once
/* Stochastic clouds, after Xenakis's Pithoprakta (1956) and Achorripsis (1957): masses of notes whose moments, pitches
   and glissandi are drawn from distributions. The onsets are a Poisson stream (so many a second, each at a random
   moment), the pitches fall across a band (evenly, or bunched in its middle), every note slides at a speed of its own
   (Gaussian: mostly slowly, a few fast — the gas molecules of Pithoprakta's strings), its length and loudness varied.
   Four clouds can sound at once, each with its own sound; a pitch sieve makes one land on a scale, a rhythm sieve on
   the sixteenths the sieve keeps (the rhythm section's, while it plays). They sound on the mixer's CLOUDS channel. */
#include <stdint.h>
#include <stdbool.h>

#define CLOUDS       4
#define CLOUD_NOTES  20                   /* sounding at once, all clouds together: the rest of the voices stay free */
#define CLOUD_MARKS  256                  /* the last notes, for the picture */

struct cloud {
    bool    on;
    uint8_t sound;                        /* a preset */
    uint8_t density;                      /* 0..100: 0.2 to 200 notes a second (evenly on a log scale) */
    uint8_t low, high;                    /* the band, MIDI notes */
    uint8_t shape;                        /* 0: evenly across the band; 1: bunched in its middle */
    uint8_t length;                       /* 0..100: 20 ms to 4 s, the notes' usual length */
    uint8_t spread;                       /* 0..100: how much the lengths vary */
    uint8_t glide;                        /* 0..100: how fast the glissandi go (0 none; 100: 12 semitones a second, typically) */
    uint8_t level;                        /* the usual velocity, 1..127 */
    uint8_t dyn;                          /* 0..100: how much it varies */
    uint8_t width;                        /* 0..100: the stereo spread */
    int8_t  pitch_sieve;                  /* -1, or 0..3: snapped to S1..S4 */
    int8_t  rhythm_sieve;                 /* -1: free (Poisson); 0..3: only on the sixteenths S1..S4 keep */
    uint8_t pad[2];
};
extern struct cloud clouds[CLOUDS];

/* the last notes played, written by the audio side: when (cloud_frames), how long, from which pitch and how fast it
   slid (1/256 semitones, and per second), the cloud and velocity */
struct cloud_mark { uint32_t at, len; int16_t pitch; int16_t glide; uint8_t cloud, vel, pad[2]; };
extern struct cloud_mark cloud_marks[CLOUD_MARKS];
extern volatile uint32_t cloud_mark_n;    /* marks written so far (the ring's head) */
extern volatile uint32_t cloud_frames;    /* the clouds' clock: frames since the start */

void     cloud_init(uint32_t rate);       /* the four defaults, all off */
void     cloud_defaults(void);            /* the same, the rate kept (a project without clouds) */
void     cloud_sanitize(struct cloud *c);
void     cloud_block(uint32_t n);         /* the audio loop, before each block: notes begin, slide and end */
void     cloud_stop(int k);               /* a cloud's notes let go (it stays on or off as it is) */
uint32_t cloud_per_second_x100(const struct cloud *c);   /* its density as notes a second ×100 */
/* one note drawn from cloud k's distributions (with a random state of the caller's), and the wait to the next */
struct cloud_note { int32_t pitch; uint32_t ms; int vel, pan; int32_t glide; };
void     cloud_draw(int k, uint32_t *rng, struct cloud_note *out);
uint32_t cloud_wait(int k, uint32_t *rng, uint32_t rate);
uint32_t cloud_length_ms(const struct cloud *c);
