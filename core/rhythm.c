#include "rhythm.h"
#include "link.h"
#include "omni.h"
#include "seq.h"
#include "synth.h"
#include "sieve.h"
#include "libc.h"

#define TAG_RHYTHM 0x400                  /* | lane */

struct rhythm_state rhythm = { false, true, 0, -1, 100, 0 };
const char *const rhythm_names[RHYTHM_PATTERNS] = { "ROCK", "POP", "DISCO", "16 BEAT", "SWING", "WALTZ", "BOSSA", "REGGAE", "SIEVE" };

/* One bar per pattern, a character per step: kick, snare, hat ('X' accented, 'x' normal), then the bass as chord
   degrees: 1 root, 3 third, 5 fifth, 8 octave. Steps are sixteenths, except SWING's triplet eighths (3 a beat);
   WALTZ is a 3/4 bar of sixteenths. */
static const struct { const char *kick, *snare, *hat, *bass; uint8_t per_beat; } pat[RHYTHM_PATTERNS - 1] = {
    { "X.......x.x.....", "....X.......X...", "x.x.x.x.x.x.x.x.", "1.......1.5.....", 4 },   /* ROCK */
    { "X.....x.x.......", "....X.......X...", "x.x.x.x.x.x.x.x.", "1.....1.5...3...", 4 },   /* POP */
    { "X...X...X...X...", "....X.......X...", "..x...x...x...x.", "1.8.1.8.1.8.1.8.", 4 },   /* DISCO */
    { "X..x..x...X..x..", "....X.......X..x", "xxxxxxxxxxxxxxxx", "1..1..5...1..8..", 4 },   /* 16 BEAT */
    { "X.....x.....",     "...x.....x..",     "x..x.xx..x.x",     "1..3..5..8..",     3 },   /* SWING */
    { "X...........",     "....x...x...",     "....x...x...",     "1.......5...",     4 },   /* WALTZ */
    { "X..x..x.x..x..x.", "x..x...x..x.x...", "x.xxx.xxx.xxx.xx", "1..5..5.1..5..5.", 4 },   /* BOSSA */
    { "........X.......", "........X.......", "..x...x...x...x.", "1.....1.5.....5.", 4 },   /* REGGAE (one drop) */
};

static uint32_t rate = 48000, step_q16, step_left_q16, bass_left;
static uint32_t step_bpm_q16; static uint8_t step_pattern = 0xFF;
static uint32_t steps_done;                           /* steps since the start (Link's phase) */
static bool bass_on;

void rhythm_init(uint32_t r) { rate = r ? r : 48000; step_bpm_q16 = 0; }
static bool sieved(void) { return rhythm.pattern >= RHYTHM_SIEVE; }
int  rhythm_steps(void) { return sieved() ? 16 : (int)strlen(pat[rhythm.pattern].kick); }
int  rhythm_beat_steps(void) { return sieved() ? 4 : pat[rhythm.pattern].per_beat; }

/* the SIEVE pattern: a lane plays where its sieve has the step, counted from the start (a sieve longer than a bar goes
   on across the bars); on the beat, accented */
static char sieve_step(int lane, uint32_t n) { return sieve_has(&sieves[lane], (int32_t)n) ? (n % 4 ? 'x' : 'X') : '.'; }

bool rhythm_hit(int lane, int step) {
    if (step < 0 || step >= rhythm_steps()) return false;
    if (sieved()) return sieve_step(lane, (rhythm.playing && rhythm.pos >= 0 ? steps_done - 1 - (uint32_t)rhythm.pos : 0) + (uint32_t)step) != '.';
    const char *s = lane == 0 ? pat[rhythm.pattern].kick : lane == 1 ? pat[rhythm.pattern].snare
                  : lane == 2 ? pat[rhythm.pattern].hat : pat[rhythm.pattern].bass;
    return s[step] != '.';
}

static void bass_off(void) { if (bass_on) { synth_note_off_tag(TAG_RHYTHM | 3); bass_on = false; } }

void rhythm_play(bool on) {
    if (on == rhythm.playing) return;
    if (on && link_rhythm_request()) return;                          /* Link: on the session's next bar */
    rhythm.playing = on;
    bass_off();
    if (!on) return;
    rhythm.pos = -1; steps_done = 0;
    /* with the sequencer running, fall in on its next step so the two stay together; alone, start at once */
    step_left_q16 = seq.playing ? seq_step_left_q16() : 0;
    if (seq.playing && seq.pos >= 0) rhythm.pos = (int16_t)(seq.pos % rhythm_steps());
}

/* a step: drums from the pattern, the bass from the chord the buttons hold */
static void step(void) {
    rhythm.pos = (int16_t)((rhythm.pos + 1) % rhythm_steps());
    bool sv = sieved();
    const char *lanes[3] = { 0, 0, 0 };
    if (!sv) { lanes[0] = pat[rhythm.pattern].kick; lanes[1] = pat[rhythm.pattern].snare; lanes[2] = pat[rhythm.pattern].hat; }
    static const uint8_t presets[3] = { P_KICK, P_SNARE, P_HAT }, notes[3] = { 36, 38, 42 };
    for (int l = 0; l < 3; l++) {
        char c = sv ? sieve_step(l, steps_done) : lanes[l][rhythm.pos];
        if (c == '.' || (rhythm.mute >> l & 1)) continue;
        int vel = rhythm.level * (c == 'X' ? 127 : 96) / 127;
        synth_note_on(notes[l], (uint8_t)MAX(1, vel), presets[l], (uint16_t)(TAG_RHYTHM | l));
    }
    char b = sv ? (sieve_step(3, steps_done) != '.' ? '1' : '.') : pat[rhythm.pattern].bass[rhythm.pos];
    if (b == '.' || !rhythm.bass || (rhythm.mute & 8)) return;
    bass_off();
    if (!omni.chord_on && !omni.hold) return;                  /* the auto bass plays only while a chord is held */
    int root = omni.n_notes ? omni.chord_notes[0] % 12 : 0;
    int third = omni.n_notes > 1 ? (omni.chord_notes[1] - omni.chord_notes[0] + 120) % 12 : 4;
    int deg = b == '1' ? 0 : b == '3' ? third : b == '5' ? 7 : 12;
    synth_note_on((uint8_t)(36 + root + deg), (uint8_t)MAX(1, rhythm.level * 110 / 127), P_SAWBASS, TAG_RHYTHM | 3);
    bass_on = true;
    bass_left = (step_q16 >> 16) * (uint32_t)(rhythm_beat_steps() - 1);   /* a little shorter than the beat */
}

void rhythm_run_events(void) {
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    if (bpm_q16 != step_bpm_q16 || rhythm.pattern != step_pattern) {    /* the step length: a beat over its steps */
        step_bpm_q16 = bpm_q16; step_pattern = rhythm.pattern;
        step_q16 = (uint32_t)(((uint64_t)rate * 60 << 32) / ((uint64_t)step_bpm_q16 * (uint32_t)rhythm_beat_steps()));
        if (rhythm.pos >= rhythm_steps()) rhythm.pos = -1;
    }
    if (bass_on && bass_left == 0) bass_off();
    if (rhythm.playing && step_left_q16 < 65536) { step(); steps_done++; step_left_q16 += step_q16; }
}
uint32_t rhythm_next_event(void) {
    uint32_t n = 0xFFFFFFFFu;
    if (rhythm.playing) n = step_left_q16 >> 16;
    if (bass_on && bass_left < n) n = bass_left;
    return n;
}
/* ---- Link (the audio side) ---- */
void rhythm_start_in(uint32_t frames) { bass_off(); rhythm.playing = true; rhythm.pos = -1; steps_done = 0; step_left_q16 = frames << 16; }
int64_t rhythm_steps_q16(void) { return (int64_t)steps_done * 65536 - (step_q16 ? (int64_t)(((uint64_t)step_left_q16 << 16) / step_q16) : 0); }
uint32_t rhythm_step_q16(void) { return step_q16; }
void rhythm_nudge(int32_t q16) { step_left_q16 = q16 < 0 && (uint32_t)-q16 > step_left_q16 ? 0 : step_left_q16 + (uint32_t)q16; }

void rhythm_advance(uint32_t frames) {
    if (bass_on) bass_left = bass_left > frames ? bass_left - frames : 0;
    if (rhythm.playing) step_left_q16 = step_left_q16 > (frames << 16) ? step_left_q16 - (frames << 16) : 0;
}
