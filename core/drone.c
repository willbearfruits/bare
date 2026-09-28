/* RADIGUE's drone (see drone.h): eight sines, each a harmonic of the base tuned a hair off, breathing; the base glides
   over minutes; everything fades over seconds. Pitches become phase steps once a block, exactly enough that two
   partials a tenth of a cent apart beat at the right rate. */
#include "drone.h"
#include "harmony.h"
#include "omni.h"
#include "tables.h"
#include "platform.h"
#include "libc.h"

struct drone_state radigue;
static uint32_t rate = 48000, inc_a4 = 39370534, inc_max;       /* A4's phase step at 48 kHz; 20 kHz's */
static const uint32_t semi_q30[12] = { 1073741824, 1137589835, 1205234447, 1276901417, 1352829926, 1433273380,
                                       1518500250, 1608794974, 1704458901, 1805811301, 1913190429, 2026954652 };
static const int32_t harm_pitch[17] = { 0, 0, 12000, 19020, 24000, 27863, 31020, 33688, 36000, 38039, 39863, 41513,
                                        43020, 44405, 45688, 46883, 48000 };   /* 12000 × log2(h): a harmonic above the base */
static const int8_t pan_pc[DRONE_PARTIALS] = { -15, 15, -30, 30, -45, 45, -60, 60 };

/* the audio side's */
static struct { uint32_t ph, inc, bph; int32_t amp; } pt[DRONE_PARTIALS];
static volatile int32_t base_now = 36000, sweep_from, sweep_to;
static volatile uint32_t sweep_len, sweep_left, fade_q31, frames_on; static int sweep_shift;   /* sweep_len >> it fits 16 bits */
static int last_root = -1;

/* 2^(x/12000) as a ratio in [1, 2) in Q30 and a whole number of octaves; e^y to its cube for the last 100 cents */
static uint32_t ratio_q30(int32_t x, int *oct) {
    int32_t o = x >= 0 ? x / 12000 : -((-x + 11999) / 12000);
    uint32_t rem = (uint32_t)(x - o * 12000), r = rem % 1000;
    uint32_t y = r * 62022u, y2 = (uint32_t)(((uint64_t)y * y) >> 30), y3 = (uint32_t)(((uint64_t)y2 * y) >> 30);
    uint32_t e = (1u << 30) + y + y2 / 2 + y3 / 6;
    *oct = o;
    return (uint32_t)(((uint64_t)semi_q30[rem / 1000] * e) >> 30);
}
static uint32_t inc_of(int32_t p) {                               /* a pitch to a phase step a frame; 0 above 20 kHz */
    int oct; uint64_t inc = ((uint64_t)inc_a4 * ratio_q30(p - 69000, &oct)) >> 30;
    if (oct >= 0) { if (oct > 8) return 0; inc <<= oct; } else inc >>= MIN(-oct, 40);
    return inc >= inc_max ? 0 : (uint32_t)inc;
}
uint32_t drone_hz_milli(int32_t p) {
    int oct; uint64_t hz = ((uint64_t)440000 * ratio_q30(p - 69000, &oct)) >> 30;
    return (uint32_t)(oct >= 0 ? hz << MIN(oct, 10) : hz >> MIN(-oct, 40));
}

void drone_defaults(void) {
    static const struct drone_partial def[DRONE_PARTIALS] = {
        { 1, 0, 80, 180 }, { 1, 26, 70, 240 }, { 2, 0, 55, 120 }, { 2, -40, 45, 300 },
        { 3, 10, 35, 90 }, { 5, 0, 20, 420 }, { 5, 33, 18, 360 }, { 7, -60, 12, 600 },
    };
    uint32_t st = plat_irq_save();
    memcpy(radigue.p, def, sizeof def);
    radigue.target = base_now = 36000; radigue.sweep_s = 120; radigue.fade_s = 20; radigue.depth = 70; radigue.level = 70; radigue.playing = false;
    sweep_left = sweep_len = 0; fade_q31 = 0; frames_on = 0;
    for (int i = 0; i < DRONE_PARTIALS; i++) { pt[i].amp = 0; pt[i].bph = i ? (uint32_t)i * 2654435769u : 1u << 31; }   /* the first begins full */
    plat_irq_restore(st);
}
void drone_init(uint32_t r) {
    rate = r ? r : 48000;
    inc_a4 = (uint32_t)(((uint64_t)440 << 32) / rate); inc_max = (uint32_t)(((uint64_t)20000 << 32) / rate);
    memset(pt, 0, sizeof pt);
    drone_defaults();
}

void drone_play(bool on) {
    uint32_t st = plat_irq_save();
    if (on && !radigue.playing && !fade_q31) frames_on = 0;        /* from silence: the clock starts again */
    radigue.playing = on;
    plat_irq_restore(st);
}

static void sweep_start(int32_t t) {                              /* either side, interrupts held on the main loop's */
    radigue.target = t = CLAMP(t, 12000, 96000);
    sweep_from = base_now; sweep_to = t;
    sweep_len = sweep_left = (uint32_t)radigue.sweep_s * rate;
    for (sweep_shift = 0; (sweep_len >> sweep_shift) > 65535; sweep_shift++) ;
    if (!sweep_len) base_now = t;
}
void drone_sweep_to(int32_t t) { uint32_t st = plat_irq_save(); sweep_start(t); plat_irq_restore(st); }

bool drone_sounding(void) { return radigue.playing || fade_q31; }
uint32_t drone_seconds(void) { return frames_on / rate; }
int32_t drone_base_now(void) { return base_now; }
uint32_t drone_sweep_left_s(void) { return sweep_left ? sweep_left / rate + 1 : 0; }
int32_t drone_fade_q15(void) {                                    /* smoothstep: it leaves and arrives gently */
    uint32_t x = fade_q31 >> 16;
    return (int32_t)MIN(((x * x) >> 15) * (98304u - 2 * x) >> 15, 32767u);
}
int32_t drone_partial_pitch(int i) { const struct drone_partial *p = &radigue.p[i & 7]; return base_now + harm_pitch[CLAMP(p->harmonic, 1, 16)] + p->detune; }
static int32_t breath_q15(int i) {                                /* 32767 in, down to 1 - depth out */
    int32_t b = (32768 - sine_q15_8192[((pt[i].bph >> 19) + 2048) & 8191]) / 2;
    return 32767 - radigue.depth * (32767 - MIN(b, 32767)) / 100;
}
static int32_t amp_of(int i) {
    int32_t a = radigue.p[i].level * 70;                           /* up to 7000 a partial */
    a = (a * breath_q15(i)) >> 15; a = (a * drone_fade_q15()) >> 15;
    return a * radigue.level / 100;
}
int32_t drone_partial_amp(int i) { return amp_of(i & 7) * 32767 / 7000; }
int drone_twin(int i) {
    int best = -1, bd = 1 << 30;
    for (int j = 0; j < DRONE_PARTIALS; j++) {
        if (j == i || radigue.p[j].harmonic != radigue.p[i].harmonic || !radigue.p[j].level) continue;
        int d = radigue.p[j].detune - radigue.p[i].detune; if (d < 0) d = -d;
        if (d < bd) { bd = d; best = j; }
    }
    return best;
}
int32_t drone_beat_mhz(int i) {
    int j = drone_twin(i);
    if (j < 0) return -1;
    uint32_t a = inc_of(drone_partial_pitch(i)), b = inc_of(drone_partial_pitch(j)), d = a > b ? a - b : b - a;
    return (int32_t)(((uint64_t)d * rate * 1000) >> 32);
}
int32_t drone_pair_amp(int i) {                                   /* |a e^{iφa} + b e^{iφb}|: the pair's envelope now */
    int j = drone_twin(i);
    int32_t a = amp_of(i);
    if (j < 0) return a * 32767 / 7000;
    int32_t b = amp_of(j), c = sine_q15_8192[(((pt[i].ph - pt[j].ph) >> 19) + 2048) & 8191];
    int32_t e2 = a * a + b * b + (int32_t)(((int64_t)2 * a * b * c) >> 15);
    uint32_t v = (uint32_t)MAX(e2, 0), s = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= s + bit) { v -= s + bit; s = (s >> 1) + bit; } else s >>= 1; bit >>= 2; }
    return (int32_t)MIN(s * 32767 / 7000 / 2, 32767);              /* the two at full, together: full */
}

bool drone_render(int32_t *l, int32_t *r, uint32_t n, bool add) {
    if (harmony_on) {                                             /* keys follow the chord: the base goes to its root */
        uint8_t pcs[3]; omni_chord_tones(omni_chord_word, pcs);
        if (pcs[0] != last_root) {
            last_root = pcs[0];
            int32_t t = radigue.target, pc = ((t / 1000) % 12 + 12) % 12, up = (last_root - pc + 12) % 12;
            sweep_start((t / 1000 + (up <= 6 ? up : up - 12)) * 1000);   /* the nearest */
        }
    } else last_root = -1;
    if (!radigue.playing && !fade_q31) { if (sweep_left) { base_now = sweep_to; sweep_left = 0; } return add; }
    if (sweep_left) {                                             /* the base, evenly in pitch */
        sweep_left = sweep_left > n ? sweep_left - n : 0;
        uint32_t done = sweep_len - sweep_left, q16 = ((done >> sweep_shift) << 16) / MAX(1u, sweep_len >> sweep_shift);   /* 32-bit, 1/32768 at worst */
        base_now = sweep_left ? sweep_from + (int32_t)(((int64_t)(sweep_to - sweep_from) * q16) >> 16) : sweep_to;
    }
    uint32_t step = (uint32_t)(0x80000000u / ((uint32_t)MAX(radigue.fade_s, 1) * rate)) * n;
    fade_q31 = radigue.playing ? MIN(fade_q31 + step, 0x80000000u) : fade_q31 > step ? fade_q31 - step : 0;
    frames_on += n;
    if (!add) { memset(l, 0, n * sizeof *l); memset(r, 0, n * sizeof *r); }
    for (int i = 0; i < DRONE_PARTIALS; i++) {
        const struct drone_partial *p = &radigue.p[i];
        uint32_t inc = inc_of(base_now + harm_pitch[CLAMP(p->harmonic, 1, 16)] + p->detune), ph = pt[i].ph;
        pt[i].bph += (0xFFFFFFFFu / ((uint32_t)CLAMP(p->breath_s, 30, 600) * rate)) * n;
        int32_t a = pt[i].amp, target = inc ? amp_of(i) : 0, da = (target - a) / (int32_t)n;
        pt[i].inc = inc;
        if (!a && !target) { pt[i].ph = ph + inc * n; continue; }
        int32_t gl = pan_pc[i] > 0 ? 32767 * (100 - pan_pc[i]) / 100 : 32767, gr = pan_pc[i] < 0 ? 32767 * (100 + pan_pc[i]) / 100 : 32767;
        for (uint32_t k = 0; k < n; k++, ph += inc, a += da) {
            uint32_t idx = ph >> 19; int32_t fr = (int32_t)((ph >> 3) & 0xFFFF);
            int32_t s0 = sine_q15_8192[idx], s1 = sine_q15_8192[(idx + 1) & 8191];
            int32_t y = ((s0 + (((s1 - s0) * fr) >> 16)) * a) >> 15;
            l[k] += (y * gl) >> 15; r[k] += (y * gr) >> 15;
        }
        pt[i].ph = ph; pt[i].amp = target;
    }
    return true;
}
