#pragma once
/* The splash: after the boot, a few seconds of picture and sound with the name — one of four pieces, never the same
   one twice in a row where the stick can remember the last (it rides in the header the autoload writes anyway).
   Any key, click, touch or MIDI note ends it; its sound fades under the instrument. The pieces:
     ANS          Murzin's photoelectronic synthesizer: the name scratched into a plate's black coating, played by a
                  scan line as 72 pure tones to the octave
     GENDY        Xenakis's dynamic stochastic synthesis: waveforms whose points random-walk between mirrors
     CMI          for the Fairlight: a green terminal, the name typed, a 3D waveform mountain, an 8-bit stab
     METASTASEIS  Xenakis's glissandi as ruled surfaces on graph paper: 46 strings from a unison to a chord
   Picture and sound follow one clock, the audio frames played since the start. */
#include <stdint.h>
#include <stdbool.h>

enum { SPLASH_ANS, SPLASH_GENDY, SPLASH_CMI, SPLASH_META, SPLASHES };
#define SPLASH_RANDOM (-1)
#define SPLASH_OFF    (-2)
extern int splash_mode;                           /* SPLASH_RANDOM (the kernel), SPLASH_OFF (the host programs), or one */
extern const char *const splash_names[SPLASHES];
#define SPLASH_LOGO_ROWS 5
#define SPLASH_LOGO_COLS 39
extern const char *const splash_logo[SPLASH_LOGO_ROWS];     /* the name in ASCII art (figlet's slant, kerned) */

int  splash_pick(int last);                       /* a piece at random, not `last` (-1: any) */
void splash_start(int which);
bool splash_showing(void);                        /* the picture is still up (the sound may ring on after) */
void splash_skip(void);                           /* end it now: the picture goes, the sound fades in 150 ms */
void splash_draw(void);                           /* the main loop, once a frame: the whole screen is the splash's */
void splash_audio(int32_t *l, int32_t *r, uint32_t n);   /* the audio interrupt: its sound, added to the mix */
