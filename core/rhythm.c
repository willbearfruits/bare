/* The omnichord's rhythm section (see rhythm.h). */
#include "rhythm.h"
#include "link.h"
#include "omni.h"
#include "seq.h"
#include "synth.h"
#include "sieve.h"
#include "midi.h"
#include "platform.h"
#include "libc.h"

#define TAG_RHYTHM 0x400                  /* | lane: 0 kick, 1 snare, 2 hat, 3 bass, 4 percussion; 8..10 the chord's notes */

struct rhythm_state rhythm = { false, false, 0, 0xFF, -1, 100, 0, 0 };
const char *const rhythm_names[RHYTHM_PATTERNS] = { "ROCK 1", "POP", "DISCO", "16 BEAT", "SWING", "WALTZ", "BOSSANOVA", "REGGAE",
                                                    "SIEVE", "ROCK 2", "SLOW ROCK", "COUNTRY", "HIP HOP", "FUNK" };
const uint8_t rhythm_sets[RHYTHM_SETS][5] = { { 0, 9, 10, 11, 4 }, { 2, 12, 13, 6, 5 }, { 1, 3, 7, 8, 0xFF } };
const char *const rhythm_set_names[RHYTHM_SETS] = { "SET 1", "SET 2", "BARE!" };

/* One bar per pattern, a character per step. Drums: 'X' accented, 'x' normal. Percussion: o open hat, h l f high, low
   and floor tom, y crash, c hand claps, r rim, b tambourine, g conga, m maracas, k claves (capitals accented). The bass
   in degrees of the held chord: 1 root, 3 third, 5 fifth, 6 sixth, 7 seventh, 8 octave; the chord 'X' 'x' a stab.
   Steps are sixteenths, except the triplet eighths of SWING and SLOW ROCK (3 a beat); WALTZ is a 3/4 bar. */
struct pattern { const char *kick, *snare, *hat, *perc, *bass, *chord; uint8_t per_beat; bool k808; };
static const struct pattern pat[RHYTHM_PATTERNS] = {
    /* ROCK 1 */    { "X.......X.X.....", "....X.......X...", "x.x.x.x.x.x.x.x.", "y...............", "1.......1.5.....", "X.....x.X.....x.", 4 },
    /* POP */       { "X.....x.x.......", "....X.......X...", "x.x.x.x.x.x.x.x.", "................", "1.....1.5...3...", "x...x...x...x...", 4 },
    /* DISCO */     { "X...X...X...X...", "....X.......X...", "x...x...x...x...", "..o...o...o...o.", "1.8.1.8.1.8.1.8.", "..x...x...x...x.", 4 },
    /* 16 BEAT */   { "X..x..x...X..x..", "....X.......X..x", "xxxxxxxxxxxxxxxx", "................", "1..1..5...1..8..", "X.....x...x.....", 4 },
    /* SWING */     { "X.....x.....",     "...x.....x..",     "x..x.xx..x.x",     "............",     "1..3..5..6..",     "...x.....x..",     3 },
    /* WALTZ */     { "X...........",     "....x...x...",     "....x...x...",     "............",     "1.......5...",     "....x...x...",     4 },
    /* BOSSANOVA */ { "X..x..x.x..x..x.", "................", "x.xxx.xxx.xxx.xx", "r..r...r..r.r...", "1..5..5.1..5..5.", "X..x..x...x..x..", 4 },
    /* REGGAE */    { "........X.......", "........X.......", "..x...x...x...x.", "................", "1.....1.5.....5.", "..x...x...x...x.", 4 },
    /* SIEVE */     { "", "", "", "", "", "", 4 },
    /* ROCK 2 */    { "X..x..x...X.x...", "....X.......X...", "x.x.x.x.x.x.x.x.", "m.m.m.m.m.m.m.m.", "1..1..1...5.1...", "X..x..x...x.x...", 4 },
    /* SLOW ROCK */ { "X.....X.x...",     "...X.....X..",     "xxxxxxxxxxxx",     "y...........",     "1..3..5..3..",     "x.xx.xx.xx.x",     3 },
    /* COUNTRY */   { "X.......X.......", "....X.......X...", "..x...x...x...x.", "................", "1.......5.......", "....X.......X...", 4 },
    /* HIP HOP */   { "X.......x..X....", "................", "xxxxxxxxx.x.xxx.", "....C.......C...", "1.......1..1....", "X...............", 4, true },
    /* FUNK */      { "X..x..x...X..x..", "....X..x.x..X...", "xxXxxxXxxxXxxxXx", "................", "1..8.1..3..5.8..", "..x.x...x.x..x..", 4 },
};

/* the kit: each drum's sound (the OM-108's, then the OM-84's), the note it is played at, and General MIDI's */
static const struct { uint8_t preset, classic, note, gm; } kit[DRUMS] = {
    [DR_BD] = { P_KICK2, P_KICK, 36, 36 },       [DR_SD] = { P_SNARE2, P_SNARE, 38, 38 },   [DR_CH] = { P_HAT2, P_HAT, 42, 42 },
    [DR_OH] = { P_OHAT, P_HAT, 46, 46 },         [DR_HT] = { P_TOMHI, P_KICK, 52, 48 },     [DR_LT] = { P_TOMLO, P_KICK, 47, 45 },
    [DR_FT] = { P_FTOM, P_KICK, 43, 41 },        [DR_CC] = { P_CRASH, P_HAT, 49, 49 },      [DR_HC] = { P_CLAP, P_SNARE, 39, 39 },
    [DR_RIM] = { P_RIM, P_SNARE, 76, 37 },       [DR_TAMB] = { P_TAMB, P_HAT, 54, 54 },     [DR_CONGA] = { P_CONGA, P_KICK, 62, 62 },
    [DR_MARACAS] = { P_MARACAS, P_HAT, 70, 70 }, [DR_CLAVES] = { P_CLAVES, P_HAT, 96, 75 }, [DR_BD808] = { P_KICK808, P_KICK, 33, 36 },
};
static int perc_kind(char c) {
    switch (c | 0x20) {
    case 'o': return DR_OH; case 'h': return DR_HT; case 'l': return DR_LT; case 'f': return DR_FT; case 'y': return DR_CC;
    case 'c': return DR_HC; case 'r': return DR_RIM; case 'b': return DR_TAMB; case 'g': return DR_CONGA; case 'm': return DR_MARACAS;
    case 'k': return DR_CLAVES;
    }
    return -1;
}

static uint32_t rate = 48000, step_q16, step_left_q16, bass_left, chord_left;
static uint32_t step_bpm_q16; static uint8_t step_pattern = 0xFF;
static uint32_t steps_done;                           /* steps since the start (Link's phase) */
static bool bass_on, chord_on;
static int bass_note = -1;

void rhythm_init(uint32_t r) { rate = r ? r : 48000; step_bpm_q16 = 0; }
static bool sieved(void) { return rhythm.pattern == RHYTHM_SIEVE; }
int  rhythm_steps(void) { return sieved() ? 16 : (int)strlen(pat[rhythm.pattern % RHYTHM_PATTERNS].kick); }
int  rhythm_beat_steps(void) { return sieved() ? 4 : pat[rhythm.pattern % RHYTHM_PATTERNS].per_beat; }

/* the SIEVE pattern: a lane plays where its sieve has the step, counted from the start (a sieve longer than a bar goes
   on across the bars); on the beat, accented */
static char sieve_step(int lane, uint32_t n) { return sieve_has(&sieves[lane], (int32_t)n) ? (n % 4 ? 'x' : 'X') : '.'; }
static const char *lane_of(const struct pattern *p, int lane) {
    return lane == 0 ? p->kick : lane == 1 ? p->snare : lane == 2 ? p->hat : lane == 3 ? p->bass : lane == 4 ? p->perc : p->chord;
}
bool rhythm_hit(int lane, int step) {
    if (step < 0 || step >= rhythm_steps() || lane < 0 || lane > 5) return false;
    if (sieved()) return lane < 4 && sieve_step(lane, (rhythm.playing && rhythm.pos >= 0 ? steps_done - 1 - (uint32_t)rhythm.pos : 0) + (uint32_t)step) != '.';
    const char *s = lane_of(&pat[rhythm.pattern % RHYTHM_PATTERNS], lane);
    return step < (int)strlen(s) && s[step] != '.';
}

void rhythm_drum(int kind, int vel) {
    if (kind < 0 || kind >= DRUMS) return;
    int lane = kind == DR_BD || kind == DR_BD808 ? 0 : kind == DR_SD || kind == DR_HC ? 1 : kind == DR_CH || kind == DR_OH ? 2 : 4;
    synth_note_on(kit[kind].note, (uint8_t)CLAMP(vel, 1, 127), rhythm.classic ? kit[kind].classic : kit[kind].preset, (uint16_t)(TAG_RHYTHM | lane));
    midi_omni_drum(kit[kind].gm, vel);
}
void rhythm_pad(int kind, bool down) {
    if (kind < 0 || kind >= DRUMS) return;
    uint16_t bit = (uint16_t)(1u << kind);
    if (!down) { rhythm.pads &= (uint16_t)~bit; return; }
    if (!rhythm.playing) { rhythm_drum(kind, rhythm.level); return; }   /* stopped: at once */
    rhythm.pads |= bit;                                              /* running: on the steps, while it is held */
}
void rhythm_pads_clear(void) { rhythm.pads = 0; }

static void bass_off(void) {
    if (bass_on) { synth_note_off_tag(TAG_RHYTHM | 3); bass_on = false; }
    if (bass_note >= 0) { midi_omni_bass(bass_note, 0); bass_note = -1; }
}
static void chord_off(void) { if (chord_on) { for (int i = 0; i < 3; i++) synth_note_off_tag((uint16_t)(TAG_RHYTHM | (8 + i))); chord_on = false; midi_omni_chord(0, 0); } }

void rhythm_select(int p) {
    p = ((p % RHYTHM_PATTERNS) + RHYTHM_PATTERNS) % RHYTHM_PATTERNS;
    if (rhythm.playing && p != rhythm.pattern) rhythm.next = (uint8_t)p;   /* on the next bar */
    else { rhythm.pattern = (uint8_t)p; rhythm.next = 0xFF; }
}

void rhythm_play(bool on) {
    if (on == rhythm.playing) return;
    if (on && link_rhythm_request()) return;                          /* Link: on the session's next bar */
    uint32_t st = plat_irq_save();
    rhythm.playing = on;
    bass_off(); chord_off();
    if (rhythm.next != 0xFF) { rhythm.pattern = rhythm.next; rhythm.next = 0xFF; }
    if (on) {
        rhythm.pos = -1; steps_done = 0;
        /* with the sequencer running, fall in on its next step so the two stay together; alone, start at once */
        step_left_q16 = seq.playing ? seq_step_left_q16() : 0;
        if (seq.playing && seq.pos >= 0) rhythm.pos = (int16_t)(seq.pos % rhythm_steps());
    } else rhythm.pads = 0;
    plat_irq_restore(st);
    if (!seq.playing) midi_transport(on);
}

/* the held chord's note for a bass degree, in the bass octave (C2 up) */
static int degree(uint32_t w, char d) {
    uint8_t pcs[3]; omni_chord_tones(w, pcs);
    int suf = (int)(w >> 4 & 15), root = pcs[0];
    int third = suf == SUF_SUS4 ? 5 : suf == SUF_ADD9 ? 4 : (pcs[1] - root + 12) % 12;
    int fifth = suf == SUF_AUG ? 8 : suf == SUF_DIM ? 6 : 7;
    int seventh = suf == SUF_MAJ7 ? 11 : suf == SUF_DIM ? 9 : 10;
    int off = d == '3' ? third : d == '5' ? fifth : d == '6' ? 9 : d == '7' ? seventh : d == '8' ? 12 : 0;
    return 36 + root + off;
}

/* a step: the drums, the percussion, and with CHORD AUTO the bass and the chord the buttons hold */
static void step(void) {
    int n = rhythm_steps();
    rhythm.pos = (int16_t)((rhythm.pos + 1) % n);
    if (rhythm.pos == 0 && rhythm.next != 0xFF) {                     /* a new pattern: on the bar */
        rhythm.pattern = rhythm.next; rhythm.next = 0xFF;
        n = rhythm_steps(); step_pattern = 0xFF;
    }
    bool sv = sieved();
    const struct pattern *p = &pat[rhythm.pattern % RHYTHM_PATTERNS];
    for (int l = 0; l < 3; l++) {
        char c = sv ? sieve_step(l, steps_done) : lane_of(p, l)[rhythm.pos];
        if (c == '.' || (rhythm.mute >> l & 1)) continue;
        rhythm_drum(l == 0 ? (p->k808 ? DR_BD808 : DR_BD) : l == 1 ? DR_SD : DR_CH, rhythm.level * (c == 'X' ? 127 : 96) / 127);
    }
    if (!sv && !(rhythm.mute & 16)) {
        char c = p->perc[rhythm.pos];
        if (c != '.') rhythm_drum(perc_kind(c), rhythm.level * (c >= 'A' && c <= 'Z' ? 127 : 90) / 127);
    }
    uint16_t pads = rhythm.pads;                                      /* keyboard mode's held drums, on every step */
    for (int k = 0; pads; k++, pads >>= 1) if (pads & 1) rhythm_drum(k, rhythm.level);
    /* the accompaniment: with CHORD AUTO, while a chord is held */
    uint32_t w = omni_chord_word;
    bool follow = omni.autoplay && (w >> 8 & 1);
    char b = sv ? (sieve_step(3, steps_done) != '.' ? '1' : '.') : p->bass[rhythm.pos];
    if (b != '.' && !(rhythm.mute & 8)) {
        bass_off();
        if (follow) {
            int note = degree(w, b);
            synth_note_on((uint8_t)note, (uint8_t)MAX(1, omni.pad_level * 110 / 127), P_SAWBASS, TAG_RHYTHM | 3);
            midi_omni_bass(note, omni.pad_level);
            bass_on = true; bass_note = note;
            bass_left = (step_q16 >> 16) * (uint32_t)(rhythm_beat_steps() - 1);   /* a little shorter than the beat */
        }
    }
    char c = sv ? '.' : p->chord[rhythm.pos];
    if (c != '.' && !(rhythm.mute & 32)) {
        chord_off();
        if (follow && omni.pad_level) {
            uint8_t pcs[3], notes[3]; omni_chord_tones(w, pcs);
            int vel = omni.pad_level * (c == 'X' ? 110 : 88) / 127;
            for (int i = 0; i < 3; i++) {
                notes[i] = (uint8_t)(54 + (pcs[i] + 6) % 12);
                synth_note_on_pan(notes[i], (uint8_t)MAX(1, vel), omni.pad_preset, (uint16_t)(TAG_RHYTHM | (8 + i)), -24 + 24 * i);
            }
            midi_omni_chord(notes, 3);
            chord_on = true;
            chord_left = (step_q16 >> 16) * (uint32_t)MAX(1, rhythm_beat_steps() - 1);
        }
    }
}

void rhythm_run_events(void) {
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    if (bpm_q16 != step_bpm_q16 || rhythm.pattern != step_pattern) {    /* the step length: a beat over its steps */
        step_bpm_q16 = bpm_q16; step_pattern = rhythm.pattern;
        step_q16 = (uint32_t)(((uint64_t)rate * 60 << 32) / ((uint64_t)step_bpm_q16 * (uint32_t)rhythm_beat_steps()));
        if (rhythm.pos >= rhythm_steps()) rhythm.pos = -1;
    }
    if (bass_on && bass_left == 0) bass_off();
    if (chord_on && chord_left == 0) chord_off();
    if (rhythm.playing && step_left_q16 < 65536) { step(); steps_done++; step_left_q16 += step_q16; }
}
uint32_t rhythm_next_event(void) {
    uint32_t n = 0xFFFFFFFFu;
    if (rhythm.playing) n = step_left_q16 >> 16;
    if (bass_on && bass_left < n) n = bass_left;
    if (chord_on && chord_left < n) n = chord_left;
    return n;
}
/* ---- Link (the audio side) ---- */
void rhythm_start_in(uint32_t frames) { bass_off(); chord_off(); rhythm.playing = true; rhythm.pos = -1; steps_done = 0; step_left_q16 = frames << 16; }
int64_t rhythm_steps_q16(void) { return (int64_t)steps_done * 65536 - (step_q16 ? (int64_t)(((uint64_t)step_left_q16 << 16) / step_q16) : 0); }
uint32_t rhythm_step_q16(void) { return step_q16; }
void rhythm_nudge(int32_t q16) { step_left_q16 = q16 < 0 && (uint32_t)-q16 > step_left_q16 ? 0 : step_left_q16 + (uint32_t)q16; }

void rhythm_advance(uint32_t frames) {
    if (bass_on) bass_left = bass_left > frames ? bass_left - frames : 0;
    if (chord_on) chord_left = chord_left > frames ? chord_left - frames : 0;
    if (rhythm.playing) step_left_q16 = step_left_q16 > (frames << 16) ? step_left_q16 - (frames << 16) : 0;
}
