#pragma once
/* MERZBOW (a LINEAGE view, after Merzbow, Masami Akita): noise from junk. Four sources — SCRAPE, a finger (or the
   mouse) dragged over metal: noise through a resonant band, the place its pitch, the pressure its level, the speed
   its grit; METAL, junk struck from the letter keys (an inharmonic FM clang each); FEEDBACK, the output fed back into
   itself through a band and a delay, louder than it went in, until it howls; BYTES, this very program's code read as
   8-bit sound — then DRIVE, CRUSH and CHOP over all of it. A cap keeps its loudness down whatever the knobs say (the
   master's limiter guards the peaks): noise to play, not to hurt. */
#include <stdint.h>
#include <stdbool.h>

#define JUNK_OBJECTS 33                   /* the letter rows: Z … /, A … ', Q … ] */
#define JUNK_SCRAPERS 6                   /* the touchpad's fingers and the mouse */
enum { JS_SCRAPE, JS_METAL, JS_FEEDBACK, JS_BYTES, JUNK_SOURCES };

struct junk_state {
    uint8_t drive, bits, chop, feedback, grain, bytes_rate, level;   /* knobs: 0 .. 100 (bits 1 .. 16) */
    bool    feedback_on, bytes_on;
    volatile uint16_t activity[JUNK_SOURCES];   /* how loud each source was lately, 0 .. 32767 (the picture) */
    volatile int32_t  peak;                     /* the highest peak after everything since the picture took it (the wall) */
};
extern struct junk_state junk;

void junk_init(uint32_t rate);
void junk_defaults(void);                       /* the knobs as they start, everything off (a project without MRZ1) */
void junk_strike(int object, int vel);          /* METAL: a key */
void junk_scrape(int who, int x, int y, int z, bool on);   /* SCRAPE: a finger (0 .. 32767 across and down, its pressure) */
bool junk_render(int32_t *l, int32_t *r, uint32_t n, bool add);   /* audio side: over the LINEAGE bus */
void junk_all_off(void);
