#pragma once
/* RADIGUE (a LINEAGE view, after Éliane Radigue): a drone of eight partials of one tone. Each partial is a harmonic of
   the base (1 .. 16) tuned a hair off it (±20 cents, in tenths of a cent): two partials on the same harmonic beat at
   the difference of their frequencies. Each has its level and breathes in and out over its own period (30 s .. 10
   min). The base glides to a new note over the sweep's time (up to 30 minutes), in pitch, so the glissando is even;
   the drone fades in and out over the fade's time. With keys following the chord, the base goes to the chord's root.
   Pitches are in tenths of a cent from MIDI note 0 (C2 = 36000). The partials are sines read from an 8192-entry table
   with linear interpolation, into the LINEAGE bus. */
#include <stdint.h>
#include <stdbool.h>

#define DRONE_PARTIALS 8
struct drone_partial {
    uint8_t  harmonic;                     /* 1 .. 16 */
    int16_t  detune;                       /* tenths of a cent, -200 .. 200 */
    uint8_t  level;                        /* 0 .. 100 */
    uint16_t breath_s;                     /* the breath's period, 30 .. 600 s */
};
struct drone_state {
    struct drone_partial p[DRONE_PARTIALS];
    int32_t  target;                       /* where the base goes (or is), tenths of a cent */
    uint16_t sweep_s;                      /* how long a sweep takes: 0 (at once) .. 1800 */
    uint8_t  fade_s;                       /* 1 .. 60 */
    uint8_t  depth;                        /* how far the partials breathe out: 0 .. 100 */
    uint8_t  level;                        /* 0 .. 100 */
    bool     playing;                      /* fading in or holding; false: fading out, then silent */
};
extern struct drone_state radigue;

void     drone_init(uint32_t rate);
void     drone_defaults(void);             /* the patch as it starts (a project without RDG1), stopped at once */
void     drone_play(bool on);              /* in or out over the fade */
void     drone_sweep_to(int32_t target);   /* the base glides there over sweep_s (main loop) */
bool     drone_render(int32_t *l, int32_t *r, uint32_t n, bool add);   /* audio side: into the LINEAGE bus */
/* for the picture and the checks */
bool     drone_sounding(void);
uint32_t drone_seconds(void);              /* since it began to sound */
int32_t  drone_base_now(void);             /* where the base is now */
uint32_t drone_sweep_left_s(void);         /* 0: not sweeping */
int32_t  drone_fade_q15(void);             /* the fade's gain now */
int32_t  drone_partial_amp(int i);         /* a partial's amplitude now (level, breath, fade), 0 .. 32767 */
int32_t  drone_partial_pitch(int i);       /* its pitch now, tenths of a cent */
int32_t  drone_pair_amp(int i);            /* with its twin (the nearest partial on the same harmonic), the pair's
                                              amplitude this instant: beating; its own without one */
int      drone_twin(int i);                /* that twin, or -1 */
int32_t  drone_beat_mhz(int i);            /* how fast it beats with the twin, in thousandths of a Hz; -1 without one */
uint32_t drone_hz_milli(int32_t pitch);    /* a pitch's frequency, in thousandths of a Hz */
