#pragma once
/* What the splash pieces (core/splash_*.c) share with the frame in core/splash.c. */
#include "splash.h"
#include "gfx.h"
#include "text.h"
#include "libc.h"
#include "tables.h"

struct splash_piece {
    const char *name, *caption;
    int ms, tail_ms;                                  /* the picture's length; the sound may ring on tail_ms more */
    void (*start)(uint32_t rate);                     /* main loop, before the first frame: state and colours */
    void (*draw)(int t_ms, bool first);               /* main loop: the picture at t_ms (first: paint it all) */
    void (*audio)(int32_t *l, int32_t *r, uint32_t n, uint32_t t);   /* interrupt: the sound from frame t, added */
};
extern const struct splash_piece splash_ans, splash_gendy, splash_cmi, splash_meta;

/* The name in ASCII art (figlet's slant font), and where the pieces put it: cells of cw x ch pixels from (lx, ly),
   drawn with the cell font at `scale`. W, H: the screen. */
#define LOGO_ROWS SPLASH_LOGO_ROWS
#define LOGO_COLS SPLASH_LOGO_COLS
struct splash_geo { int W, H, scale, lx, ly, cw, ch; const struct font *f; };
extern struct splash_geo sg;
void splash_glyph(int px, int py, char c, uint8_t color, int scale);        /* one character, top left at px, py */
void splash_captions(const char *caption, uint8_t color);                   /* the release bottom left, caption right */
void splash_ramp(int first, int n, uint32_t from, uint32_t to);             /* palette entries first..first+n-1 */

/* sound */
static inline uint32_t sp_rand(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static inline int32_t sp_sin(uint32_t phase) { return sine_q15_8192[phase >> 19]; }         /* a 32-bit phase */
uint32_t sp_inc(uint32_t hz_q8, uint32_t rate);                             /* phase step for a frequency in Q8 Hz */
/* a small stereo reverb for the pieces' sound (their own: the mixer's is the project's) — four delay lines, a
   Hadamard matrix, a low-pass in each loop */
struct sp_verb { int32_t *line[4]; uint32_t len[4], pos[4]; int32_t lp[4]; int32_t fb, damp; };
void sp_verb_init(struct sp_verb *v, uint32_t rate, int fb_q15, int damp_q15);
void sp_verb_run(struct sp_verb *v, const int32_t *inl, const int32_t *inr, int32_t *outl, int32_t *outr, uint32_t n);
