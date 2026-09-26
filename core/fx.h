#pragma once
/* The mixer's effects: a reverb every channel can send to (like the echo), and three inserts on the master — a
   resonant filter, a drive and a crusher (fewer bits, a lower rate). All fixed-point; the reverb's delay lines are
   ~40 KB, so it runs on the smallest machine. */
#include <stdint.h>
#include <stdbool.h>

enum { FXF_LOW, FXF_BAND, FXF_HIGH };

struct fx_state {
    uint8_t rev_size, rev_damp, rev_level;          /* 0..100: decay, darkness, return level */
    uint8_t echo_div, echo_feedback;                /* the echo's time (FX_ECHO_DIVS) and repeats 0..100 */
    bool    filter_on; uint8_t filter_mode, filter_cut, filter_res;   /* cut 0..127, res 0..100 */
    bool    drive_on;  uint8_t drive;               /* 0..100 */
    bool    crush_on;  uint8_t crush_bits, crush_rate;   /* 1..16 bits; hold each frame 1..32 times */
};
extern struct fx_state fx;
#define FX_ECHO_DIVS 5
extern const char *const fx_echo_div_names[FX_ECHO_DIVS];
extern const uint8_t fx_echo_div_q4[FX_ECHO_DIVS];     /* the echo time in sixteenths of a beat */

void fx_init(uint32_t rate);
/* the reverb: a mono send in, a stereo return added to L/R (skipped once the tail has died away) */
void fx_reverb(const int32_t *send, int32_t *l, int32_t *r, uint32_t n);
void fx_master(int32_t *l, int32_t *r, uint32_t n);    /* filter → drive → crush, where they are on */
