#include "omni.h"
#include "synth.h"
#include "keys.h"
#include "libc.h"
#include "platform.h"

struct omni_state omni;

const char *const omni_root_names[OMNI_ROOTS] = { "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "Db", "Ab" };
static const uint8_t root_pc[OMNI_ROOTS]      = {  3,   10,   5,   0,   7,   2,   9,   4,  11,   6,    1,    8  };
const char *const omni_quality_names[3] = { "MAJ", "MIN", "7TH" };
const char omni_root_keys[OMNI_ROOTS + 1] = "1234567890-=";
const char omni_zone_keys[2][OMNI_ZONES + 1] = { "zxcvbnm,./ ", "asdfghjkl;'" };

static const uint8_t intervals[3][4] = { { 0, 4, 7, 12 }, { 0, 3, 7, 12 }, { 0, 4, 7, 10 } };
static const uint8_t n_intervals[3] = { 3, 3, 4 };

#define TAG_PAD   0x100
#define TAG_STRUM 0x200

static uint8_t held_root_key;     /* key code of the root button physically held, 0 = none */

static const char *const note_names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
void omni_note_name(int midi, char *out) {
    const char *n = note_names[midi % 12];
    int oct = midi / 12 - 1;
    int i = 0; while (n[i]) { out[i] = n[i]; i++; }
    if (oct < 0) { out[i++] = '-'; oct = -oct; }
    out[i++] = '0' + oct; out[i] = 0;
}

static void build_chord(void) {
    int n = n_intervals[omni.quality];
    int base = 48 + root_pc[omni.root];
    omni.n_notes = n;
    for (int i = 0; i < n; i++) omni.chord_notes[i] = (uint8_t)(base + intervals[omni.quality][i]);
}

int omni_zone_note(int row, int zone) {
    int n = n_intervals[omni.quality];
    int base = 60 + root_pc[omni.root] + 12 * (omni.octave + (row ? 0 : -1));
    return base + 12 * (zone / n) + intervals[omni.quality][zone % n];
}

static void pad_on(uint64_t now) {
    synth_note_off_tag(TAG_PAD);
    build_chord();
    for (int i = 0; i < omni.n_notes; i++)                        /* the chord's notes spread a little, low to high */
        synth_note_on_pan(omni.chord_notes[i], (uint8_t)MAX(1, omni.pad_level), omni.pad_preset, TAG_PAD, omni.n_notes > 1 ? -24 + 48 * i / (omni.n_notes - 1) : 0);
    omni.chord_on = true; omni.root_hit_ms = now;
}
static void pad_off(void) { synth_note_off_tag(TAG_PAD); omni.chord_on = false; }

void omni_init(void) {
    memset(&omni, 0, sizeof omni);
    omni.root = 3; omni.quality = QUAL_MAJ;   /* C major */
    omni.pad_preset = P_ORGAN; omni.strum_preset = P_PLUCK; omni.strum_override = 0xFF;
    omni.pad_level = 100; omni.strum_level = 110;
    build_chord();
}

void omni_chord(int root, int quality, bool down, uint64_t now) {
    if (!down) { if (held_root_key == 0xFF) { held_root_key = 0; if (!omni.hold) pad_off(); } return; }
    omni.root = (uint8_t)CLAMP(root, 0, OMNI_ROOTS - 1); omni.quality = (uint8_t)CLAMP(quality, 0, 2);
    held_root_key = 0xFF;                                          /* held by the pointer */
    pad_on(now);
}

void omni_strum(int row, int z, bool down, uint64_t now) {
    uint16_t tag = (uint16_t)(TAG_STRUM | (row << 4) | z);
    if (!down) { synth_note_off_tag(tag); return; }               /* sustaining strum sounds release; one-shots ignore this */
    int note = omni_zone_note(row, z);
    if (note < 12 || note > 120) return;
    int pan = -60 + 120 * z / (OMNI_ZONES - 1);                   /* across the plate, left to right, like a harp */
    synth_note_on_pan((uint8_t)note, (uint8_t)MAX(1, omni.strum_level), omni.strum_override != 0xFF ? omni.strum_override : omni.strum_preset, tag, pan);
    omni.zone_hit_ms[row][z] = now;
    build_chord();
}

void omni_key(uint8_t code, bool down, uint64_t now) {
    /* chord buttons */
    for (int r = 0; r < OMNI_ROOTS; r++) {
        if (code != (uint8_t)omni_root_keys[r]) continue;
        if (down) { omni.root = r; held_root_key = code; pad_on(now); }
        else if (code == held_root_key) { held_root_key = 0; if (!omni.hold) pad_off(); }
        return;
    }
    /* strum plate */
    for (int row = 0; row < 2; row++)
        for (int z = 0; z < OMNI_ZONES; z++) {
            if (omni_zone_keys[row][z] == ' ' || code != (uint8_t)omni_zone_keys[row][z]) continue;
            omni_strum(row, z, down, now);
            return;
        }
    if (!down) return;
    switch (code) {
    case 'q': case 'w': case 'e': {
        int q = code == 'q' ? QUAL_MAJ : code == 'w' ? QUAL_MIN : QUAL_7TH;
        omni.quality = q; build_chord();
        if (omni.chord_on) pad_on(now);
        break; }
    case KEY_SPACE:
        omni.hold = !omni.hold;
        if (omni.hold && !omni.chord_on) pad_on(now);
        if (!omni.hold && !held_root_key) pad_off();
        break;
    case KEY_UP:   if (omni.octave < 2) omni.octave++; break;
    case KEY_DOWN: if (omni.octave > -2) omni.octave--; break;
    }
}

void omni_sound(bool strum, int d, uint64_t now) {
    if (strum) { omni.strum_preset = (uint8_t)synth_preset_next(omni.strum_preset, d); return; }
    omni.pad_preset = (uint8_t)synth_preset_next(omni.pad_preset, d);
    if (omni.chord_on) pad_on(now);
}

void omni_panic(void) {
    synth_all_off(); omni.chord_on = false; omni.hold = false; held_root_key = 0;
}
