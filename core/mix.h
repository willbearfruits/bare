#pragma once
/* The mixer. Every source is a channel with a fader, pan, mute, solo and an echo send; the channels sum into the
   master (echo, volume and limiter in audio.c). Voices land on the PLAY, SEQ or RHYTHM channel by their tag; the
   TOUCH page's circuit has a channel of its own (added last, so older projects' channel numbers still mean the same).
   The INPUT channel is the line in or microphone, muted (not heard) by default, since a laptop's own microphone and
   speakers would howl. The stretcher and the tape's MIX tracks take what is heard; unheard, the input still reaches
   the sampler's IN and a tape track set to IN. */
#include <stdint.h>
#include <stdbool.h>

enum { CH_PLAY, CH_SEQ, CH_RHYTHM, CH_INPUT, CH_STRETCH, CH_TAPE, CH_TOUCH, CH_ANS, CH_UPIC, CH_CLOUD, CH_DOOM, MIX_CHANNELS };
#define MIX_CHANNELS_V1 6                  /* before 2.2: no TOUCH */
#define MIX_DB_MIN  (-60)
#define MIX_DB_MAX  12
#define MIX_DB_OFF  (-61)                  /* the fader at the bottom: silent */

struct mix_channel {
    int8_t  db;                            /* fader: MIX_DB_OFF, or -60 .. +12 dB */
    int8_t  pan;                           /* -100 left .. 100 right (balance) */
    bool    mute, solo;
    uint8_t echo, reverb;                  /* sends, 0..100 */
    int32_t vu_l, vu_r;                    /* peak meters after the fader, decaying */
    int32_t gl, gr;                        /* the gains the last block ended on (Q12), for ramping */
};
struct mix_state {
    struct mix_channel ch[MIX_CHANNELS];
    bool     in_mono;                      /* input: left and right summed to both sides */
    int8_t   input;                        /* the platform's input in use, -1 = off */
    uint32_t in_xruns;                     /* the input ran dry (a glitch) */
};
extern struct mix_state mix;
extern const char *const mix_names[MIX_CHANNELS];

void    mix_init(void);
int32_t mix_gain_q12(int db);              /* 4096 = 0 dB */
bool    mix_heard(int ch);                 /* not muted, and soloed if anything is */
bool    mix_set_input(int index);          /* -1 = off; through the platform */
