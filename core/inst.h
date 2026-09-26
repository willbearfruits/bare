#pragma once
/* Instruments anyone can add (INSTRUMENTS.md has the file format): a text file in INSTR/ on the stick — a name, a
   sound made of what the synth has, and how it is played (the letter rows, the touchpad as a strip, a grid of pads,
   an arpeggio). The examples in instruments/ are built in; a stick's file of the same name replaces one. Each
   instrument's sound is a preset (P_INST1 + its index), so it plays anywhere; its page is part of PLAY (F1 again steps
   to it). A file that can't be read makes no instrument, and one with mistakes says where they are (its page, the log).
   Playing is declarative: the page says which notes are held (and where the strip's finger is); the audio side turns
   that into voices — chords, one voice gliding, or an arpeggio on the tempo. */
#include <stdint.h>
#include <stdbool.h>
#include "synth.h"

#define INSTS      8
#define INST_KNOBS 8
enum { IK_WAVE, IK_WIDTH, IK_DETUNE, IK_ATTACK, IK_DECAY, IK_SUSTAIN, IK_RELEASE, IK_CUTOFF, IK_RESO, IK_ENVELOPE,
       IK_VIBRATO, IK_LEVEL, IK_GLIDE, IK_RATE, IK_OCTAVES, IK_COUNT };
extern const char *const inst_knob_names[IK_COUNT];
enum { ARP_OFF, ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM };
enum { STEPS_FREE, STEPS_SEMI, STEPS_SCALE };
enum { KEYS_OFF, KEYS_CHROMATIC, KEYS_SCALE };
enum { HOLD_NO, HOLD_YES, HOLD_TOGGLE };                     /* toggle: a key opens its note, the next press closes it */
enum { TUNING_EQUAL, TUNING_JUST };                          /* just: 5-limit ratios from the tonic (the fifth 3:2) */
enum { SC_CHROMATIC, SC_MAJOR, SC_MINOR, SC_DORIAN, SC_PHRYGIAN, SC_LYDIAN, SC_MIXOLYDIAN, SC_PENTA, SC_MINORPENTA,
       SC_BLUES, SC_WHOLETONE, SC_HARMONIC, SC_S1, SC_S2, SC_S3, SC_S4, SC_COUNT };
extern const char *const inst_scale_names[SC_COUNT];

struct inst {
    char name[12], by[24], about[64], file[13];
    struct preset snd;                       /* its sound: the preset P_INST1 + its index (snd.name is name) */
    uint8_t wave;                            /* which of inst_wave_names */
    uint8_t strip_lo, strip_hi, steps;       /* the touchpad as a strip (strip_hi 0: none) */
    uint8_t scale, keys, keys_root;          /* the letter rows: off, chromatic or the scale, from keys_root */
    uint8_t pads_w, pads_h, pads_root;       /* a grid of pads (pads_w 0: none), up the scale from pads_root */
    bool    mono, bellows;                   /* bellows: the level follows the air pumped in (the touchpad, Enter) */
    uint8_t hold, tuning, tonic;             /* HOLD_*; TUNING_*, and the tonic's pitch class for just tuning */
    uint16_t glide_ms;
    uint8_t arp, rate, octaves;              /* the arpeggio: its pattern, steps a beat, octaves */
    uint8_t knob[INST_KNOBS], nknobs;
    int     errors; char err[72];            /* what was wrong with the file (the first mistake, and how many) */
};
extern struct inst insts[INSTS];
extern int inst_count;
struct inst_text { const char *file, *text; };
extern const struct inst_text inst_builtin[];
extern const int inst_builtin_count;
extern const char *const inst_wave_names[];

bool inst_parse(const char *text, int len, struct inst *out);   /* false: nothing to play (no name, or not a file) */
void inst_load_all(void);                   /* the built-in ones, then the .TXT files in the stick's INSTR folder */
int  inst_step(const struct inst *in, int root, int k);          /* the k-th note of its scale from root, 1/256 semitones */
int  inst_knob_get(const struct inst *in, int k);
void inst_knob_set(struct inst *in, int k, int v);
void inst_knob_text(const struct inst *in, int k, char *out, int cap);
void inst_apply(int i);                     /* its sound into its preset (after a knob turns) */

/* playing the instrument showing */
void inst_select(int i);                    /* -1: none; what sounded is let go */
int  inst_selected(void);
void inst_note(int id, int32_t pitch_q8, uint8_t vel, bool on);   /* id: a key, pad or MIDI note that holds it */
void inst_strip(bool touching, int32_t pitch_q8, uint8_t vel);
void inst_latch(bool on);                   /* hold: notes stay after they are let go, until new ones are played */
bool inst_latched(void);
void inst_clear(void);                      /* every note held (toggled open, latched) let go */
void inst_pump(int amount);                 /* the bellows: air in (0..32767 is empty to full) */
int  inst_air(void);
void inst_block(uint32_t n);                /* the audio loop, before each block */
int  inst_sounding(int32_t *pitches, int max);                     /* for the picture: what sounds now */

/* its page (core/page_inst.c), which PLAY shows in its place */
bool inst_page_key(int i, uint8_t code, bool down, uint64_t now);
void inst_page_pointer(int i, uint64_t now);
void inst_page_draw(int i, uint64_t now);
bool inst_page_midi(int i, uint8_t note, uint8_t vel, uint64_t now);
