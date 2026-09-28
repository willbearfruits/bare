#pragma once
/* Keys that follow the chord: the omnichord's idea — hold a chord and whatever you play lands on it — offered to the
   other pages. ⇧K turns it on and off. Off, every page is as it was: chromatic keyboards, the instruments' own scales,
   the sieves. On, a note played from the letter rows, a pad, a stepped strip, MIDI, a stroke on UPIC or a cloud moves to
   the nearest note of the omnichord's chord (the last one the buttons made; ties go up), keeping its octave. The
   tracker's note entry, the sieves' scale and the omnichord itself are left alone. */
#include <stdint.h>
#include <stdbool.h>

extern bool harmony_on;
bool     harmony_active(void);             /* on (there is always a chord: the last one) */
int      harmony_note(int note);           /* a MIDI note, moved to the nearest chord tone when on */
int32_t  harmony_snap_q8(int32_t q8);      /* the same in 1/256 semitones, any pitch (either side may call it) */
uint16_t harmony_pcs(void);                /* the chord's pitch classes, a bit each */
const char *harmony_label(void);           /* "♪ Cmaj7": the title bar's, while on */
