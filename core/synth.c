#include "synth.h"
#include "env.h"
#include "libc.h"
#include "platform.h"
#include "tables.h"
#include "wave.h"
#include "fm.h"
#include "sampler.h"
#include "gendy.h"

int synth_draw_slot = 0;

static struct preset presets[P_COUNT] = {                        /* the last 8: the instruments', filled by core/inst.c */
    /* name        wave        duty  att  dec   rel  sus vol cut res fenv penv pdec vib det oneshot */
    { "ORGAN",     WAVE_PULSE, 25,   40,  300,  220, 82, 40,  84,  0,   0,   0,   0,  0,  0, false },
    { "PLUCK",     WAVE_PULSE, 50,    3, 1300,  160,  0, 60,  62,  8,  58,   0,   0,  0,  0, true  },
    { "SAW BASS",  WAVE_SAW,    0,    5,  220,   80, 70, 62,  48, 30,  52,   0,   0,  0,  0, false },
    { "FAT SAW",   WAVE_SAW,    0,   80,  400,  300, 80, 40,  86, 10,   0,   0,   0,  8, 40, false },
    { "SQ LEAD",   WAVE_PULSE, 50,    5,  100,  120, 75, 48, 110,  0,   0,   0,   0, 40,  0, false },
    { "CHIP",      WAVE_PULSE, 50,    1,    1,   20,100, 42, 127,  0,   0,   0,   0,  0,  0, false },
    { "BELL",      WAVE_PULSE, 12,    2,  900,  300,  0, 50, 112,  0,  15,   0,   0,  0,  8, true  },
    { "GRIND",     WAVE_SAW,    0,    2,  150,   60, 60, 66,  68, 42,  32,   0,   0,  0, 12, false },
    { "KICK",      WAVE_SINE,   0,    1,  260,   50,  0, 95, 127,  0,   0,  40,  70,  0,  0, true  },
    { "SNARE",     WAVE_NOISE,  0,    1,  180,   50,  0, 55, 104,  0,   0,   0,   0,  0,  0, true  },
    { "HAT",       WAVE_NOISE,  0,    1,   45,   20,  0, 28, 127,  0,   0,   0,   0,  0,  0, true  },
    { "DRAWN",     WAVE_TABLE,  0,    5,  600,  200, 60, 50, 104,  0,  16,   0,   0,  0,  0, false, SRC_SLOT },
    { "MORPH",     WAVE_TABLE,  0,   30, 1500,  300, 70, 50, 108,  0,   0,   0,   0, 10,  0, false, SRC_MORPH },
    { "SCAN",      WAVE_TABLE,  0,    5,  400,  200, 70, 48, 100,  0,   0,   0,   0,  0,  0, false, SRC_SCAN },
    { "ROM",       WAVE_TABLE,  0,    5,  400,  200, 70, 48,  96,  0,   0,   0,   0,  0,  0, false, SRC_ROM },
    /* FM: the operators' envelopes shape the sound; the voice envelope just stays open */
    { "FM 1",      WAVE_FM,     0,    1,    1, 4000,100, 60, 127,  0,   0,   0,   0,  0,  0, false, 0 },
    { "FM 2",      WAVE_FM,     0,    1,    1, 4000,100, 60, 127,  0,   0,   0,   0,  0,  0, false, 1 },
    { "FM 3",      WAVE_FM,     0,    1,    1, 4000,100, 60, 127,  0,   0,   0,   0,  0,  0, false, 2 },
    { "FM 4",      WAVE_FM,     0,    1,    1, 4000,100, 60, 127,  0,   0,   0,   0,  0,  0, false, 3 },
    /* samples: the slot sets the envelope, level and one-shot (core/sampler.h) */
    { "SAMPLE 1",  WAVE_SAMPLE, 0,    1,    1,  120,100,100, 127,  0,   0,   0,   0,  0,  0, false, 0 },
    { "SAMPLE 2",  WAVE_SAMPLE, 0,    1,    1,  120,100,100, 127,  0,   0,   0,   0,  0,  0, false, 1 },
    { "SAMPLE 3",  WAVE_SAMPLE, 0,    1,    1,  120,100,100, 127,  0,   0,   0,   0,  0,  0, false, 2 },
    { "SAMPLE 4",  WAVE_SAMPLE, 0,    1,    1,  120,100,100, 127,  0,   0,   0,   0,  0,  0, false, 3 },
    { "SAMPLE 5",  WAVE_SAMPLE, 0,    1,    1,  120,100,100, 127,  0,   0,   0,   0,  0,  0, false, 4 },
    { "SAMPLE 6",  WAVE_SAMPLE, 0,    1,    1,  120,100,100, 127,  0,   0,   0,   0,  0,  0, false, 5 },
    { "SAMPLE 7",  WAVE_SAMPLE, 0,    1,    1,  120,100,100, 127,  0,   0,   0,   0,  0,  0, false, 6 },
    { "SAMPLE 8",  WAVE_SAMPLE, 0,    1,    1,  120,100,100, 127,  0,   0,   0,   0,  0,  0, false, 7 },
    /* GENDY: the patch sets the envelope, level and filter (core/gendy.h) */
    { "GENDY 1",   WAVE_GENDY,  0,    1,    1,  120,100, 70, 127,  0,   0,   0,   0,  0,  0, false, 0 },
    { "GENDY 2",   WAVE_GENDY,  0,    1,    1,  120,100, 70, 127,  0,   0,   0,   0,  0,  0, false, 1 },
    { "GENDY 3",   WAVE_GENDY,  0,    1,    1,  120,100, 70, 127,  0,   0,   0,   0,  0,  0, false, 2 },
    { "GENDY 4",   WAVE_GENDY,  0,    1,    1,  120,100, 70, 127,  0,   0,   0,   0,  0,  0, false, 3 },
    [P_INST8 + 1] =                                              /* the omnichord's voices: main voices first, then subs */
    { "MELLOW PULSE",  WAVE_PULSE, 50,  2, 1500, 250,  0, 58,  60, 12,  26,   0,   0,  0,  0, false },
    { "TREMOLO PULSE", WAVE_PULSE, 40,  4, 1800, 300,  0, 46,  66,  0,  12,   0,   0,  0,  0, false, 0, 55 },
    { "SYNTH STRINGS", WAVE_SAW,    0, 180,  600, 450, 85, 40,  74,  5,   0,   0,   0, 12, 30, false },
    { "MELLOW PAD",    WAVE_PULSE, 30, 260,  800, 600, 80, 42,  58,  8,   6,   0,   0,  6, 20, false },
    { "HARP",          WAVE_TRI,    0,  1, 2600, 400,  0, 70,  98,  0,  22,   0,   0,  0,  0, false },
    { "CELESTE",       WAVE_FM,     0,  1,    1, 4000,100,58, 127,  0,   0,   0,   0,  0,  0, false, FM_PATCHES + 1 },
    { "A.PIANO",       WAVE_SAW,    0,  2, 2400, 350,  0, 55,  70,  5,  40,   0,   0,  0,  4, false },
    { "GUITAR",        WAVE_PULSE, 18,  1, 1900, 250,  0, 55,  76, 18,  36,   0,   0,  0,  0, false },
    { "FM PIANO",      WAVE_FM,     0,  1,    1, 4000,100,58, 127,  0,   0,   0,   0,  0,  0, false, FM_PATCHES + 0 },
    { "VIBES",         WAVE_FM,     0,  1,    1, 4000,100,58, 127,  0,   0,   0,   0,  0,  0, false, FM_PATCHES + 2, 35 },
    { "BANJO",         WAVE_PULSE, 22,  1,  700, 180,  0, 60,  88, 22,  30,   0,   0,  0,  0, false },
    /* its drum kit (the OM-84's is KICK, SNARE and HAT above) */
    { "KICK 2",        WAVE_SINE,   0,  1,  320,  60,  0,100, 127,  0,   0,  44,  55,  0,  0, true  },
    { "SNARE 2",       WAVE_NOISE,  0,  1,  200,  60,  0, 58, 108, 10,  10,   0,   0,  0,  0, true  },
    { "HAT 2",         WAVE_NOISE,  0,  1,   40,  20,  0, 26, 124,  0,   0,   0,   0,  0,  0, true  },
    { "OPEN HAT",      WAVE_NOISE,  0,  1,  320,  80,  0, 24, 122,  0,   0,   0,   0,  0,  0, true  },
    { "CLAP",          WAVE_NOISE,  0,  3,  160,  60,  0, 50, 100, 30,   8,   0,   0,  0,  0, true  },
    { "TOM HI",        WAVE_SINE,   0,  1,  260,  60,  0, 80, 127,  0,   0,  20,  90,  0,  0, true  },
    { "TOM LO",        WAVE_SINE,   0,  1,  320,  60,  0, 85, 127,  0,   0,  20, 110,  0,  0, true  },
    { "FLOOR TOM",     WAVE_SINE,   0,  1,  400,  80,  0, 90, 127,  0,   0,  22, 140,  0,  0, true  },
    { "CRASH",         WAVE_NOISE,  0,  1, 1600, 400,  0, 24, 118,  5,   0,   0,   0,  0,  0, true  },
    { "RIM",           WAVE_PULSE, 50,  1,   30,  20,  0, 60, 110,  0,   0,   0,   0,  0,  0, true  },
    { "TAMBOURINE",    WAVE_NOISE,  0,  1,  140,  40,  0, 22, 126,  0,   0,   0,   0,  0,  0, true  },
    { "CONGA",         WAVE_SINE,   0,  1,  180,  40,  0, 75, 127,  0,   0,  14,  40,  0,  0, true  },
    { "MARACAS",       WAVE_NOISE,  0,  1,   35,  15,  0, 18, 126,  0,   0,   0,   0,  0,  0, true  },
    { "CLAVES",        WAVE_SINE,   0,  1,   70,  20,  0, 70, 127,  0,   0,   0,   0,  0,  0, true  },
    { "KICK 808",      WAVE_SINE,   0,  1,  900, 200,  0,100, 127,  0,   0,  30,  60,  0,  0, true  },
    { "MOOG",          WAVE_SAW,    0,  5,  400, 250, 70, 60,  70, 40,  40,   0,   0,  0, 12, false },   /* CARLOS rewrites it */
    { "JUNK METAL",    WAVE_FM,     0,  1,    1, 4000,100,70, 127,  0,   0,   0,   0,  0,  0, false, FM_PATCHES + 3 },
    { "MARIMBA",       WAVE_FM,     0,  1,    1, 1500,100,64, 127,  0,   0,   0,   0,  0,  0, false, FM_PATCHES + 4 },
};
int32_t synth_tune_q8;
void synth_user_preset(int i, const struct preset *p) {
    if (i < 0 || i >= P_INST_END - P_INST1) return;
    uint32_t st = plat_irq_save();
    if (p) presets[P_INST1 + i] = *p; else memset(&presets[P_INST1 + i], 0, sizeof presets[0]);
    plat_irq_restore(st);
}
void synth_set_preset(int id, const struct preset *p) {
    if (id < 0 || id >= P_COUNT || !p) return;
    uint32_t st = plat_irq_save();
    presets[id] = *p;
    plat_irq_restore(st);
}
const char *synth_preset_name(int id) {
    if (synth_is_inst(id)) return presets[id].name ? presets[id].name : "(no instrument)";
    const struct preset *p = synth_preset(id);
    if (p->wave == WAVE_FM && p->src >= FM_PATCHES) return p->name;          /* a fixed patch: the preset's name */
    return p->wave == WAVE_FM ? fm_patch_of(p->src)->name : p->wave == WAVE_SAMPLE ? samples[p->src].name
         : p->wave == WAVE_GENDY ? gendy_bank[p->src].name : p->name;
}
int synth_preset_next(int id, int d) {
    for (int i = 0; i < P_COUNT; i++) {
        id = (id + P_COUNT + d) % P_COUNT;
        if (synth_is_inst(id) ? presets[id].name != 0 : presets[id].wave != WAVE_SAMPLE || samples[presets[id].src].len) break;
    }
    return id;
}
/* one-shots play to their end and retrigger on the same tag; a sample slot chooses for itself */
static bool oneshot(int id) { const struct preset *p = synth_preset(id); return p->wave == WAVE_SAMPLE ? samples[p->src].oneshot : p->oneshot; }
const struct preset *synth_preset(int id) { return &presets[id < 0 ? 0 : id >= P_COUNT ? P_COUNT - 1 : id]; }

const int32_t block_recip[33] = { 0, 32768, 16384, 10923, 8192, 6554, 5461, 4681, 4096, 3641, 3277, 2979, 2731, 2521, 2341,
    2185, 2048, 1928, 1820, 1725, 1638, 1560, 1489, 1425, 1365, 1311, 1260, 1214, 1170, 1130, 1092, 1057, 1024 };

struct voice {
    bool     active, released;
    uint8_t  preset, note, stage, bus;
    uint16_t tag;
    uint32_t born, freq_hz;
    /* oscillator */
    uint32_t phase, phase2, inc, inc2, duty;
    int32_t  pulse_dc;                   /* what a non-50 % pulse puts at 0 Hz, taken out again */
    uint32_t noise;
    const int16_t *tab;                  /* WAVE_TABLE source (live pointer) */
    int32_t  tab_dc; uint8_t tab_age;    /* the table's mean, refreshed every few blocks (drawn waves change) */
    uint8_t  morph_from; uint32_t morph_q16, morph_inc;
    /* envelope + level */
    int32_t  env; struct adsr adsr;
    int32_t  amp, pan_l, pan_r;          /* Q15 */
    int32_t  gl, gr;                     /* per-channel gain reached at the end of the last block */
    /* modulation */
    int32_t  penv_cur, penv_dec;         /* Q15 pitch envelope, decrement per sample */
    uint32_t lfo, lfo_inc;
    /* filter (Chamberlin state variable) */
    int32_t  f_low, f_band;
    struct fm_voice fm;
    struct gendy_osc gd;
    /* sample playback: a frame index and its fraction (Q16); smul turns the pitch increment into frames per output frame */
    const struct sample *smp;
    uint32_t spos, sfrac, smul;
    uint32_t bend_q16;                   /* the tracker's pitch offset, 65536 = none */
    int32_t  base_amp;                   /* the level before velocity (Q15 units of 24000) */
};

/* 24 playable voices plus a few tails: a stolen voice finishes its last milliseconds there instead of clicking off */
#define TAILS 4
static struct voice voices[SYNTH_MAX_VOICES + TAILS];
static uint32_t rate = 48000;
static uint32_t sample_counter;
static int mod_cutoff, mod_detune;
static int32_t fast_release;                  /* per-sample step of a ~3 ms fade */
void synth_set_mod(int c, int d) { mod_cutoff = c; mod_detune = d; }

/* 2^(i/12) in Q16 */
static const uint32_t semitone_q16[12] = {
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715 };

static uint32_t note_freq_q16(int note) {
    int k = note - 69, oct = 0;
    while (k < 0) { k += 12; oct--; }
    while (k >= 12) { k -= 12; oct++; }
    uint64_t f = ((uint64_t)(440u << 16) * semitone_q16[k]) >> 16;
    if (oct > 0) f <<= oct; else if (oct < 0) f >>= -oct;
    return (uint32_t)f;
}

void synth_init(uint32_t sample_rate) {
    rate = sample_rate ? sample_rate : 48000;
    memset(voices, 0, sizeof voices);
    sample_counter = 0;
    fast_release = ms_to_step(3, rate);
}
uint32_t synth_rate(void) { return rate; }

static void fade_out(struct voice *v) {
    v->released = true;
    if (presets[v->preset].wave == WAVE_FM) fm_voice_release(&v->fm, fast_release);
    v->stage = ENV_RELEASE;
    if (v->adsr.r < fast_release) v->adsr.r = fast_release;
}

static struct voice *alloc_voice(int preset_id, uint16_t tag) {
    struct voice *best = 0;
    if (oneshot(preset_id))
        for (int i = 0; i < SYNTH_MAX_VOICES; i++)
            if (voices[i].active && voices[i].tag == tag) return &voices[i];
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) if (!voices[i].active) return &voices[i];
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) {           /* steal: quietest released, else oldest */
        struct voice *v = &voices[i];
        if (!best) { best = v; continue; }
        if (v->released && !best->released) best = v;
        else if (v->released == best->released && (v->released ? v->env < best->env : v->born < best->born)) best = v;
    }
    for (int t = SYNTH_MAX_VOICES; t < SYNTH_MAX_VOICES + TAILS; t++)
        if (!voices[t].active) { voices[t] = *best; fade_out(&voices[t]); break; }
    best->active = false;
    return best;
}

void synth_note_on_pan(uint8_t note, uint8_t vel, uint8_t preset_id, uint16_t tag, int pan) { synth_note_on_ring(note, vel, preset_id, tag, pan, 0); }
void synth_note_on_ring(uint8_t note, uint8_t vel, uint8_t preset_id, uint16_t tag, int pan, int ring_ms) {
    const struct preset *p = synth_preset(preset_id);
    const struct sample *sm = p->wave == WAVE_SAMPLE ? &samples[p->src] : 0;
    if (sm && (sm->busy || !sm->data || sm->end <= sm->start)) return;      /* an empty slot, or one being rewritten */
    uint32_t st = plat_irq_save();
    struct voice *v = alloc_voice(preset_id, tag);
    bool retrig = v->active && v->tag == tag && v->preset == preset_id;
    uint32_t f = note_freq_q16(note);
    if (synth_tune_q8) f = (uint32_t)(((uint64_t)f * synth_bend_mul(synth_tune_q8)) >> 16);
    v->active = true; v->released = false; v->preset = preset_id; v->note = note; v->tag = tag;
    v->bus = (tag >> 8) == 3 ? BUS_SEQ : (tag >> 8) == 4 ? BUS_RHYTHM : (tag >> 8) == 9 ? BUS_UPIC : (tag >> 8) == 8 ? BUS_CLOUD :
             (tag >> 12) == 0xB ? BUS_DOOM : (tag >> 8) == 0xC ? BUS_LINEAGE : BUS_PLAY;
    v->inc = (uint32_t)(((uint64_t)f << 16) / rate);
    v->inc2 = v->inc + (uint32_t)(((uint64_t)v->inc * p->detune) >> 12);
    v->freq_hz = f >> 16;
    if (!retrig) { v->phase = 0; v->phase2 = 0x40000000u; v->f_low = 0; v->f_band = 0; v->env = 0; v->gl = v->gr = 0; }
    else v->env /= 2;                           /* soft restart avoids a click */
    v->duty = (uint32_t)(((uint64_t)p->duty << 32) / 100);
    v->pulse_dc = (int32_t)p->duty * 65534 / 100 - 32767;
    v->stage = ENV_ATTACK; v->born = sample_counter;
    v->adsr = (struct adsr){ ms_to_step(p->attack_ms, rate), ms_to_step(p->decay_ms, rate), (int32_t)((ENV_MAX / 100) * p->sustain), ms_to_step(p->release_ms, rate) };
    v->base_amp = 24000 * (int32_t)p->volume / 100;               /* 3/4 of full scale: the master trim */
    v->bend_q16 = 65536;
    if (ring_ms > 0) {                                          /* how long it rings: a decaying sound's decay, the release */
        if (!p->sustain && p->wave != WAVE_FM) v->adsr.d = ms_to_step(ring_ms, rate);
        v->adsr.r = ms_to_step(MAX(40, ring_ms / 2), rate);
    }
    bool own = synth_is_inst(preset_id);                        /* an instrument's sound keeps its own envelope and level */
    if (sm) {
        if (!own) {
            v->adsr = (struct adsr){ ms_to_step(sm->attack_ms, rate), ms_to_step(1, rate), ENV_MAX, ms_to_step(sm->release_ms, rate) };
            v->base_amp = 24000 * (int32_t)sm->level / 100;
        }
        v->smp = sm; v->spos = sm->start; v->sfrac = 0;
        v->smul = (uint32_t)(((uint64_t)sm->rate << 32) / note_freq_q16(sm->root));
    }
    v->amp = v->base_amp * vel / 127;
    pan = CLAMP(pan, -100, 100);
    v->pan_l = pan > 0 ? 32767 * (100 - pan) / 100 : 32767;      /* balance law: the centre stays at full level */
    v->pan_r = pan < 0 ? 32767 * (100 + pan) / 100 : 32767;
    if (p->wave == WAVE_FM) fm_voice_start(&v->fm, fm_patch_of(p->src), v->inc, rate, note, vel);
    if (p->wave == WAVE_GENDY) {                                    /* its patch: envelope, level; a walk of its own */
        const struct gendy_patch *gp = &gendy_bank[p->src & 3];
        if (!own) {
            v->adsr = (struct adsr){ ms_to_step(MAX(gp->a_ms, 1), rate), ms_to_step(MAX(gp->d_ms, 1), rate), (int32_t)((ENV_MAX / 100) * gp->s_pct),
                                     ms_to_step(MAX(gp->r_ms, 1), rate) };
            v->base_amp = 24000 * (int32_t)gp->level / 100; v->amp = v->base_amp * vel / 127;
        }
        if (!retrig) gendy_osc_start(&v->gd, gp, (sample_counter * 2654435761u) ^ ((uint32_t)tag << 8) ^ note);
    }
    v->penv_cur = p->penv ? 32767 : 0;
    v->penv_dec = p->penv ? (int32_t)(32767u * 1000u / ((uint32_t)p->pdecay_ms * rate)) + 1 : 0;
    v->lfo_inc = (uint32_t)(((uint64_t)55 << 32) / (rate * 10));   /* 5.5 Hz */
    if (!v->noise) v->noise = 0x2545F491u ^ (uint32_t)tag;
    v->tab_age = 0;
    switch (p->wave == WAVE_TABLE ? p->src : SRC_NONE) {
    case SRC_SLOT:  v->tab = wave_bank[synth_draw_slot].tab; break;
    case SRC_MORPH: {
        v->tab = wave_bank[synth_draw_slot].tab; v->morph_from = (uint8_t)synth_draw_slot;
        uint32_t steps = (WAVE_SLOTS - 1) - (uint32_t)synth_draw_slot, span = rate * 3 / 2;   /* to the last slot over ~1.5 s */
        v->morph_q16 = 0; v->morph_inc = (uint32_t)((((uint64_t)steps * 256) << 16) / span);
        break; }
    case SRC_SCAN:  v->tab = wave_scan_tab; break;
    case SRC_ROM:   v->tab = wave_rom_tab; break;
    default: v->tab = 0;
    }
    plat_irq_restore(st);
}
void synth_note_on(uint8_t note, uint8_t vel, uint8_t preset, uint16_t tag) { synth_note_on_pan(note, vel, preset, tag, 0); }

void synth_note_off_tag(uint16_t tag) {
    uint32_t st = plat_irq_save();
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
        struct voice *v = &voices[i];
        if (v->active && v->tag == tag && !v->released && !oneshot(v->preset)) {
            v->released = true;
            if (presets[v->preset].wave == WAVE_FM) fm_voice_release(&v->fm, 0); else v->stage = ENV_RELEASE;
        }
    }
    plat_irq_restore(st);
}

void synth_tag_cut(uint16_t tag) {
    uint32_t st = plat_irq_save();
    int32_t step = ms_to_step(10, rate);
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
        struct voice *v = &voices[i];
        if (!v->active || v->tag != tag) continue;
        v->released = true; v->stage = ENV_RELEASE;
        if (v->adsr.r < step) v->adsr.r = step;
        if (presets[v->preset].wave == WAVE_FM) fm_voice_release(&v->fm, step);
    }
    plat_irq_restore(st);
}

void synth_all_off(void) {
    uint32_t st = plat_irq_save();
    int32_t step = ms_to_step(30, rate);
    for (int i = 0; i < SYNTH_MAX_VOICES + TAILS; i++) {
        struct voice *v = &voices[i];
        if (!v->active) continue;
        v->released = true; v->stage = ENV_RELEASE;
        if (v->adsr.r < step) v->adsr.r = step;
        if (presets[v->preset].wave == WAVE_FM) fm_voice_release(&v->fm, step);
    }
    plat_irq_restore(st);
}

/* 2^(s/12) in Q16 for s in 1/256 semitones: whole semitones from the table, the fraction between them linearly */
uint32_t synth_bend_mul(int32_t s) {
    int32_t semis = s >> 8, frac = s & 255, oct = 0;
    while (semis < 0) { semis += 12; oct--; }
    while (semis >= 12) { semis -= 12; oct++; }
    uint32_t a = semitone_q16[semis], b = semis == 11 ? 131072 : semitone_q16[semis + 1];
    uint32_t m = a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)frac) >> 8);
    return oct >= 0 ? m << oct : m >> -oct;
}
void synth_tag_bend(uint16_t tag, int32_t s) {
    uint32_t m = s ? synth_bend_mul(CLAMP(s, -96 * 256, 96 * 256)) : 65536;
    uint32_t st = plat_irq_save();
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) if (voices[i].active && voices[i].tag == tag) { voices[i].bend_q16 = m; voices[i].fm.bend_q16 = m; }
    plat_irq_restore(st);
}
void synth_tag_velocity(uint16_t tag, uint8_t vel) {
    uint32_t st = plat_irq_save();
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) if (voices[i].active && voices[i].tag == tag) voices[i].amp = voices[i].base_amp * MIN(vel, 127) / 127;
    plat_irq_restore(st);
}
void synth_tag_level(uint16_t tag, int32_t q15) {
    q15 = CLAMP(q15, 0, 32767);
    uint32_t st = plat_irq_save();
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) if (voices[i].active && voices[i].tag == tag) voices[i].amp = (voices[i].base_amp * q15) >> 15;
    plat_irq_restore(st);
}
void synth_tag_pan(uint16_t tag, int pan) {
    pan = CLAMP(pan, -100, 100);
    int32_t pl = pan > 0 ? 32767 * (100 - pan) / 100 : 32767, pr = pan < 0 ? 32767 * (100 + pan) / 100 : 32767;
    uint32_t st = plat_irq_save();
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) if (voices[i].active && voices[i].tag == tag) { voices[i].pan_l = pl; voices[i].pan_r = pr; }
    plat_irq_restore(st);
}

void synth_kill_preset(int preset) {
    uint32_t st = plat_irq_save();
    for (int i = 0; i < SYNTH_MAX_VOICES + TAILS; i++) if (voices[i].active && voices[i].preset == preset) voices[i].active = false;
    plat_irq_restore(st);
}

int synth_sample_heads(int slot, uint32_t *pos, int max) {
    int n = 0;
    for (int i = 0; i < SYNTH_MAX_VOICES && n < max; i++) {
        const struct voice *v = &voices[i];
        if (v->active && presets[v->preset].wave == WAVE_SAMPLE && presets[v->preset].src == slot) pos[n++] = v->spos;
    }
    return n;
}

/* PolyBLEP residual in Q15 for phase t (Q32) and increment dt (Q32).
   x = t/dt in Q15 is computed as t / (dt >> 15): a 32-bit division (the 64-bit one is a slow library loop on i386). */
static inline int32_t blep(uint32_t t, uint32_t dt) {
    if (t >= dt && t <= (uint32_t)(0u - dt)) return 0;        /* the common case: nowhere near an edge */
    uint32_t d = dt >> 15;
    if (!d) return 0;
    if (t < dt) {
        int32_t x = (int32_t)(t / d); if (x > 32767) x = 32767;
        return 2 * x - ((x * x) >> 15) - 32768;
    }
    int32_t x = (int32_t)((0u - t) / d); if (x > 32767) x = 32767;
    return ((x * x) >> 15) - 2 * x + 32768;
}

static inline int32_t lerp_tab(const int16_t *t, uint32_t ph) {
    uint32_t ix = ph >> 24, fr = (ph >> 16) & 255;
    return (t[ix] * (int32_t)(256 - fr) + t[(ix + 1) & 255] * (int32_t)fr) >> 8;
}
static int32_t tab_mean(const int16_t *t) { int32_t s = 0; for (int i = 0; i < 256; i++) s += t[i]; return s >> 8; }

/* The per-sample loop for one oscillator, with and without the filter. Everything that changes slower than the
   audio is computed once per block before it; OSC sets `s` and advances the phase. */
#define VOICE_LOOP(OSC)                                                                                   \
    if (filt) {                                                                                           \
        for (int i = 0; i < n; i++) {                                                                     \
            int32_t s; OSC;                                                                               \
            low += (f * band) >> 15;                                                                      \
            int32_t high = s - low - ((q * band) >> 15);                                                  \
            high = CLAMP(high, -65535, 65535);                                                            \
            band += (f * high) >> 15;                                                                     \
            band = CLAMP(band, -65535, 65535); low = CLAMP(low, -65535, 65535);                           \
            L[i] += (low * gl) >> 15; R[i] += (low * gr) >> 15; gl += dgl; gr += dgr;                     \
        }                                                                                                 \
    } else {                                                                                              \
        for (int i = 0; i < n; i++) {                                                                     \
            int32_t s; OSC;                                                                               \
            L[i] += (s * gl) >> 15; R[i] += (s * gr) >> 15; gl += dgl; gr += dgr;                         \
        }                                                                                                 \
    }

static void voice_block(struct voice *v, int32_t *L, int32_t *R, int n) {
    const struct preset *p = &presets[v->preset];
    /* envelope and level */
    /* (an FM voice's own envelope stays open at 100 %: its operators shape the sound, a note-off releases them) */
    int32_t env = env_advance(v->env, &v->stage, &v->adsr, n);
    bool ending = v->stage == ENV_DONE;
    int32_t e = env_gain(env);
    int32_t g = (e * v->amp) >> 15;
    int32_t gl_end = (g * v->pan_l) >> 15, gr_end = (g * v->pan_r) >> 15;
    int32_t gl = v->gl, gr = v->gr, dgl = ramp_step(gl, gl_end, n), dgr = ramp_step(gr, gr_end, n);
    /* pitch: the tracker's bend, envelope, thermal detune, vibrato */
    uint32_t inc = v->inc, inc2 = v->inc2;
    if (v->bend_q16 != 65536) { inc = (uint32_t)(((uint64_t)inc * v->bend_q16) >> 16); inc2 = (uint32_t)(((uint64_t)inc2 * v->bend_q16) >> 16); }
    if (v->penv_cur > 0) {
        uint32_t d = (uint32_t)(((uint64_t)inc * (uint32_t)v->penv_cur * p->penv) >> 19);
        inc += d; inc2 += d;
        v->penv_cur -= v->penv_dec * n; if (v->penv_cur < 0) v->penv_cur = 0;
    }
    if (mod_detune) { int32_t d = (int32_t)(((int64_t)inc * mod_detune) >> 12); inc2 += d; if (!p->detune) inc += d / 2; }
    if (p->vib || p->trem) v->lfo += v->lfo_inc * (uint32_t)n;
    int32_t tri = (int32_t)((v->lfo < 0x80000000u ? v->lfo : ~v->lfo) >> 16) - 16384;   /* ±16384 */
    if (p->vib) {
        int32_t d = (int32_t)(((int64_t)inc * tri * p->vib) >> 26);
        inc += d; inc2 += d;
    }
    if (p->trem) {                                                     /* the level swings with the same LFO */
        int32_t t = 32767 - (int32_t)p->trem * (16384 + tri) / 100;       /* 1 .. 1 - depth */
        gl_end = (gl_end * t) >> 15; gr_end = (gr_end * t) >> 15;
        dgl = ramp_step(gl, gl_end, n); dgr = ramp_step(gr, gr_end, n);
    }
    /* filter coefficients for this block (a GENDY patch has its own) */
    const struct gendy_patch *gp = p->wave == WAVE_GENDY ? &gendy_bank[p->src & 3] : 0;
    bool patch_filter = gp && !synth_is_inst(v->preset);            /* an instrument's own filter, whatever its wave */
    int cutoff = patch_filter ? gp->cutoff : p->cutoff, reso = patch_filter ? gp->reso : p->reso;
    bool filt = cutoff < 127;
    int32_t f = 0, q = 32767 - (int32_t)reso * 290;
    if (filt) {
        int ci = cutoff + (int)(((int32_t)p->fenv * (env >> 9)) >> 15) + mod_cutoff;
        f = svf_f_q15[CLAMP(ci, 0, 127)];
    }
    int32_t low = v->f_low, band = v->f_band;
    uint32_t ph = v->phase, ph2 = v->phase2;
    bool det = p->detune != 0;

    switch (p->wave) {
    case WAVE_PULSE: {
        uint32_t duty = v->duty; int32_t dc = v->pulse_dc;
        if (det) { VOICE_LOOP(s = ((ph < duty ? 32767 : -32767) + blep(ph, inc) - blep(ph - duty, inc) - dc
                                   + (ph2 < duty ? 32767 : -32767) + blep(ph2, inc2) - blep(ph2 - duty, inc2) - dc) >> 1;
                              ph += inc; ph2 += inc2) }
        else { VOICE_LOOP(s = (ph < duty ? 32767 : -32767) + blep(ph, inc) - blep(ph - duty, inc) - dc; ph += inc) }
        break; }
    case WAVE_SAW:
        if (det) { VOICE_LOOP(s = ((int32_t)(ph >> 16) - 32768 - blep(ph, inc) + (int32_t)(ph2 >> 16) - 32768 - blep(ph2, inc2)) >> 1;
                              ph += inc; ph2 += inc2) }
        else { VOICE_LOOP(s = (int32_t)(ph >> 16) - 32768 - blep(ph, inc); ph += inc) }
        break;
    case WAVE_TRI:
        VOICE_LOOP({ int32_t x = (int32_t)(ph >> 16); s = x < 32768 ? x * 2 - 32768 : 98303 - x * 2; ph += inc; })
        break;
    case WAVE_SINE:
        VOICE_LOOP(s = sine_q15_8192[ph >> 19]; ph += inc)
        break;
    case WAVE_NOISE: {
        uint32_t nz = v->noise;
        VOICE_LOOP({ nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5; s = (int32_t)(nz >> 16) - 32768; })
        v->noise = nz;
        break; }
    case WAVE_TABLE:
        if (p->src == SRC_MORPH) {
            /* travel from the chosen slot to the last one over ~1.5 s; position moves once per block */
            uint32_t last = WAVE_SLOTS - 1, steps = last - v->morph_from, posq8 = v->morph_q16 >> 16;
            if (posq8 >= steps * 256) posq8 = steps * 256; else v->morph_q16 += v->morph_inc * (uint32_t)n;
            uint32_t a = v->morph_from + (posq8 >> 8), b = a + 1 > last ? last : a + 1;
            int32_t mf = (int32_t)(posq8 & 255);
            const int16_t *ta = wave_bank[a].tab, *tb = wave_bank[b].tab;
            if (v->tab_age-- == 0) { v->tab_dc = (tab_mean(ta) * (256 - mf) + tab_mean(tb) * mf) >> 8; v->tab_age = 7; }
            int32_t dc = v->tab_dc;
            VOICE_LOOP(s = ((lerp_tab(ta, ph) * (256 - mf) + lerp_tab(tb, ph) * mf) >> 8) - dc; ph += inc)
        } else {
            const int16_t *t = v->tab;
            if (v->tab_age-- == 0) { v->tab_dc = tab_mean(t); v->tab_age = 7; }   /* drawn / scanned waves change */
            int32_t dc = v->tab_dc;
            VOICE_LOOP(s = lerp_tab(t, ph) - dc; ph += inc)
        }
        break;
    case WAVE_SAMPLE: {
        /* 16-bit frames are interpolated; 8-bit ones are held, like the old samplers' DACs (their grit is the point) */
        const struct sample *sm = v->smp;
        if (sm->busy) { ending = true; break; }
        uint32_t step = (uint32_t)(((uint64_t)inc * v->smul) >> 32), pos = v->spos, fr = v->sfrac;
        uint32_t end = MIN(sm->end, sm->len), ls = sm->loop_start;
        bool loop = sm->loop && ls < end && end - ls >= 16;
        if (step > (64u << 16)) step = 64u << 16;
        if (sm->bits == 8) {
            const int8_t *d = sm->data;
            VOICE_LOOP({
                if (pos < end) {
                    s = d[pos] * 256;
                    fr += step; pos += fr >> 16; fr &= 0xFFFF;
                    if (pos >= end && loop) pos = ls + (pos - end) % (end - ls);
                } else s = 0;
            })
        } else {
            const int16_t *d = sm->data;
            VOICE_LOOP({
                if (pos < end) {
                    uint32_t nx = pos + 1 < end ? pos + 1 : loop ? ls : pos;
                    s = d[pos] + (((d[nx] - d[pos]) * (int32_t)(fr >> 1)) >> 15);
                    fr += step; pos += fr >> 16; fr &= 0xFFFF;
                    if (pos >= end && loop) pos = ls + (pos - end) % (end - ls);
                } else s = 0;
            })
        }
        v->spos = pos; v->sfrac = fr;
        if (pos >= end) ending = true;
        break; }
    case WAVE_GENDY: {
        int32_t tmp[SYNTH_BLOCK];
        gendy_render(&v->gd, gp, tmp, n, inc);
        VOICE_LOOP(s = tmp[i])
        break; }
    case WAVE_FM: {
        int32_t tmp[SYNTH_BLOCK];
        const struct fm_patch *fp = fm_patch_of(p->src);
        fm_voice_render(&v->fm, fp, tmp, n);
        for (int i = 0; i < n; i++) { int32_t s = tmp[i]; L[i] += (s * gl) >> 15; R[i] += (s * gr) >> 15; gl += dgl; gr += dgr; }
        if (fm_voice_done(&v->fm, fp)) ending = true;
        break; }
    }
    v->phase = ph; v->phase2 = ph2; v->f_low = low; v->f_band = band;
    v->env = env; v->gl = gl_end; v->gr = gr_end;
    if (ending) v->active = false;
}

uint32_t synth_render(int32_t (*L)[SYNTH_BLOCK], int32_t (*R)[SYNTH_BLOCK], uint32_t n) {
    if (n > SYNTH_BLOCK) n = SYNTH_BLOCK;
    uint32_t used = 0;
    for (int i = 0; i < SYNTH_MAX_VOICES + TAILS; i++) {
        struct voice *v = &voices[i];
        if (!v->active) continue;
        if (!(used >> v->bus & 1)) { used |= 1u << v->bus; memset(L[v->bus], 0, n * 4); memset(R[v->bus], 0, n * 4); }
        voice_block(v, L[v->bus], R[v->bus], (int)n);
    }
    sample_counter += n;
    return used;
}

int synth_active_voices(void) {
    int c = 0;
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) if (voices[i].active) c++;
    return c;
}

uint32_t synth_mono_freq(void) {
    /* Newest one-shot (strum/lead) note wins for ~200 ms, otherwise arpeggiate held notes (chip-style). */
    struct voice *lead = 0;
    uint32_t held[SYNTH_MAX_VOICES]; int nheld = 0;
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
        struct voice *v = &voices[i];
        if (!v->active || presets[v->preset].wave == WAVE_NOISE) continue;
        if (oneshot(v->preset)) { if (!lead || v->born > lead->born) lead = v; }
        else if (!v->released) held[nheld++] = v->freq_hz;
    }
    if (lead && sample_counter - lead->born < rate / 5) return lead->freq_hz;
    if (nheld) return held[(sample_counter / (rate / 40)) % nheld];
    if (lead && sample_counter - lead->born < rate) return lead->freq_hz;
    return 0;
}
