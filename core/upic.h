#pragma once
/* UPIC, after the drawing machine Xenakis built at CEMAMu (1977; Mycenae-Alpha, 1978): a page of arcs, each a line of
   pitch over time drawn by hand, each played by a sound of its own. A cursor crosses the page in its length (bars of
   the tempo, or seconds) and loops; where it meets an arc, that arc sounds, gliding as the line rises and falls — a
   synth voice started at the arc's first pitch and bent along it (tags 0x900 | arc, the mixer's UPIC channel). The
   hand can hold the cursor instead (scrubbing, as the 1987 UPIC let you). Arcs are kept as points (time across the
   page, 0..65535; pitch in 1/256 semitones from MIDI note 0), each arc's points together and in time order. */
#include <stdint.h>
#include <stdbool.h>

#define UPIC_ARCS   192
#define UPIC_POINTS 6144
#define UPIC_TAG    0x900
#define UPIC_LO     (24 * 256)                 /* the page's pitches: C1 .. C8 */
#define UPIC_HI     (108 * 256)

struct upic_pt  { uint16_t t, p; };
struct upic_arc { uint16_t first, n; uint8_t sound, level; };
struct upic_data { struct upic_arc arc[UPIC_ARCS]; struct upic_pt pt[UPIC_POINTS]; };   /* saved as it is (project media) */
struct upic_state {
    struct upic_data d;
    volatile uint16_t narcs, npts;
    volatile bool playing;
    uint8_t  bars, seconds;                    /* the page's length: bars of the tempo (1..32), or 0 and seconds (1..120) */
    volatile uint32_t pos;                     /* the cursor: the page is 2^32 */
    volatile int32_t seek;                     /* a place to jump to + 1 (0: none), taken by the next block */
    volatile int32_t scrub;                    /* -1, or the time (0..65535) the hand holds the cursor at */
    uint64_t scrub_ms;                         /* when the hand last held it: a hold not renewed lets go (upic_work) */
    volatile uint32_t changes;                 /* counts edits: pictures key on it */
};
extern struct upic_state upic;

void upic_init(uint32_t rate);
void upic_clear(void);
void upic_work(uint64_t now);                  /* the main loop: the cursor's speed from the tempo; a stale hold let go */
void upic_block(uint32_t n);                   /* the audio loop, before each block */
void upic_all_off(void);                       /* every arc's voice let go */
/* edits (the audio side reads the page: each is done with interrupts held) */
int  upic_add(const struct upic_pt *pts, int n, uint8_t sound, uint8_t level);   /* an arc; its index, or -1 (full) */
void upic_delete(int arc);
void upic_transpose(int32_t semis_q8);         /* every arc */
void upic_mirror_time(void);                   /* retrograde: the page backwards */
void upic_mirror_pitch(void);                  /* inversion: upside down around the page's middle */
int32_t upic_pitch_at(int arc, uint16_t t);    /* the arc's pitch at t (it must span t) */
/* projects: the page as media; after the media are read, upic_loaded checks what came and makes it the page */
uint32_t upic_bytes(void);                     /* what the save writes: 0 for an empty page */
void upic_loaded(int narcs, int npts);
uint32_t upic_frames_per_page(void);
