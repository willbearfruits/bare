/* The omnichord (see omni.h): chord buttons, the strumplate, the voices, keyboard mode. */
#include "omni.h"
#include "rhythm.h"
#include "synth.h"
#include "keys.h"
#include "libc.h"
#include "platform.h"
#include "midi.h"

struct omni_state omni;
volatile uint32_t omni_chord_word;

const char *const omni_root_names[OMNI_ROOTS] = { "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#" };
const uint8_t omni_root_pc[OMNI_ROOTS]        = {  1,    8,    3,    10,   5,   0,   7,   2,   9,   4,   11,  6  };
const char *const omni_suffix_names[OMNI_SUFFIXES] = { "", "m", "7", "maj7", "m7", "aug", "dim", "sus4", "add9" };
const char *const omni_row_names[OMNI_ROWS] = { "MAJOR", "MINOR", "7TH" };
const char omni_row_keys[OMNI_ROWS][OMNI_ROOTS + 1] = { "1234567890-=", "qwertyuiop[]", "asdfghjkl;'\\" };
const char omni_zone_keys[11] = "zxcvbnm,./";
/* three notes a chord, the root first: "the chords of this unit consist of triads", the fifth left out of the
   four-note ones (7th, maj7, m7, dim) */
static const uint8_t tones[OMNI_SUFFIXES][3] = {
    { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 10 }, { 0, 4, 11 }, { 0, 3, 10 }, { 0, 4, 8 }, { 0, 3, 9 }, { 0, 5, 7 }, { 0, 2, 4 } };

/* the voices: a main sound and a sub sound under it (its manual's table); guitar's and organ's subs an octave up */
const struct omni_voice omni_voices[OMNI_VOICES] = {
    { "omni 1", P_MELLOW, P_TREMOLO, 0 }, { "omni 2", P_MELLOW, P_STRINGS, 0 }, { "harp", P_HARP, P_STRINGS, 0 },
    { "celeste", P_CELESTE, P_STRINGS, 0 }, { "A. piano", P_PIANO, P_STRINGS, 0 }, { "guitar", P_GUITAR, P_STRINGS, 1 },
    { "FM piano", P_FMPIANO, P_PAD, 0 }, { "organ", P_ORGAN, P_STRINGS, 1 }, { "vibes", P_VIBES, P_STRINGS, 0 },
    { "banjo", P_BANJO, P_STRINGS, 0 } };

const char *const omni_kb_top[OMNI_ROOTS] = { "T", "P", "↓", "↑", "BD", "SD", "HT", "FT", "CH", "OH", "CC", "HC" };
const char *const omni_plate_drums[8] = { "BD", "SD", "HT", "LT", "CH", "OH", "CC", "HC" };
static const uint8_t top_drums[8] = { DR_BD, DR_SD, DR_HT, DR_FT, DR_CH, DR_OH, DR_CC, DR_HC };
static const uint8_t plate_drum_kind[8] = { DR_BD, DR_SD, DR_HT, DR_LT, DR_CH, DR_OH, DR_CC, DR_HC };

#define TAG_PAD   0x100
#define TAG_MAIN  0x200                    /* | string */
#define TAG_SUB   0x220                    /* | string */
#define TAG_KEYS  0x240                    /* | key (keyboard mode); omni 1 plays one note, on TAG_KEYS itself */
#define SETTLE_MS 40

static uint32_t press_no, pressed_at[OMNI_ROWS][OMNI_ROOTS];
static uint64_t settle_ms;

static const char *const note_names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
void omni_note_name(int midi, char *out) {
    if (midi < 0) midi = 0;
    const char *n = note_names[midi % 12];
    int oct = midi / 12 - 1, i = 0;
    while (n[i]) { out[i] = n[i]; i++; }
    if (oct < 0) { out[i++] = '-'; oct = -oct; }
    out[i++] = (char)('0' + oct % 10); out[i] = 0;
}
void omni_chord_name(char *out) { snfmt(out, 12, "%s%s", omni_root_names[omni.root], omni_suffix_names[omni.suffix]); }

int omni_chord_tones(uint32_t w, uint8_t pcs[3]) {
    int root = (int)(w & 15) % OMNI_ROOTS, suf = (int)(w >> 4 & 15) % OMNI_SUFFIXES, tr = (int)(w >> 9 & 15) - 6;
    for (int i = 0; i < 3; i++) pcs[i] = (uint8_t)((omni_root_pc[root] + tones[suf][i] + tr + 24) % 12);
    return 3;
}
static uint32_t chord_word(void) { return (uint32_t)omni.root | (uint32_t)omni.suffix << 4 | (uint32_t)(omni.chord_on || omni.hold) << 8 | (uint32_t)(omni.transpose + 6) << 9; }
static void publish(void) {
    uint8_t pcs[3]; omni_chord_tones(chord_word(), pcs);
    for (int i = 0; i < 3; i++) omni.chord_notes[i] = (uint8_t)(54 + (pcs[i] + 6) % 12);   /* in F#3 .. F4, as the manual groups */
    omni.n_notes = 3;
    omni_chord_word = chord_word();
}

/* a string: four groups of the chord's three notes in chord order, each in its octave from F# to F, then the root */
int omni_zone_note(int z) {
    uint8_t pcs[3]; omni_chord_tones(chord_word(), pcs);
    z = CLAMP(z, 0, OMNI_ZONES - 1);
    int g = z < 12 ? z / 3 : 4, pc = pcs[z < 12 ? z % 3 : 0];
    return 54 + 12 * g + (pc + 6) % 12 + 12 * omni.octave;
}

/* ---- which chord the buttons make ---- */
int omni_recognize(const uint16_t held[OMNI_ROWS], int nrow, int nroot, int *suffix) {
    if (!held[0] && !held[1] && !held[2]) return -1;
#define DOWN(row, r) (held[row] >> (((r) + OMNI_ROOTS) % OMNI_ROOTS) & 1)
    int r = nroot;
    if (nrow != ROW_MAJ && !DOWN(ROW_MAJ, r) && DOWN(ROW_MAJ, r + 1)) r = r + 1;   /* the 7th or MINOR left of a MAJ */
    r %= OMNI_ROOTS;
    if (DOWN(ROW_MAJ, r)) {
        bool mi = DOWN(ROW_MIN, r), se = DOWN(ROW_7, r);
        *suffix = mi && se ? SUF_AUG : mi ? SUF_DIM : se ? SUF_MAJ7 : DOWN(ROW_7, r - 1) ? SUF_SUS4 : DOWN(ROW_MIN, r - 1) ? SUF_ADD9 : SUF_MAJ;
        return r;
    }
    r = nroot;
    if (DOWN(ROW_MIN, r) && DOWN(ROW_7, r)) { *suffix = SUF_MIN7; return r; }
    *suffix = nrow == ROW_MIN ? SUF_MIN : nrow == ROW_7 ? SUF_7 : SUF_MAJ;
#undef DOWN
    return r;
}

static void pad_off(void) { synth_note_off_tag(TAG_PAD); midi_omni_chord(0, 0); }
static void pad_on(uint64_t now) {
    synth_note_off_tag(TAG_PAD);
    publish();
    bool held_chord = !omni.autoplay || !rhythm.playing;       /* AUTO with the rhythm running: the rhythm plays it */
    if (held_chord && omni.pad_level)
        for (int i = 0; i < omni.n_notes; i++)
            synth_note_on_pan(omni.chord_notes[i], (uint8_t)MAX(1, omni.pad_level), omni.pad_preset, TAG_PAD, -24 + 24 * i);
    midi_omni_chord(held_chord ? omni.chord_notes : 0, held_chord ? omni.n_notes : 0);
    omni.root_hit_ms = now;
}

/* the chord from the buttons down now: a new one sounds (and SYNC starts the rhythm with it) */
static void apply(uint64_t now) {
    int nrow = -1, nroot = 0; uint32_t best = 0;
    for (int w = 0; w < OMNI_ROWS; w++) for (int r = 0; r < OMNI_ROOTS; r++)
        if (omni.held[w] >> r & 1 && pressed_at[w][r] >= best) { best = pressed_at[w][r]; nrow = w; nroot = r; }
    int suffix = SUF_MAJ, root = nrow < 0 ? -1 : omni_recognize(omni.held, nrow, nroot, &suffix);
    if (root < 0) return;
    bool changed = !omni.chord_on || root != omni.root || suffix != omni.suffix;
    omni.root = (uint8_t)root; omni.suffix = (uint8_t)suffix; omni.chord_on = true;
    if (omni.sync && !rhythm.playing) rhythm_play(true);
    if (changed) pad_on(now); else publish();
}

void omni_button(int row, int root, bool down, uint64_t now) {
    if (row < 0 || row >= OMNI_ROWS || root < 0 || root >= OMNI_ROOTS) return;
    uint16_t bit = (uint16_t)(1u << root);
    if (down) {
        omni.held[row] |= bit; pressed_at[row][root] = ++press_no; settle_ms = 0;
        apply(now);
        return;
    }
    if (!(omni.held[row] & bit)) return;                        /* a release whose press never came here */
    omni.held[row] &= (uint16_t)~bit;
    if (omni.held[0] | omni.held[1] | omni.held[2]) { settle_ms = now ? now : 1; return; }   /* the rest let go too? wait a moment */
    settle_ms = 0;
    if (!omni.hold) { omni.chord_on = false; pad_off(); publish(); }
}

void omni_work(uint64_t now) {
    if (settle_ms && now - settle_ms >= SETTLE_MS) { settle_ms = 0; if (omni.held[0] | omni.held[1] | omni.held[2]) apply(now); }
}

/* ---- the strings ---- */
static int ring(void) { return 120 + omni.sustain * 30; }        /* SUSTAIN: 0.12 .. 3.9 s */
void omni_strum(int z, bool down, uint64_t now) {
    if (z < 0 || z >= OMNI_ZONES) return;
    if (omni.keyboard) { if (down) omni.zone_hit_ms[z] = now; rhythm_pad(plate_drum_kind[MIN(z * 7 / OMNI_ZONES, 6)], down); return; }   /* the drum set */
    uint16_t tm = (uint16_t)(TAG_MAIN | z), ts = (uint16_t)(TAG_SUB | z);
    int note = omni_zone_note(z);
    if (!down) { synth_note_off_tag(tm); synth_note_off_tag(ts); midi_omni_string(note, 0, 0); return; }
    if (note < 12 || note > 120) return;
    int pan = -60 + 120 * z / (OMNI_ZONES - 1);                    /* across the plate, left to right, like a harp */
    bool custom = omni.strum_override != 0xFF || omni.voice >= OMNI_VOICES;
    const struct omni_voice *v = &omni_voices[MIN(omni.voice, OMNI_VOICES - 1)];
    int main = omni.strum_override != 0xFF ? omni.strum_override : custom ? omni.strum_preset : v->main;
    if (omni.main_level) synth_note_on_ring((uint8_t)note, omni.main_level, (uint8_t)main, tm, pan, ring());
    if (!custom && omni.sub_level) {
        int sn = note + 12 * v->sub_oct;
        if (sn <= 120) synth_note_on_ring((uint8_t)sn, omni.sub_level, v->sub, ts, pan, ring());
    }
    midi_omni_string(note, omni.main_level, custom ? 0 : omni.sub_level);
    omni.zone_hit_ms[z] = now;
}

void omni_off(uint64_t now) {
    (void)now;
    synth_tag_cut(TAG_PAD);                                         /* at once, not with their release */
    for (int z = 0; z < OMNI_ZONES; z++) { synth_tag_cut((uint16_t)(TAG_MAIN | z)); synth_tag_cut((uint16_t)(TAG_SUB | z)); }
    for (int k = 0; k < 32; k++) synth_tag_cut((uint16_t)(TAG_KEYS | k));
    for (int k = 3; k <= 10; k++) synth_tag_cut((uint16_t)(0x400 | k));    /* the accompaniment's bass and chord */
    omni.chord_on = false; settle_ms = 0;
    memset(omni.held, 0, sizeof omni.held);
    if (omni.sync && rhythm.playing) rhythm_play(false);          /* with SYNC START the rhythm stops too */
    midi_omni_chord(0, 0);
    publish();
}

/* ---- keyboard mode: the 7th row the white keys, the MINOR row the black ones, the MAJOR row T P ↓ ↑ and drums ---- */
static const int8_t black[OMNI_ROOTS] = { -1, 1, 3, -1, 6, 8, 10, -1, 13, 15, -1, 18 };   /* q w e r t y u i o p [ ] */
static const int8_t white[OMNI_ROOTS] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19 };     /* a … ' and Enter: C … G */
int omni_kb_note(int row, int col) {
    if (col < 0 || col >= OMNI_ROOTS) return -1;
    int k = row == ROW_7 ? white[col] : row == ROW_MIN ? black[col] : -1;
    return k < 0 ? -1 : 60 + 12 * omni.kb_octave + k;
}
static uint8_t mono[16], n_mono;                                   /* omni 1 plays one note: the last key held */
static bool mono_voice(void) { return omni.voice == 0; }
static void kb_note(int row, int col, bool down) {
    int note = omni_kb_note(row, col);
    if (note < 0) return;
    const struct omni_voice *v = &omni_voices[MIN(omni.voice, OMNI_VOICES - 1)];
    int main = omni.voice >= OMNI_VOICES ? omni.strum_preset : v->main;
    if (mono_voice()) {
        int i = 0; while (i < n_mono && mono[i] != note) i++;
        if (i < n_mono) { memmove(mono + i, mono + i + 1, (size_t)(n_mono - i - 1)); n_mono--; }
        if (down && n_mono < (int)sizeof mono) mono[n_mono++] = (uint8_t)note;
        synth_note_off_tag(TAG_KEYS);
        if (n_mono) synth_note_on_ring(mono[n_mono - 1], MAX(1, omni.main_level), (uint8_t)main, TAG_KEYS, 0, ring());
        midi_omni_key(note, down ? omni.main_level : 0);
        return;
    }
    uint16_t tag = (uint16_t)(TAG_KEYS | (row == ROW_7 ? 0 : 16) | col);
    if (!down) { synth_note_off_tag(tag); midi_omni_key(note, 0); return; }
    synth_note_on_ring((uint8_t)note, MAX(1, omni.main_level), (uint8_t)main, tag, 0, ring());
    midi_omni_key(note, omni.main_level);
}
static bool t_held, p_held;
static void kb_top(int col, bool down, uint64_t now) {
    (void)now;
    if (col == 0) { t_held = down; return; }
    if (col == 1) { p_held = down; return; }
    if (col == 2 || col == 3) {
        if (!down) return;
        int d = col == 3 ? 1 : -1;
        if (t_held) omni_set_transpose(omni.transpose + d);
        else if (p_held) omni_set_tune(omni.tune + d);
        else omni.kb_octave = (int8_t)CLAMP(omni.kb_octave + d, -1, 1);
        publish();
        return;
    }
    if (down) rhythm_drum(top_drums[col - 4], 110);                /* a drum the moment it is pressed */
}

void omni_key(uint8_t code, bool down, uint64_t now) {
    for (int w = 0; w < OMNI_ROWS; w++)
        for (int r = 0; r < OMNI_ROOTS; r++)
            if (code == (uint8_t)omni_row_keys[w][r] || (w == ROW_7 && r == 11 && code == KEY_ENTER)) {
                if (!omni.keyboard) { omni_button(w, r, down, now); return; }
                if (down) omni.held[w] |= (uint16_t)(1u << r); else omni.held[w] &= (uint16_t)~(1u << r);   /* lit on the page */
                if (w == ROW_MAJ) kb_top(r, down, now); else kb_note(w, r, down);
                return;
            }
    for (int z = 0; omni_zone_keys[z]; z++) if (code == (uint8_t)omni_zone_keys[z]) {
        if (omni.keyboard) { if (z < 7) { if (down) omni.zone_hit_ms[(z * OMNI_ZONES + 6) / 7] = now; rhythm_pad(plate_drum_kind[z], down); } }
        else omni_strum(z, down, now);
        return;
    }
    if (code == KEY_BACKSPACE) {                                   /* INSTANT OFF (in keyboard mode: hand claps) */
        if (omni.keyboard) rhythm_pad(DR_HC, down);
        else if (down) omni_off(now);
    }
}

void omni_set_hold(bool on, uint64_t now) {
    omni.hold = on;
    if (on && !omni.chord_on) { omni.chord_on = true; pad_on(now); }       /* HOLD on with nothing down: the last chord again */
    if (!on && !(omni.held[0] | omni.held[1] | omni.held[2])) { omni.chord_on = false; pad_off(); }
    publish();
}
void omni_set_auto(bool on, uint64_t now) { omni.autoplay = on; if (omni.chord_on || omni.hold) pad_on(now); }

void omni_set_transpose(int t) { omni.transpose = (int8_t)CLAMP(t, -6, 6); publish(); }

void omni_set_voice(int v) { omni.voice = (uint8_t)CLAMP(v, 0, OMNI_CUSTOM); n_mono = 0; synth_note_off_tag(TAG_KEYS); }
void omni_set_tune(int hz) {
    static const int16_t q8[13] = { -61, -51, -40, -30, -20, -10, 0, 10, 20, 30, 40, 50, 60 };   /* 1200·log2((440+hz)/440) cents */
    omni.tune = (int8_t)CLAMP(hz, -6, 6);
    synth_tune_q8 = q8[omni.tune + 6];
}

void omni_init(void) {
    memset(&omni, 0, sizeof omni);
    omni.root = 5; omni.suffix = SUF_MAJ;                          /* C major */
    omni.pad_preset = P_ORGAN; omni.strum_preset = P_PLUCK; omni.strum_override = 0xFF;
    omni.pad_level = 100; omni.main_level = 110; omni.sub_level = 64; omni.sustain = 60;
    omni_set_tune(0);
    publish();
}

void omni_sound(bool strum, int d, uint64_t now) {
    if (strum) { omni.strum_preset = (uint8_t)synth_preset_next(omni.strum_preset, d); omni_set_voice(OMNI_CUSTOM); return; }
    omni.pad_preset = (uint8_t)synth_preset_next(omni.pad_preset, d);
    if (omni.chord_on || omni.hold) pad_on(now);
}

void omni_panic(void) {
    synth_all_off(); omni.chord_on = false; omni.hold = false; settle_ms = 0; n_mono = 0; t_held = p_held = false;
    memset(omni.held, 0, sizeof omni.held);
    rhythm_pads_clear();
    midi_omni_chord(0, 0);
    publish();
}
