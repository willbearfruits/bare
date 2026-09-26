#pragma once
/* Sieves: Xenakis's way of building scales and rhythms from residue classes (Nomos Alpha, Psappha, Jonchaies). The class
   m@r is every whole number that leaves r when divided by m (3@1: … 1, 4, 7, 10 …); a sieve joins classes with | (or),
   & (and) and - (not), in parentheses as needed. The major scale is (-3@2&4@0)|(-3@1&4@1)|(3@2&4@2)|(-3@0&4@3); 3@0|4@0
   is three against four. Four sieves, S1-S4, written as text and compiled to a small program for the audio side. Read as
   rhythms they count sixteenths from the start (the rhythm section's SIEVE pattern: S1 kick, S2 snare, S3 hat, S4 bass);
   read as pitches, steps of `unit` per semitone from MIDI note 0 (1: semitones, so 60 is middle C; 6: the ANS's 72 an
   octave). */
#include <stdint.h>
#include <stdbool.h>

#define SIEVES      4
#define SIEVE_TEXT  64
#define SIEVE_OPS   40
#define SIEVE_NONE  (-0x7FFFFFFF)                  /* sieve_next / sieve_nearest: the sieve has no members */

enum { SV_RES, SV_NOT, SV_AND, SV_OR };
struct sieve_prog { uint8_t n; struct { uint8_t op; uint16_t m, r; } op[SIEVE_OPS]; };
struct sieve {
    char     text[SIEVE_TEXT];
    uint8_t  unit;                                 /* read as pitches: steps a semitone, 1 2 3 or 6 */
    /* compiled (the audio side reads prog; sieve_compile swaps it in with interrupts held) */
    struct sieve_prog prog;
    bool     ok;
    uint32_t period;                               /* the moduli's least common multiple (0: past 100000) */
    int      members;                              /* how many in one period */
    char     err[40];
};
extern struct sieve sieves[SIEVES];
extern volatile uint32_t sieve_changes;           /* counts compiles: pictures key on it */

void    sieve_init(void);                          /* the four defaults, compiled */
bool    sieve_compile(struct sieve *s);            /* s->text into s->prog; false: s->err says why (prog unchanged) */
bool    sieve_has(const struct sieve *s, int32_t x);
/* the first member at x or above, the closest one (the lower on a tie): within `within` of x, else SIEVE_NONE */
int32_t sieve_next(const struct sieve *s, int32_t x, int within);
int32_t sieve_nearest(const struct sieve *s, int32_t x, int within);
/* a pitch in 1/256 semitones snapped to the sieve (its members as pitches), or unchanged if none is within 2 octaves */
int32_t sieve_snap_pitch(const struct sieve *s, int32_t semis_q8);

struct sieve_example { const char *text, *what; };
extern const struct sieve_example sieve_examples[];
extern const int sieve_example_count;
