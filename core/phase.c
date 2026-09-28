/* REICH's players (see phase.h). Time is kept in frames × 65536 since the start; each player's next step is a point on
   that line. The first player's steps are a fixed length apart. A PHASE move plays its m steps in the time of m - k
   of the first player's, each step's time worked out from the move's start (not added up), so the move ends exactly on
   the grid and the lock is exact. */
#include "phase.h"
#include "synth.h"
#include "sampler.h"
#include "seq.h"
#include "omni.h"
#include "platform.h"
#include "libc.h"

struct phase_state reich;
struct phase_player phase_pl[PHASE_PLAYERS];
struct phase_hit phase_log[PHASE_LOG];
volatile uint32_t phase_log_n;
const char *const phase_mode_names[PHASE_MODES] = { "PHASE", "SHIFT", "DRIFT", "LOOP" };
const uint8_t phase_sounds[PHASE_SOUNDS] = { P_MARIMBA, P_PIANO, P_VIBES, P_CELESTE, P_HARP, P_GUITAR, P_CLAP, P_CLAVES };

static uint32_t rate = 48000;
static uint64_t now_q16, step_q16;                  /* the time; the first player's step */
static struct { uint64_t next_q16, len_q16, move_start_q16; uint32_t move_j, move_m; uint16_t tag; bool sounding; } pl[PHASE_PLAYERS];
static uint32_t step_bpm_q16; static uint8_t step_per_beat, step_mode, step_slot;
static volatile bool start_asked;
static const int8_t pans[PHASE_PLAYERS] = { -55, 55, -20, 20 };

void phase_defaults(void) {
    phase_play(false);
    static const uint8_t pattern[PHASE_STEPS] = { 62, 69, 74, 69, 65, 69, 74, 77, 69, 74, 65, 69, 62, 74, 69, 77 };   /* D minor, an original figure */
    reich = (struct phase_state){ PM_PHASE, 2, 12, { 0 }, 4, 8, 2, 5, 0, 0, 70, false };
    memcpy(reich.notes, pattern, sizeof pattern);
}
void phase_init(uint32_t r) { rate = r ? r : 48000; memset(pl, 0, sizeof pl); phase_defaults(); }

static uint64_t loop_len_q16(void) {                /* LOOP: the slot's sound, in output frames */
    const struct sample *s = &samples[reich.slot % SAMPLE_SLOTS];
    if (!s->len || s->end <= s->start || !s->rate) return 0;
    return ((uint64_t)(s->end - s->start) * rate << 16) / s->rate;
}
static void measure_step(void) {                    /* the first player's step, from the tempo (or the loop) */
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    step_bpm_q16 = bpm_q16; step_per_beat = reich.per_beat; step_mode = reich.mode; step_slot = reich.slot;
    if (reich.mode == PM_LOOP) { step_q16 = loop_len_q16(); if (!step_q16) step_q16 = (uint64_t)rate << 16; return; }
    step_q16 = ((uint64_t)rate * 60 << 32) / ((uint64_t)bpm_q16 * CLAMP(reich.per_beat, 2, 4));
}
static bool step_changed(void) {
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    return bpm_q16 != step_bpm_q16 || reich.per_beat != step_per_beat || reich.mode != step_mode || reich.slot != step_slot;
}
static int players(void) { return CLAMP(reich.players, 2, PHASE_PLAYERS); }
static int len(void) { return reich.mode == PM_LOOP ? 1 : CLAMP(reich.len, 2, PHASE_STEPS); }

static void let_go(int k) { if (pl[k].sounding) { synth_note_off_tag(pl[k].tag); pl[k].sounding = false; } }

void phase_play(bool on) {
    uint32_t st = plat_irq_save();
    if (on) start_asked = true;
    else { reich.playing = false; start_asked = false; for (int k = 0; k < PHASE_PLAYERS; k++) let_go(k); }
    plat_irq_restore(st);
}

/* a player's step: its note (or the loop) on its own voice, the last one let go into its release */
static void hit(int k) {
    struct phase_player *P = &phase_pl[k];
    int step = P->pos % len();
    bool loop = reich.mode == PM_LOOP;
    const struct sample *s = &samples[reich.slot % SAMPLE_SLOTS];
    uint8_t note = loop ? (s->len ? s->root : 0) : reich.notes[step];
    let_go(k);
    if (note) {
        pl[k].tag = (uint16_t)(0xC80 | k << 4 | step);
        uint8_t preset = loop ? (uint8_t)(P_SMP1 + reich.slot % SAMPLE_SLOTS) : phase_sounds[reich.sound % PHASE_SOUNDS];
        synth_note_on_pan(note, (uint8_t)CLAMP(20 + reich.level * 107 / 100, 1, 127), preset, pl[k].tag, pans[k]);
        pl[k].sounding = true;
    }
    uint32_t i = phase_log_n % PHASE_LOG;
    phase_log[i] = (struct phase_hit){ (uint8_t)k, (uint8_t)step, (uint32_t)(pl[k].next_q16 >> 16) };
    phase_log_n++;
    P->steps++; P->last_q16 = pl[k].next_q16;
    P->pos = (uint8_t)((step + 1) % len());
}

/* when a player plays next, after the step it just played */
static void schedule(int k) {
    struct phase_player *P = &phase_pl[k];
    uint64_t t = pl[k].next_q16, next;
    bool wrapped = P->pos == 0;                                     /* it just finished a pattern */
    int gain = k;
    switch (k ? reich.mode : PM_SHIFT) {
    case PM_PHASE:
        if (P->moving && pl[k].move_j >= pl[k].move_m) {           /* this step was on the grid, a step (k) ahead: locked */
            P->moving = false; P->count = 0;
            next = t + step_q16;
        } else if (P->moving) {
            uint32_t m = pl[k].move_m, g = (uint32_t)MIN(gain, (int)m / 2);
            pl[k].move_j++;
            next = pl[k].move_start_q16 + (uint64_t)pl[k].move_j * (m - g) * step_q16 / m;
            P->count = (uint8_t)(pl[k].move_j / (uint32_t)len());
        } else {
            next = t + step_q16;
            if (wrapped && ++P->count >= CLAMP(reich.hold, 1, 32)) {
                P->moving = true; P->count = 0;
                pl[k].move_start_q16 = next; pl[k].move_j = 0; pl[k].move_m = (uint32_t)CLAMP(reich.move, 1, 16) * (uint32_t)len();
            }
        }
        break;
    case PM_SHIFT:
        next = t + step_q16;
        if (k && wrapped && ++P->count >= CLAMP(reich.hold, 1, 32)) { P->count = 0; P->pos = (uint8_t)((P->pos + gain) % len()); }
        else if (!k && wrapped) P->count = (uint8_t)((P->count + 1) % 100);
        break;
    default:                                                        /* DRIFT, LOOP: a little faster each, never locking */
        next = t + step_q16 * (uint64_t)(1000 - CLAMP(reich.drift, 1, 50) * k) / 1000;
        if (wrapped) P->count = (uint8_t)((P->count + 1) % 100);
        break;
    }
    pl[k].len_q16 = next - t;
    pl[k].next_q16 = next;
}

void phase_run_events(void) {
    if (start_asked) {                                              /* everyone from the first step, now */
        start_asked = false;
        for (int k = 0; k < PHASE_PLAYERS; k++) let_go(k);
        measure_step(); now_q16 = 0;
        for (int k = 0; k < PHASE_PLAYERS; k++) { pl[k].next_q16 = 0; pl[k].len_q16 = step_q16; phase_pl[k] = (struct phase_player){ 0 }; }
        reich.playing = true;
    }
    if (!reich.playing) return;
    int np = players();
    for (int k = 0; k < np; k++) {
        if (pl[k].next_q16 >= now_q16 + 65536) continue;           /* not in this frame */
        /* the tempo (or the loop) changes where the first player begins a pattern, while nobody is moving */
        if (k == 0 && phase_pl[0].pos == 0 && step_changed()) {
            bool moving = false; for (int j = 1; j < np; j++) moving |= phase_pl[j].moving;
            if (!moving) measure_step();
        }
        hit(k);
        schedule(k);
    }
    for (int k = np; k < PHASE_PLAYERS; k++) let_go(k);            /* players taken away fall silent */
}
uint32_t phase_next_event(void) {
    if (start_asked) return 0;
    if (!reich.playing) return 0xFFFFFFFFu;
    uint64_t soon = ~0ull;
    for (int k = 0; k < players(); k++) soon = MIN(soon, pl[k].next_q16);
    return soon <= now_q16 ? 0 : (uint32_t)MIN((soon - now_q16) >> 16, 0xFFFFFFFFull);   /* the frame it falls in */
}
void phase_advance(uint32_t frames) { if (reich.playing) now_q16 += (uint64_t)frames << 16; }

uint32_t phase_step_frames(void) { return (uint32_t)(step_q16 >> 16); }
int32_t phase_offset_q16(int k) {                                   /* where k is in the pattern minus where the first is */
    if (!reich.playing || k <= 0 || k >= PHASE_PLAYERS) return 0;
    uint32_t st = plat_irq_save();
    int64_t pos[2];
    for (int i = 0; i < 2; i++) {                                   /* the next step, less the time left to it in steps */
        int p = i ? k : 0;
        uint64_t left = pl[p].next_q16 > now_q16 ? pl[p].next_q16 - now_q16 : 0, l = pl[p].len_q16 ? pl[p].len_q16 : 1;
        pos[i] = (int64_t)phase_pl[p].pos * 65536 - (int64_t)MIN(left * 65536 / l, 65536u);
    }
    plat_irq_restore(st);
    int64_t span = (int64_t)len() * 65536, d = (pos[1] - pos[0]) % span;
    return (int32_t)(d < 0 ? d + span : d);
}

void phase_from_chord(void) {
    uint8_t pcs[3]; int n = omni_chord_tones(omni_chord_word, pcs);
    int r = 60 + pcs[0]; if (r > 66) r -= 12;                       /* the root near middle C */
    int t = r + (n > 1 ? (pcs[1] - pcs[0] + 12) % 12 : 4), f = r + (n > 2 ? (pcs[2] - pcs[0] + 12) % 12 : 7);
    int tones[5] = { r, t, f, r + 12, t + 12 };
    static const uint8_t fig[PHASE_STEPS] = { 0, 2, 3, 2, 1, 2, 3, 4, 2, 3, 1, 2, 0, 3, 2, 4 };   /* an original figure */
    for (int i = 0; i < PHASE_STEPS; i++) reich.notes[i] = (uint8_t)CLAMP(tones[fig[i]], 24, 108);
}
