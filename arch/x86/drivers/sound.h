#pragma once
/* The sound chip, whichever it is: Intel HDA (hda.c), AC'97 (ac97.c) or a Sound Blaster 16 (sb16.c). Each keeps a
   ring of 16-bit stereo ahead of its DMA, filled from the 1 kHz tick (pump) with the engine's output; without one the
   PC speaker plays (platform.c). */
#include <stdint.h>
#include <stdbool.h>

struct sound {
    const char *name;                       /* short, for the title of the sound device: "HDA", "AC97", "SB16" */
    const char *long_name;                  /* for the log: "Intel HDA", … */
    uint32_t (*rate)(void);                 /* the rate the engine renders at */
    uint32_t out_rate;                      /* what the chip plays (the SB16 resamples to 44.1 kHz) */
    uint32_t (*latency)(void);              /* frames between rendering and hearing */
    void (*pump)(bool silent);              /* the 1 kHz tick: top the ring up */
    void (*prefill)(void);                  /* fill it all, before a stretch without interrupts (BIOS disk calls) */
    void (*hold)(bool on);                  /* silence without running the engine (an export renders) */
    void (*long_lead)(void);                /* no timer interrupts: the main loop pumps, keep more ahead */
    void (*status)(char *out, int cap);     /* one line for the log view */
    void (*test_tone)(bool on);             /* 440 Hz straight into the ring */
    /* optional, 0 where the chip has none */
    void (*poll_jacks)(void);
    bool (*headphones)(void);
    void (*all_outputs)(bool on);
    int  (*inputs)(const char **names, int max);
    bool (*input_select)(int index);        /* -1 = off */
    bool (*input_jack)(int index);
};
extern const struct sound sound_hda, sound_ac97, sound_sb16;
extern const struct sound *sound;           /* the one running, 0 = none */

const struct sound *sound_init(void);       /* HDA, else AC97, else a Sound Blaster 16 */
bool ac97_init(void);
bool sb16_init(void);
static inline void sound_prefill(void) { if (sound) sound->prefill(); }
