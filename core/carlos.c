/* CARLOS's voice (see carlos.h): the MOOG preset shaped by the knobs, microtonal pitches as a note and a bend, and in
   mono a glide worked out on the audio side, block by block. */
#include "carlos.h"
#include "synth.h"
#include "platform.h"
#include "libc.h"

struct carlos_state carlos;
const char *const carlos_scale_names[CARLOS_SCALES] = { "equal (12)", "alpha", "beta", "gamma" };
const int32_t carlos_step_c100[CARLOS_SCALES] = { 10000, 7800, 6380, 3510 };
const char *const carlos_wave_names[3] = { "saw", "square", "triangle" };

#define TAG_POLY  0xC00                    /* | step (0 .. 32); the chord comparison uses 0xC50 | id */
#define TAG_MONO  0xC40
static uint8_t stack[CARLOS_KEYS + 1]; static int nstack;   /* mono: the steps held, the last on top */
static int32_t sounding_poly[CARLOS_KEYS];                  /* poly: each step's pitch while it sounds, -1 not */
/* the glide, shared with the audio side: where the voice's pitch is, where it goes, the note it was started on */
static volatile int32_t from_q8, to_q8, cur_q8; static volatile uint32_t glide_left, glide_len; static volatile int base_note = -1;
static bool ribbon_on;

void carlos_apply(void) {
    struct preset p = { "MOOG", (uint8_t)(carlos.wave == 1 ? WAVE_PULSE : carlos.wave == 2 ? WAVE_TRI : WAVE_SAW), 50,
                        (uint16_t)(2 + carlos.attack * carlos.attack / 5), (uint16_t)(20 + carlos.decay * carlos.decay / 4),
                        (uint16_t)(10 + carlos.release * carlos.release / 4), carlos.sustain, (uint8_t)(carlos.level * 70 / 100),
                        carlos.cutoff, carlos.reso, carlos.contour, 0, 0, 0, (uint8_t)(carlos.wave == 2 ? 0 : 10), false, 0, 0 };
    synth_set_preset(P_MOOG, &p);
}

void carlos_init(void) {
    carlos = (struct carlos_state){ CS_ALPHA, 3, false, 20, 0, 72, 45, 30, 5, 40, 70, 25, 80, true };
    nstack = 0; for (int i = 0; i < CARLOS_KEYS; i++) sounding_poly[i] = -1;
    carlos_apply();
}

int32_t carlos_pitch_q8(int step) {
    int32_t base = (carlos.octave + 1) * 12 * 256;
    return base + (int32_t)((int64_t)step * carlos_step_c100[carlos.scale % CARLOS_SCALES] * 256 / 10000);
}

/* a pitch on a voice of its own: the nearest note below, and a bend for the rest */
static void voice_on(uint16_t tag, int32_t q8, int vel) {
    int note = CLAMP(q8 >> 8, 0, 127);
    synth_note_on(note, (uint8_t)vel, P_MOOG, tag);
    synth_tag_bend(tag, q8 - note * 256);
}

static void mono_to(int32_t q8, bool legato) {
    uint32_t st = plat_irq_save();
    if (!legato || base_note < 0) {                            /* a new note: struck where it is */
        base_note = CLAMP(q8 >> 8, 0, 127); from_q8 = to_q8 = cur_q8 = q8; glide_left = 0;
        plat_irq_restore(st);
        synth_note_on((uint8_t)base_note, 110, P_MOOG, TAG_MONO);
        synth_tag_bend(TAG_MONO, q8 - base_note * 256);
        return;
    }
    from_q8 = cur_q8; to_q8 = q8;                              /* legato: it glides there */
    glide_len = glide_left = (uint32_t)carlos.glide * carlos.glide * synth_rate() / 10000;   /* 0 .. 1 s, finer near 0 */
    if (!glide_left) cur_q8 = q8;
    plat_irq_restore(st);
    if (!glide_len) synth_tag_bend(TAG_MONO, q8 - base_note * 256);
}
static void mono_off(void) { synth_note_off_tag(TAG_MONO); uint32_t st = plat_irq_save(); base_note = -1; glide_left = 0; plat_irq_restore(st); }

void carlos_key(int step, bool down) {
    if (step < 0 || step >= CARLOS_KEYS) return;
    if (!carlos.mono) {
        uint16_t tag = (uint16_t)(TAG_POLY | step);
        if (down) { sounding_poly[step] = carlos_pitch_q8(step); voice_on(tag, sounding_poly[step], 110); }
        else { synth_note_off_tag(tag); sounding_poly[step] = -1; }
        return;
    }
    int i = 0; while (i < nstack && stack[i] != step) i++;     /* last-note priority, as the Moog's keyboard */
    if (i < nstack) { memmove(stack + i, stack + i + 1, (size_t)(nstack - i - 1)); nstack--; }
    if (down) stack[nstack++] = (uint8_t)step;
    if (nstack) mono_to(carlos_pitch_q8(stack[nstack - 1]), true);
    else if (!ribbon_on) mono_off();
}

void carlos_ribbon(int32_t q8, bool down) {
    if (!down) { ribbon_on = false; if (!nstack) mono_off(); return; }
    bool legato = ribbon_on; ribbon_on = true;
    mono_to(q8, legato);
}

void carlos_play_q8(int id, int32_t q8, bool down) {
    uint16_t tag = (uint16_t)(0xC50 | (id & 15));
    if (down) voice_on(tag, q8, 100); else synth_note_off_tag(tag);
}

void carlos_all_off(void) {
    for (int i = 0; i < CARLOS_KEYS; i++) { synth_note_off_tag((uint16_t)(TAG_POLY | i)); sounding_poly[i] = -1; }
    for (int i = 0; i < 16; i++) synth_note_off_tag((uint16_t)(0xC50 | i));
    nstack = 0; ribbon_on = false; mono_off();
}

void carlos_block(uint32_t n) {
    if (base_note < 0 || !glide_left) return;
    glide_left = glide_left > n ? glide_left - n : 0;
    int32_t done = (int32_t)(glide_len - glide_left);                 /* 32-bit: ±96 semitones × a second's frames fits */
    cur_q8 = from_q8 + (to_q8 - from_q8) * done / (int32_t)MAX(1u, glide_len);
    synth_tag_bend(TAG_MONO, cur_q8 - base_note * 256);
}

int carlos_sounding(int32_t *q8, int max) {
    int k = 0;
    if (carlos.mono) { if (base_note >= 0 && k < max) q8[k++] = cur_q8; }
    else for (int i = 0; i < CARLOS_KEYS && k < max; i++) if (sounding_poly[i] >= 0) q8[k++] = sounding_poly[i];
    return k;
}
