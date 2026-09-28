#pragma once
/* The omnichord, after the Suzuki Omnichord OM-108 (its owner's manual). Chord buttons in three rows, MAJOR, MINOR and
   7th, over twelve roots on the circle of fifths from Db; pressed together they make more chords (MAJ+7 maj7, MIN+7 m7,
   MAJ+MIN dim, all three aug, a MAJ with the 7th button to its left sus4, with the MINOR to its left add9), each
   played as three notes. The strumplate: 13 strings, the chord's root, third and fifth in each octave from F# to F,
   four times, and the root on top. Ten voices, each a main and a sub sound; keyboard mode, where the buttons are a
   keyboard and the strings a drum set. On the PC keyboard the three letter rows are the button rows (1 … =, Q … ],
   A … ' with Enter or \ as the twelfth) and Z … / the first ten strings. */
#include <stdint.h>
#include <stdbool.h>

#define OMNI_ROOTS  12
#define OMNI_ZONES  13                     /* the strumplate's strings */
#define OMNI_VOICES 10
#define OMNI_CUSTOM OMNI_VOICES            /* the voice ⇧← → picks: that preset, no sub */
enum { ROW_MAJ, ROW_MIN, ROW_7, OMNI_ROWS };
enum { SUF_MAJ, SUF_MIN, SUF_7, SUF_MAJ7, SUF_MIN7, SUF_AUG, SUF_DIM, SUF_SUS4, SUF_ADD9, OMNI_SUFFIXES };

struct omni_voice { const char *name; uint8_t main, sub; int8_t sub_oct; };
extern const struct omni_voice omni_voices[OMNI_VOICES];

struct omni_state {
    uint8_t  root, suffix;                 /* the chord: a root (its button's column, Db first) and a suffix */
    bool     chord_on;                     /* a button is down (a key or the pointer) */
    bool     hold;                         /* CHORD HOLD: the chord stays after the buttons are let go */
    bool     autoplay;                     /* CHORD AUTO: the rhythm plays the chord and a bass; else the chord is held */
    bool     sync;                         /* SYNC START: the first chord starts the rhythm */
    bool     keyboard;                     /* KEYBOARD mode */
    int8_t   octave;                       /* the strings, -2 .. 2 octaves */
    int8_t   kb_octave;                    /* keyboard mode: -1, 0, 1 */
    int8_t   transpose;                    /* -6 .. 6 semitones: the chord and the strings */
    int8_t   tune;                         /* -6 .. 6 Hz from A = 440: everything */
    uint8_t  voice;                        /* 0 .. 9, or OMNI_CUSTOM */
    uint8_t  main_level, sub_level, sustain;   /* STRUMPLATE MAIN, SUB and SUSTAIN, 0 .. 127 */
    uint8_t  pad_level;                    /* CHORD: the held chord's and the accompaniment's level */
    uint8_t  pad_preset, strum_preset;     /* the chord's sound; CUSTOM's strings */
    uint8_t  strum_override;               /* 0xFF = none: a page plays the strings with its own sound (no sub) */
    uint8_t  chord_notes[3], n_notes;      /* the chord, voiced from F#3 to F4 */
    uint16_t held[OMNI_ROWS];              /* the buttons down, a bit per root */
    uint64_t root_hit_ms, zone_hit_ms[OMNI_ZONES];
};
extern struct omni_state omni;
/* the chord as one word for the audio side (the accompaniment): root | suffix << 4 | on << 8 | (transpose + 6) << 9 */
extern volatile uint32_t omni_chord_word;
extern const char *const omni_root_names[OMNI_ROOTS];       /* "Db" "Ab" … "F#" */
extern const char *const omni_suffix_names[OMNI_SUFFIXES];  /* "", "m", "7", "maj7", "m7", "aug", "dim", "sus4", "add9" */
extern const char *const omni_row_names[OMNI_ROWS];         /* "MAJOR" "MINOR" "7TH" */
extern const char omni_row_keys[OMNI_ROWS][OMNI_ROOTS + 1];
extern const char omni_zone_keys[11];                       /* "zxcvbnm,./" */
extern const uint8_t omni_root_pc[OMNI_ROOTS];

void omni_init(void);
void omni_key(uint8_t code, bool down, uint64_t now_ms);    /* the button rows, the strings' row, Backspace (INSTANT OFF) */
void omni_work(uint64_t now_ms);                            /* main loop: a combination let go unevenly settles */
void omni_button(int row, int root, bool down, uint64_t now_ms);   /* a chord button by position (the pointer) */
void omni_strum(int zone, bool down, uint64_t now_ms);
void omni_off(uint64_t now_ms);                             /* INSTANT OFF: the chord, the strings, the accompaniment */
void omni_panic(void);
void omni_sound(bool strum, int d, uint64_t now_ms);        /* ⇧↑↓ the chord's sound, ⇧←→ the strings' (CUSTOM) */
void omni_set_voice(int v);
void omni_set_hold(bool on, uint64_t now_ms);
void omni_set_auto(bool on, uint64_t now_ms);           /* CHORD MANUAL / AUTO */
void omni_set_tune(int hz);                                 /* -6 .. 6 */
void omni_set_transpose(int semis);                         /* -6 .. 6 */
int  omni_zone_note(int zone);                              /* the MIDI note a string plays now */
int  omni_chord_tones(uint32_t word, uint8_t pcs[3]);       /* the chord's pitch classes, root first: how many */
/* which chord buttons make: the root (a column), and its suffix; -1 when none is down. newest: the last one pressed */
int  omni_recognize(const uint16_t held[OMNI_ROWS], int newest_row, int newest_root, int *suffix);
void omni_note_name(int midi, char *out /* >= 5 */);
void omni_chord_name(char *out /* >= 12 */);                /* "Cmaj7", "F#m" */
/* keyboard mode: the MAJOR row's T P ↓ ↑ and eight drums, the strings' seven drums and INSTANT OFF's hand claps */
extern const char *const omni_kb_top[OMNI_ROOTS];           /* "T" "P" "↓" "↑" "BD" "SD" "HT" "FT" "CH" "OH" "CC" "HC" */
extern const char *const omni_plate_drums[8];               /* "BD" "SD" "HT" "LT" "CH" "OH" "CC", then "HC" */
int  omni_kb_note(int row, int col);                        /* the note a button plays in keyboard mode, -1 none */
