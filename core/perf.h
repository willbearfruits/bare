#pragma once
/* Performance effects, played live from the FX page (F10), on everything that is heard or on the input alone (a
   guitar or a microphone through the line in). Four work on a ring of the last seconds: REPEAT loops the last slice
   (a beat repeat), REVERSE plays backwards from the moment it is pressed, TAPE slows to a stop, GATE chops in time.
   FILTER (a DJ filter: low-pass to the left, high-pass to the right, nothing in the middle) and CRUSH (bits and rate)
   are their own. DUB throws everything into the echo and FREEZE into the reverb for as long as they are held; the
   tails ring on after. Lengths and rates follow the tempo (the sequencer's, or the Link session's). */
#include <stdint.h>
#include <stdbool.h>

enum { PF_FILTER, PF_REPEAT, PF_REVERSE, PF_TAPE, PF_GATE, PF_CRUSH, PF_DUB, PF_FREEZE, PERF_FX };
enum { PERF_ALL, PERF_INPUT };
struct perf_state {
    bool on[PERF_FX];                     /* sounding: held (a key, a finger, the mouse) or latched */
    bool latched[PERF_FX];
    int16_t x[PERF_FX], y[PERF_FX];       /* 0..1000: the pad's two axes for each effect */
    int8_t sel;                           /* the effect the pad plays */
    int8_t source;                        /* PERF_ALL or PERF_INPUT */
};
extern struct perf_state perf;
extern const char *const perf_names[PERF_FX], *const perf_x_names[PERF_FX], *const perf_y_names[PERF_FX];
/* the mixer's sends, raised while DUB / FREEZE are held: added to every channel in the mask */
extern volatile uint8_t perf_echo_throw, perf_rev_throw;
extern volatile uint16_t perf_throw_mask;

void perf_init(uint32_t rate);
void perf_alloc(uint32_t bytes);          /* from plan_memory: the ring (a few seconds) */
uint32_t perf_ring_frames(void);
void perf_block(int32_t *l, int32_t *r, uint32_t n);   /* the audio interrupt: the effects, in place */
void perf_work(uint64_t now);             /* the main loop: the tempo, DUB and FREEZE's hold on the echo and reverb */
void perf_value(int fx, char *out, int cap);          /* what the pad sets now, in words: "1/16 beat · fade 20 %" */
/* for the page's picture: the ring's peaks (one a 256 frames, 0..255), where it is written, and what reads it */
struct perf_view { const uint8_t *peaks; uint32_t blocks, w_block; bool frozen;
                   uint32_t rep_from, rep_len, rev_from, rev_len, tape_at;   /* frames back from the write head */
                   int32_t gate_open, level; };
void perf_view(struct perf_view *v);
