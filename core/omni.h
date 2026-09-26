#pragma once
/* Omnichord-style instrument logic: chord buttons + strum plate, driven by key events. */
#include <stdint.h>
#include <stdbool.h>

#define OMNI_ROOTS 12
#define OMNI_ZONES 11
enum { QUAL_MAJ = 0, QUAL_MIN = 1, QUAL_7TH = 2 };

struct omni_state {
    uint8_t  quality, root;                /* root: index in circle-of-fifths order */
    bool     chord_on, hold;
    int8_t   octave;                       /* strum octave shift */
    uint8_t  pad_preset, strum_preset;
    uint8_t  pad_level, strum_level;       /* 0..127: the chord's and the strings' velocity (the PLAY page's knobs) */
    uint8_t  strum_override;               /* 0xFF = none; a page can route the strum plate to another sound */
    uint8_t  chord_notes[4], n_notes;      /* MIDI notes of the sounding/selected chord (pad voicing) */
    uint64_t root_hit_ms;
    uint64_t zone_hit_ms[2][OMNI_ZONES];   /* [row][zone] last hit time, row 0 = low, 1 = high */
};

extern struct omni_state omni;
extern const char *const omni_root_names[OMNI_ROOTS];   /* "Eb" "Bb" "F" ... circle of fifths */
extern const char *const omni_quality_names[3];
extern const char omni_root_keys[OMNI_ROOTS + 1];       /* "1234567890-=" */
extern const char omni_zone_keys[2][OMNI_ZONES + 1];    /* low row / high row */

void    omni_init(void);
void    omni_key(uint8_t code, bool down, uint64_t now_ms);
void    omni_sound(bool strum, int d, uint64_t now_ms);   /* the chord (pad) or strum sound, one preset along */
/* the same instrument driven by position (the pointer on the PLAY page) */
void    omni_chord(int root, int quality, bool down, uint64_t now_ms);
void    omni_strum(int row, int zone, bool down, uint64_t now_ms);   /* row 0 = low, 1 = high */
void    omni_panic(void);
int     omni_zone_note(int row, int zone);   /* MIDI note a strum zone plays now */
void    omni_note_name(int midi, char *out /* >= 4 */);
