#pragma once
/* CARLOS (a LINEAGE view, after Wendy Carlos): a Moog-style voice, and her scales. Equal temperament has a step of 100
   cents; for Beauty in the Beast (1986) she made scales whose steps don't divide the octave, chosen so that thirds
   and fifths come out close to pure: alpha 78.0 cents, beta 63.8, gamma 35.1. The letter rows play consecutive steps
   of the scale chosen, from C of the octave; mono glides between notes the way the Moog's portamento did, poly
   plays chords; the touchpad is a ribbon. */
#include <stdint.h>
#include <stdbool.h>

enum { CS_12TET, CS_ALPHA, CS_BETA, CS_GAMMA, CARLOS_SCALES };
extern const char *const carlos_scale_names[CARLOS_SCALES];
extern const int32_t carlos_step_c100[CARLOS_SCALES];   /* cents x 100 a step: 10000, 7800, 6380, 3510 */
#define CARLOS_KEYS 33                                  /* Z … /, A … ', Q … ]: consecutive steps */

struct carlos_state {
    uint8_t scale;
    int8_t  octave;                        /* the lowest key is C of this octave */
    bool    mono;                          /* one voice, gliding (the Moog was monophonic) */
    uint8_t glide;                         /* 0 .. 100: up to a second */
    uint8_t wave;                          /* 0 saw, 1 square, 2 triangle */
    uint8_t cutoff, reso, contour;         /* the ladder-ish filter: 0 .. 127, 0 .. 100, its envelope 0 .. 60 */
    uint8_t attack, decay, sustain, release;   /* the contour generator: 0 .. 100 each */
    uint8_t level;                         /* 0 .. 100 */
    bool    ribbon_steps;                  /* the ribbon (touchpad): steps of the scale, or free */
};
extern struct carlos_state carlos;
extern const char *const carlos_wave_names[3];

void    carlos_init(void);
void    carlos_apply(void);                /* the knobs into the MOOG preset */
int32_t carlos_pitch_q8(int step);         /* a step's pitch from C of the octave, in 1/256 semitones */
void    carlos_key(int step, bool down);   /* a key (a step) down or up */
void    carlos_ribbon(int32_t q8, bool down);   /* the ribbon: a pitch while a finger is on it */
void    carlos_play_q8(int id, int32_t q8, bool down);   /* any pitch, on its own voice (the page's chord comparison) */
void    carlos_all_off(void);
void    carlos_block(uint32_t n);          /* audio side: the glide */
int     carlos_sounding(int32_t *q8, int max);   /* the pitches sounding now (for the picture) */
