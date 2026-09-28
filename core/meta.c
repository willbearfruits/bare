/* METASTASEIS: families of string glissandi between two guides (see meta.h). The main loop compiles the families into
   strings; the audio side plays whichever the cursor is crossing, each with its bow drawn in and out. */
#include "meta.h"
#include "harmony.h"
#include "upic.h"
#include "seq.h"
#include "synth.h"
#include "tables.h"
#include "platform.h"
#include "libc.h"

struct meta_state meta;
const char *const meta_section_names[META_SECTIONS] = { "violins I", "violins II", "violas", "cellos", "basses", "the orchestra" };

static uint32_t rate = 48000, inc_a4;
static struct meta_string strs[META_STRINGS];      /* the audio side reads these */
static int n_strs;
static struct { uint32_t ph, vib, vinc; int32_t env, pitch; } rt[META_STRINGS];
static int16_t tone[2048];                         /* one cycle: harmonics 1, 1/2, 1/3, 1/4 — a bowed string's, roughly */
static int32_t recip_sqrt[META_VOICES + 1];        /* 32767 / sqrt(k): the mass keeps its loudness however many play */
static bool sounding;

static int32_t isqrt(uint32_t v) { uint32_t r = 0, b = 1u << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (int32_t)r; }

void meta_init(uint32_t r) {
    rate = r ? r : 48000;
    inc_a4 = (uint32_t)((440ull << 32) / rate);
    for (int i = 0; i < 2048; i++) {
        uint32_t p = (uint32_t)i << 21;
        int32_t s = sine_q15_8192[p >> 19] + (sine_q15_8192[(p * 2) >> 19] >> 1) + sine_q15_8192[(p * 3) >> 19] / 3 + (sine_q15_8192[(p * 4) >> 19] >> 2);
        tone[i] = (int16_t)(s * 15 / 32);
    }
    recip_sqrt[0] = 32767;
    for (int k = 1; k <= META_VOICES; k++) recip_sqrt[k] = (int32_t)(32767 * 1024 / isqrt((uint32_t)k << 20));
    uint32_t seed = 0x4D455441u;
    for (int i = 0; i < META_STRINGS; i++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; rt[i].ph = seed;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; rt[i].vib = seed;
        rt[i].vinc = (uint32_t)(((uint64_t)(5 * 256 + seed % 300) << 24) / rate);   /* ~5 Hz, each its own */
    }
    meta_defaults();
}

/* the opening of Metastaseis, roughly: every string on one G, fanning out into a cluster over five octaves; then a
   second family crossing from the cluster onto a narrower band, the lines' envelope a curve */
void meta_defaults(void) {
    memset(meta.fam, 0, sizeof meta.fam);
    meta.fam[0] = (struct meta_family){ { 1500, 55 * 256, 1500, 55 * 256 }, { 26000, 36 * 256, 26000, 96 * 256 }, 46, false, false, true, META_SPLIT, 80 };
    meta.fam[1] = (struct meta_family){ { 30000, 38 * 256, 40000, 94 * 256 }, { 44000, 90 * 256, 62000, 46 * 256 }, 24, false, true, true, META_VLN1, 70 };
    meta.fam[2] = (struct meta_family){ { 10000, 48 * 256, 30000, 60 * 256 }, { 20000, 72 * 256, 50000, 84 * 256 }, 12, true, false, false, META_VC, 70 };
    meta.fam[3] = (struct meta_family){ { 40000, 40 * 256, 64000, 40 * 256 }, { 40000, 80 * 256, 64000, 80 * 256 }, 16, true, false, false, META_CB, 70 };
    meta.bars = 0; meta.seconds = 20; meta.playing = false; meta.pos = 0;
    meta_compile();
}

int meta_section_of(const struct meta_string *s, int k, int n) {     /* SPLIT: the score's 12 + 12 + 8 + 8 + 6, top down */
    (void)s;
    int i = k * 46 / MAX(1, n);
    return i < 12 ? META_VLN1 : i < 24 ? META_VLN2 : i < 32 ? META_VLA : i < 40 ? META_VC : META_CB;
}

/* where the i-th of n strings meets a guide: evenly, or with gaps in the golden section's proportions (the Modulor's
   series: Fibonacci numbers, each gap about 1.618 times the last, starting again) */
static uint32_t spacing(int i, int n, bool modulor) {
    if (n < 2) return 0;
    if (!modulor) return (uint32_t)((uint64_t)i * 65536 / (uint32_t)(n - 1));
    static const uint8_t fib[7] = { 1, 1, 2, 3, 5, 8, 13 };
    uint32_t total = 0, at = 0;
    for (int k = 0; k < n - 1; k++) { total += fib[k % 7]; if (k < i) at += fib[k % 7]; }
    return (uint32_t)((uint64_t)at * 65536 / total);
}
static int32_t lerp(int32_t a, int32_t b, uint32_t u16) { return a + (int32_t)(((int64_t)(b - a) * u16) >> 16); }

void meta_compile(void) {
    static struct meta_string next[META_STRINGS];
    int n = 0;
    for (int f = 0; f < META_FAMILIES; f++) {
        const struct meta_family *F = &meta.fam[f];
        if (!F->on) continue;
        int k = CLAMP(F->n, 2, META_MAX);
        for (int i = 0; i < k; i++) {
            uint32_t u = spacing(i, k, F->modulor), v = F->cross ? 65536 - u : u;
            int32_t ta = lerp(F->a.t0, F->a.t1, u), pa = lerp(F->a.p0, F->a.p1, u);
            int32_t tb = lerp(F->b.t0, F->b.t1, v), pb = lerp(F->b.p0, F->b.p1, v);
            if (ta > tb) { int32_t x = ta; ta = tb; tb = x; x = pa; pa = pb; pb = x; }
            if (tb - ta < 256) tb = MIN(65535, ta + 256);                  /* a line straight up: a short note */
            pb = harmony_snap_q8(pb);                                     /* keys follow the chord: the strings land on it */
            struct meta_string *s = &next[n++];
            s->t0 = (uint16_t)CLAMP(ta, 0, 65535); s->t1 = (uint16_t)CLAMP(tb, 0, 65535);
            s->p0 = (uint16_t)CLAMP(pa, UPIC_LO, UPIC_HI); s->p1 = (uint16_t)CLAMP(pb, UPIC_LO, UPIC_HI);
            s->fam = (uint8_t)f;
            s->section = (uint8_t)(F->section == META_SPLIT ? meta_section_of(s, i, k) : F->section);
        }
    }
    uint32_t st = plat_irq_save();
    memcpy(strs, next, (size_t)n * sizeof strs[0]); n_strs = n;
    plat_irq_restore(st);
    meta.changes++;
}
int meta_strings(const struct meta_string **out) { *out = strs; return n_strs; }

/* ---- the audio side ---- */
static uint32_t frames_per_pass(void) {
    if (!meta.bars) return (uint32_t)CLAMP(meta.seconds, 1, 240) * rate;
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    return (uint32_t)(((uint64_t)meta.bars * 4 * 60 * rate << 16) / bpm_q16);
}
/* a pitch (MIDI note x256) to a phase step: A4's, by octaves and semitones, the fraction between them linear */
static uint32_t inc_of(int32_t m) {
    static const uint32_t st[13] = { 65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072 };
    int32_t s = m - 69 * 256, oct = 0;
    while (s < 0) { s += 12 * 256; oct--; }
    while (s >= 12 * 256) { s -= 12 * 256; oct++; }
    int k = s >> 8, fr = s & 255;
    uint32_t ratio = st[k] + (uint32_t)(((st[k + 1] - st[k]) * (uint32_t)fr) >> 8);
    uint32_t inc = (uint32_t)(((uint64_t)inc_a4 * ratio) >> 16);
    return oct >= 0 ? inc << oct : inc >> -oct;
}

bool meta_render(int32_t *l, int32_t *r, uint32_t n, bool add) {
    if (!meta.playing && !sounding) return add;
    uint32_t pos = meta.pos;
    int t = (int)(pos >> 16);
    int count = 0;                                                         /* how many the cursor is crossing */
    if (meta.playing) for (int i = 0; i < n_strs && count < META_VOICES; i++) if (t >= strs[i].t0 && t <= strs[i].t1) count++;
    int32_t norm = recip_sqrt[MAX(count, 1)];
    int32_t est = (int32_t)(32767 * n / (rate / 40));                     /* the bow in and out: 25 ms */
    bool any = add, left = false;
    int used = 0;
    for (int i = 0; i < n_strs; i++) {
        const struct meta_string *s = &strs[i];
        bool in = meta.playing && t >= s->t0 && t <= s->t1 && used < META_VOICES;
        int32_t target = in ? (int32_t)((6000 * norm) >> 15) * meta.fam[s->fam].level / 100 : 0;
        int32_t e0 = rt[i].env, e1 = e0 < target ? MIN(target, e0 + est) : MAX(target, e0 - est);
        if (!e0 && !e1) continue;
        if (in) { used++; rt[i].pitch = (int32_t)s->p0 + ((int32_t)(s->p1 - s->p0) * (t - s->t0)) / MAX(1, s->t1 - s->t0); }
        rt[i].env = e1; left = true;
        if (!any) { memset(l, 0, n * sizeof *l); memset(r, 0, n * sizeof *r); any = true; }
        rt[i].vib += rt[i].vinc * n;
        uint32_t inc = inc_of(rt[i].pitch + ((sine_q15_8192[rt[i].vib >> 19] * 12) >> 15)), ph = rt[i].ph;
        static const int16_t pan[5] = { -170, -60, 30, 110, 170 };          /* the sections, left to right, as seated */
        int32_t gl = 256 - pan[s->section % 5], gr = 256 + pan[s->section % 5];
        int32_t g = e0, dg = (e1 - e0) / (int32_t)n;
        for (uint32_t k = 0; k < n; k++, g += dg) {
            int32_t y = (tone[ph >> 21] * g) >> 15;
            l[k] += (y * gl) >> 8; r[k] += (y * gr) >> 8;
            ph += inc;
        }
        rt[i].ph = ph;
    }
    sounding = left;
    if (meta.playing) {                                                    /* the page loops; its step worked out when the length changes */
        static uint32_t step, key;
        uint32_t k = (uint32_t)meta.bars << 24 ^ (uint32_t)meta.seconds << 16 ^ (seq_link_q16 ? seq_link_q16 >> 8 : seq.bpm);
        if (k != key || !step) { key = k; step = (uint32_t)(((uint64_t)1 << 32) / MAX(1u, frames_per_pass())); }
        meta.pos = pos + step * n;
    }
    return any;
}

int meta_write_upic(int family) {
    int k = 0;
    for (int i = 0; i < n_strs; i++) if (family < 0 || strs[i].fam == family) k++;
    if (!k || upic.narcs + k > UPIC_ARCS || upic.npts + 2 * k > UPIC_POINTS) return -1;
    for (int i = 0; i < n_strs; i++) {
        const struct meta_string *s = &strs[i];
        if (family >= 0 && s->fam != family) continue;
        struct upic_pt p[2] = { { s->t0, s->p0 }, { s->t1, s->p1 } };
        upic_add(p, 2, P_STRINGS, (uint8_t)(meta.fam[s->fam].level * 127 / 100));
    }
    return k;
}
