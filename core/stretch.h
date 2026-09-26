#pragma once
/* Extreme time-stretch (paulstretch) of the instrument's own output. The last 10 s are always captured; freezing
   finds the last phrase you played and stretches it, looping through it, in stereo (left and right get independent
   random phases, so the frozen sound is wide). */
#include <stdint.h>
#include <stdbool.h>

#define STRETCH_CAP_SEC   10
#define STRETCH_MAX_WIN   8192
#define STRETCH_BANDS     40                /* 4 per octave from ~23 Hz, for the spectrum display */

struct stretch_state {
    bool     frozen, stay;            /* stay: don't advance through the capture (infinite pad) */
    uint16_t factor;                  /* 1..1024 */
    uint16_t win;                     /* 1024..8192 */
    uint8_t  mix;                     /* 0..100 */
    uint32_t pos;                     /* read position in the capture ring (samples) */
    uint32_t cap_len;                 /* capture ring length */
    uint32_t region_start, region_len;/* the phrase being stretched, in ring positions */
    uint8_t  bands[STRETCH_BANDS];    /* last frame's spectrum for the UI, 0..100 over 60 dB */
};
extern struct stretch_state stretch;

void stretch_init(uint32_t rate);
void stretch_capture(const int32_t *l, const int32_t *r, uint32_t n);   /* the dry mix, from the audio render */
void stretch_pull(int32_t *l, int32_t *r, uint32_t n);             /* adds the stretched audio (audio interrupt) */
void stretch_work(void);                                 /* main loop: compute frames while the FIFO is short */
void stretch_freeze(bool on);
bool stretch_key(uint8_t code, bool down, uint64_t now); /* true if the page used the key */
const int16_t *stretch_capture_buf(uint32_t *head);
/* the sampler's side: hold the capture still, find the phrase freezing would take, fill the ring, freeze on a part */
void     stretch_hold(bool on);
bool     stretch_phrase(uint32_t *start, uint32_t *len);   /* false when nothing was played */
int16_t *stretch_ring(uint32_t *len);
void     stretch_freeze_region(uint32_t start, uint32_t len);
