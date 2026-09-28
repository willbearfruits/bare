#include "fm.h"
#include "libc.h"
#include "tables.h"
#include "synth.h"

struct fm_patch fm_bank[FM_PATCHES];

/* mod_mask[i]: bitmask of ops that modulate op i (ops are 0-based here; op index 3 is "OP4"). Modulators always have a higher index. */
const struct fm_algo fm_algos[FM_ALGOS] = {
    { "SERIAL",   { 0x2, 0x4, 0x8, 0x0 }, 0x1 },
    { "Y",        { 0x6, 0x0, 0x8, 0x0 }, 0x1 },
    { "TWO PAIR", { 0x6, 0x8, 0x0, 0x0 }, 0x1 },   /* 4→2→1 and 3→1 (op 3 was unconnected before project v2) */
    { "FAN IN",   { 0xE, 0x0, 0x0, 0x0 }, 0x1 },
    { "2+2",      { 0x2, 0x0, 0x8, 0x0 }, 0x5 },
    { "3+1",      { 0x0, 0x4, 0x8, 0x0 }, 0x3 },
    { "1 TO 3",   { 0x8, 0x8, 0x8, 0x0 }, 0x7 },
    { "ADDITIVE", { 0x0, 0x0, 0x0, 0x0 }, 0xF },
};

/* the factory patches keep their 1.0 sound: no velocity, no key scaling, no LFO — except where noted */
static const struct fm_patch factory[FM_PATCHES] = {
    { "E.PIANO", 1, 20, { { 0, 2, 0, 100, 1, 900, 700, 40, 0, 0 }, { 0, 2, 0, 55, 1, 350, 400, 0, 0, 0 }, { 0, 28, 0, 20, 1, 150, 300, 0, 0, 0 }, { 0, 2, 0, 35, 1, 200, 300, 0, 0, 0 } }, 30, LFO_SINE, 0, 0, 0 },
    { "FM BASS", 0, 45, { { 0, 2, 0, 100, 2, 300, 250, 70, 0, 0 }, { 0, 2, 0, 70, 1, 180, 200, 30, 0, 0 }, { 0, 4, 0, 30, 1, 120, 150, 0, 0, 0 }, { 1, 2, 0, 25, 1, 100, 120, 0, 0, 0 } }, 30, LFO_SINE, 0, 0, 0 },
    { "BELL",    2, 0,  { { 0, 2, 0, 100, 1, 2500, 2000, 0, 0, 0 }, { 0, 7, 0, 45, 1, 1200, 1500, 0, 0, 0 }, { 0, 2, 0, 0, 1, 2500, 2000, 0, 0, 0 }, { 0, 23, 0, 35, 1, 900, 1200, 0, 0, 0 } }, 30, LFO_SINE, 0, 0, 0 },
    { "BRASS",   0, 60, { { 0, 2, 0, 100, 60, 400, 300, 80, 0, 0 }, { 0, 2, 0, 65, 90, 500, 250, 70, 0, 0 }, { 0, 2, 1, 40, 120, 600, 300, 60, 0, 0 }, { 2, 2, 0, 35, 150, 800, 300, 50, 0, 0 } }, 30, LFO_SINE, 0, 0, 0 },
};

/* the omnichord's (they don't follow edits of the bank): an electric piano like the factory one; a celeste, two pairs
   with the bars' bright partials fading first; vibes, the bar's fundamental and its 4th partial, the motor's tremolo
   left to the preset */
static const struct fm_patch fixed[FM_FIXED] = {
    { "FM PIANO", 1, 20, { { 0, 2, 0, 100, 1, 1400, 700, 30, 40, 20 }, { 0, 2, 0, 55, 1, 450, 400, 0, 50, 0 }, { 0, 28, 0, 22, 1, 150, 300, 0, 60, 0 }, { 0, 2, 0, 35, 1, 250, 300, 0, 40, 0 } }, 30, LFO_SINE, 0, 0, 0 },
    { "CELESTE",  4, 0,  { { 0, 2, 0, 100, 1, 1300, 900, 0, 30, 20 }, { 0, 8, 0, 30, 1, 250, 300, 0, 50, 0 }, { 0, 8, 0, 35, 1, 700, 600, 0, 30, 0 }, { 0, 14, 0, 20, 1, 150, 200, 0, 40, 0 } }, 30, LFO_SINE, 0, 0, 0 },
    { "VIBES",    4, 0,  { { 0, 2, 0, 100, 1, 2600, 1200, 0, 30, 20 }, { 0, 8, 0, 22, 1, 500, 500, 0, 40, 0 }, { 0, 8, 0, 30, 1, 1200, 900, 0, 30, 0 }, { 0, 20, 0, 10, 1, 300, 300, 0, 40, 0 } }, 30, LFO_SINE, 0, 0, 0 },
    /* MERZBOW's junk: carriers at inharmonic ratios (1, 2.76, 5.4, a struck plate's), modulated hard and briefly */
    { "JUNK METAL", 6, 70, { { 0, 2, 0, 100, 1, 1400, 500, 0, 20, 0 }, { 0, 5, 10, 70, 1, 900, 400, 0, 20, 0 }, { 0, 10, 8, 55, 1, 600, 300, 0, 20, 0 }, { 3, 13, 37, 90, 1, 120, 100, 0, 30, 0 } }, 30, LFO_SINE, 0, 0, 0 },
    { "MARIMBA",  4, 0,  { { 0, 2, 0, 100, 1, 750, 350, 0, 30, 20 }, { 0, 8, 0, 32, 1, 70, 60, 0, 50, 0 }, { 0, 8, 0, 22, 1, 200, 150, 0, 30, 0 }, { 0, 20, 0, 16, 1, 25, 30, 0, 40, 0 } }, 30, LFO_SINE, 0, 0, 0 },
};
const struct fm_patch *fm_patch_of(int src) { return src < FM_PATCHES ? &fm_bank[src < 0 ? 0 : src] : &fixed[MIN(src - FM_PATCHES, FM_FIXED - 1)]; }

/* operator waves as 8192-entry tables, so every operator is one lookup whatever its wave */
static int16_t tri_tab[8192], saw_tab[8192], sq_tab[8192];
static const int16_t *const wave_tab[4] = { sine_q15_8192, tri_tab, saw_tab, sq_tab };

void fm_init(void) {
    memcpy(fm_bank, factory, sizeof fm_bank);
    for (int i = 0; i < 8192; i++) {
        int32_t x = i * 8;                                                  /* 0..65535 */
        tri_tab[i] = (int16_t)(x < 32768 ? x * 2 - 32768 : 98303 - x * 2);
        saw_tab[i] = (int16_t)(x - 32768);
        sq_tab[i] = (int16_t)(i < 4096 ? 32767 : -32767);
    }
}

void fm_patch_sanitize(struct fm_patch *p) {
    p->name[sizeof p->name - 1] = 0;
    if (p->algo >= FM_ALGOS) p->algo = 0;
    if (p->feedback > 100) p->feedback = 100;
    for (int i = 0; i < FM_OPS; i++) {
        struct fm_op *o = &p->op[i];
        if (o->wave > 3) o->wave = 0;
        o->ratio_x2 = (uint8_t)CLAMP(o->ratio_x2, 1, 32);
        if (o->fine > 99) o->fine = 99;
        if (o->level > 100) o->level = 100;
        if (o->a_ms > 2000) o->a_ms = 2000;
        if (o->d_ms > 4000) o->d_ms = 4000;
        if (o->r_ms > 4000) o->r_ms = 4000;
        if (o->s_pct > 100) o->s_pct = 100;
        if (o->vel > 100) o->vel = 100;
        if (o->key > 100) o->key = 100;
    }
    if (p->lfo_rate > 100) p->lfo_rate = 100;
    if (p->lfo_wave >= LFO_WAVES) p->lfo_wave = LFO_SINE;
    if (p->lfo_pitch > 100) p->lfo_pitch = 100;
    if (p->lfo_amp > 100) p->lfo_amp = 100;
    if (p->lfo_mod > 100) p->lfo_mod = 100;
}

/* the LFO's rate: 0.1 Hz at 0 to 20 Hz at 100, an octave every ~13 steps (2^(r/13)/10), as a Q32 phase step */
static uint32_t lfo_step(uint8_t r, uint32_t rate) {
    uint32_t mhz = 100;                                               /* 0.1 Hz in mHz */
    for (int i = 0; i < r / 13; i++) mhz *= 2;
    mhz = mhz * (1000 + (uint32_t)(r % 13) * 1000 / 13 * 693 / 1000) / 1000;   /* the rest of the octave, near enough */
    return (uint32_t)(((uint64_t)mhz << 32) / (1000ull * rate));
}

void fm_voice_start(struct fm_voice *v, const struct fm_patch *p, uint32_t base_inc, uint32_t rate, uint8_t note, uint8_t vel) {
    /* key scaling counts from C3 up to C7 */
    int32_t up = CLAMP((int32_t)note - 48, 0, 48) * 32767 / 48;
    for (int i = 0; i < FM_OPS; i++) {
        const struct fm_op *o = &p->op[i];
        int32_t sv = 32767 - (int32_t)o->vel * (127 - MIN(vel, 127)) * 258 / 100;       /* at vel 100: silent at velocity 0 */
        int32_t sk = 32767 - (int32_t)o->key * (up * 24576 >> 15) / 100;                /* at key 100: 1/4 (-12 dB) up top */
        v->scale[i] = (sv * sk) >> 15;
        uint32_t r = (uint32_t)o->ratio_x2 * 50 + o->fine;                  /* ratio in hundredths */
        v->inc[i] = (uint32_t)(((uint64_t)base_inc * r) / 100);
        v->ph[i] = 0; v->env[i] = 0; v->gain[i] = 0; v->stage[i] = ENV_ATTACK;
        v->adsr[i] = (struct adsr){ ms_to_step(o->a_ms, rate), ms_to_step(o->d_ms, rate), (int32_t)((ENV_MAX / 100) * o->s_pct), ms_to_step(o->r_ms, rate) };
    }
    v->fb_out = 0; v->bend_q16 = 65536;
    v->lfo_ph = 0; v->lfo_inc = lfo_step(p->lfo_rate, rate); v->sh = 0;
    if (!v->rng) v->rng = 0x9E3779B9u ^ note;
}
void fm_voice_release(struct fm_voice *v, int32_t fast) {
    for (int i = 0; i < FM_OPS; i++) {
        if (v->stage[i] < ENV_RELEASE) v->stage[i] = ENV_RELEASE;
        if (fast && v->adsr[i].r < fast) v->adsr[i].r = fast;
    }
}
bool fm_voice_done(const struct fm_voice *v, const struct fm_patch *p) {
    uint8_t car = fm_algos[p->algo].carriers;
    for (int i = 0; i < FM_OPS; i++) if ((car & (1 << i)) && v->stage[i] != ENV_DONE) return false;
    return true;
}

/* One algorithm's inner loop. Called with constant masks, so the compiler drops every modulation path the algorithm
   doesn't have. Phase modulation wraps in 32 bits, which is exactly the phase wrap: out(Q15) * depth needs no 64-bit. */
static inline __attribute__((always_inline)) void fm_loop(struct fm_voice *v, int32_t *out, int n, const int32_t dg[FM_OPS],
        const uint32_t md[FM_OPS], const int32_t cg[FM_OPS], uint32_t fbk, const int16_t *const w[FM_OPS],
        const uint8_t m0, const uint8_t m1, const uint8_t m2, const uint8_t car) {
    uint32_t p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], p3 = v->ph[3];
    const uint32_t b = v->bend_q16;
    const uint32_t i0 = b == 65536 ? v->inc[0] : (uint32_t)(((uint64_t)v->inc[0] * b) >> 16), i1 = b == 65536 ? v->inc[1] : (uint32_t)(((uint64_t)v->inc[1] * b) >> 16),
                   i2 = b == 65536 ? v->inc[2] : (uint32_t)(((uint64_t)v->inc[2] * b) >> 16), i3 = b == 65536 ? v->inc[3] : (uint32_t)(((uint64_t)v->inc[3] * b) >> 16);
    int32_t g0 = v->gain[0], g1 = v->gain[1], g2 = v->gain[2], g3 = v->gain[3], fb = v->fb_out;
    for (int i = 0; i < n; i++) {
        int32_t o3 = (w[3][(p3 + (uint32_t)fb * fbk) >> 19] * g3) >> 15;
        fb = (fb + o3) >> 1;
        uint32_t d3 = (uint32_t)o3 * md[3];
        int32_t o2 = (w[2][(p2 + ((m2 & 8) ? d3 : 0)) >> 19] * g2) >> 15;
        uint32_t d2 = (uint32_t)o2 * md[2];
        int32_t o1 = (w[1][(p1 + ((m1 & 8) ? d3 : 0) + ((m1 & 4) ? d2 : 0)) >> 19] * g1) >> 15;
        uint32_t d1 = (uint32_t)o1 * md[1];
        int32_t o0 = (w[0][(p0 + ((m0 & 8) ? d3 : 0) + ((m0 & 4) ? d2 : 0) + ((m0 & 2) ? d1 : 0)) >> 19] * g0) >> 15;
        out[i] = (((car & 1) ? o0 * cg[0] : 0) + ((car & 2) ? o1 * cg[1] : 0) + ((car & 4) ? o2 * cg[2] : 0) + ((car & 8) ? o3 * cg[3] : 0)) >> 15;
        p0 += i0; p1 += i1; p2 += i2; p3 += i3;
        g0 += dg[0]; g1 += dg[1]; g2 += dg[2]; g3 += dg[3];
    }
    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p2; v->ph[3] = p3; v->fb_out = fb;
}

/* the LFO's value for this block, -32767..32767 */
static int32_t lfo(struct fm_voice *v, const struct fm_patch *p, int n) {
    uint32_t prev = v->lfo_ph;
    v->lfo_ph += v->lfo_inc * (uint32_t)n;
    switch (p->lfo_wave) {
    case LFO_TRI:    return (int32_t)((v->lfo_ph < 0x80000000u ? v->lfo_ph : ~v->lfo_ph) >> 15) - 32768;
    case LFO_SAW:    return (int32_t)(v->lfo_ph >> 16) - 32768;
    case LFO_SQUARE: return v->lfo_ph < 0x80000000u ? 32767 : -32767;
    case LFO_SH:     if (v->lfo_ph < prev) { v->rng ^= v->rng << 13; v->rng ^= v->rng >> 17; v->rng ^= v->rng << 5; v->sh = (int32_t)(v->rng >> 17) - 16384; v->sh *= 2; }
                     return v->sh;
    default:         return sine_q15_8192[v->lfo_ph >> 19];
    }
}

void fm_voice_render(struct fm_voice *v, const struct fm_patch *p, int32_t *out, int n) {
    const struct fm_algo *a = &fm_algos[p->algo];
    int ncar = 0;
    for (int i = 0; i < FM_OPS; i++) ncar += (a->carriers >> i) & 1;
    /* the LFO moves the pitch (with the tracker's bend), the carriers' level and the modulators' */
    int32_t l = (p->lfo_pitch | p->lfo_amp | p->lfo_mod) ? lfo(v, p, n) : 0;
    uint32_t bend = v->bend_q16;
    if (p->lfo_pitch) bend = (uint32_t)(((uint64_t)bend * (uint32_t)(65536 + (l * p->lfo_pitch * 39 >> 15))) >> 16);   /* ±1 semitone at 100 */
    int32_t amp = 32767 - (p->lfo_amp * ((l + 32767) >> 1) / 100), mod = 32767 + (l * p->lfo_mod / 200);
    uint32_t keep = v->bend_q16; v->bend_q16 = bend;
    int32_t dg[FM_OPS], cg[FM_OPS], end[FM_OPS]; uint32_t md[FM_OPS]; const int16_t *w[FM_OPS];
    for (int i = 0; i < FM_OPS; i++) {
        const struct fm_op *o = &p->op[i];
        v->env[i] = env_advance(v->env[i], &v->stage[i], &v->adsr[i], n);
        end[i] = env_gain(v->env[i]);
        dg[i] = ramp_step(v->gain[i], end[i], n);
        int32_t sc = v->scale[i];
        md[i] = (uint32_t)(((uint64_t)o->level * 131072u / 100 * (uint32_t)sc >> 15) * (uint32_t)mod >> 15);   /* at level 100 a full-scale modulator swings ±1 cycle */
        cg[i] = (int32_t)(((int64_t)o->level * 32767 / (100 * (ncar ? ncar : 1)) * sc >> 15) * amp >> 15);
        w[i] = wave_tab[o->wave & 3];
    }
    uint32_t fbk = (uint32_t)p->feedback * 655;
    switch (p->algo) {
#define ALG(k) case k: fm_loop(v, out, n, dg, md, cg, fbk, w, fm_algos[k].mod_mask[0], fm_algos[k].mod_mask[1], fm_algos[k].mod_mask[2], fm_algos[k].carriers); break;
    ALG(0) ALG(1) ALG(2) ALG(3) ALG(4) ALG(5) ALG(6) ALG(7)
#undef ALG
    default: memset(out, 0, (size_t)n * sizeof *out); break;
    }
    v->bend_q16 = keep;
    for (int i = 0; i < FM_OPS; i++) v->gain[i] = end[i];
}
