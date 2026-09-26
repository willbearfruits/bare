/* GENDY's walk and wave; see gendy.h. Per sample: where the phase is in the period, and a straight line between the
   segment's two breakpoints (a multiply by the segment's reciprocal, no division). Per period: every breakpoint's walk,
   then the segments' edges as shares of 2^32 (one 32-bit division), their reciprocals and the period's mean. */
#include "gendy.h"
#include "synth.h"
#include "tables.h"
#include "libc.h"

const char *const gendy_dist_names[GD_DISTS] = { "LINEAR", "CAUCHY", "LOGIST", "HYPCOS", "ARCSIN", "EXPON" };
struct gendy_patch gendy_bank[GENDY_PATCHES];

static const struct gendy_patch defaults[GENDY_PATCHES] = {
    /* name        pts adist     ast amir ine ddist      dst dmir drift lvl cut res   a    d    r   s */
    { "GENDY3",    12, GD_CAUCHY, 34, 80, 60, GD_CAUCHY, 22, 45,  0,  70, 127,  0,   5, 300, 250, 80 },
    { "S.709",      7, GD_LOGIST, 48, 90, 35, GD_CAUCHY, 55, 75, 30,  65, 127,  0,   5, 400, 300, 85 },
    { "BREATH",    16, GD_ARCSIN, 14, 60, 85, GD_LINEAR,  8, 12,  0,  75,  92, 10, 140, 600, 700, 90 },
    { "STORM",      5, GD_CAUCHY, 80, 100, 0, GD_EXPON,  90, 100, 70, 55, 127,  0,   2, 200, 200, 90 },
};

void gendy_init(void) { memcpy(gendy_bank, defaults, sizeof gendy_bank); }

void gendy_patch_sanitize(struct gendy_patch *p) {
    p->name[sizeof p->name - 1] = 0;
    p->points = (uint8_t)CLAMP(p->points, 2, GENDY_POINTS);
    p->adist = (uint8_t)MIN(p->adist, GD_DISTS - 1); p->ddist = (uint8_t)MIN(p->ddist, GD_DISTS - 1);
    p->astep = (uint8_t)MIN(p->astep, 100); p->dstep = (uint8_t)MIN(p->dstep, 100);
    p->amirror = (uint8_t)CLAMP(p->amirror, 5, 100); p->dmirror = (uint8_t)MIN(p->dmirror, 100);
    p->inertia = (uint8_t)MIN(p->inertia, 100); p->drift = (uint8_t)MIN(p->drift, 100);
    p->level = (uint8_t)MIN(p->level, 100); p->cutoff = (uint8_t)MIN(p->cutoff, 127); p->reso = (uint8_t)MIN(p->reso, 100);
    p->a_ms = (uint16_t)MIN(p->a_ms, 4000); p->d_ms = (uint16_t)MIN(p->d_ms, 8000); p->r_ms = (uint16_t)MIN(p->r_ms, 8000);
    p->s_pct = (uint8_t)MIN(p->s_pct, 100);
}

static inline uint32_t xorshift(uint32_t x) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
static inline int32_t mirror(int32_t v, int32_t lo, int32_t hi) {  /* thrown back from the walls, then kept inside */
    if (v > hi) v = 2 * hi - v;
    if (v < lo) v = 2 * lo - v;
    return CLAMP(v, lo, hi);
}
static int32_t amax_of(const struct gendy_patch *p) { return 327 * CLAMP(p->amirror, 5, 100); }
static void len_range(const struct gendy_patch *p, int32_t *lo, int32_t *hi) {   /* up to 8 times either way of 256 */
    int32_t r8 = 8 + p->dmirror * 56 / 100;                                        /* the ratio, eighths: 1 .. 8 */
    *lo = 256 * 8 / r8; *hi = 256 * r8 / 8;
}

/* the edges as shares of the period, each segment's reciprocal, and the period's mean */
static void edges(struct gendy_osc *g) {
    int n = g->n;
    uint32_t total = 0; for (int i = 0; i < n; i++) total += g->len[i];
    uint32_t scale = 0xFFFFFFFFu / MAX(total, 1u), cum = 0;
    int32_t sum = 0;
    g->edge[0] = 0;
    for (int i = 0; i < n; i++) {
        cum += g->len[i];
        g->edge[i + 1] = i + 1 == n ? 0xFFFFFFFFu : cum * scale;
        g->recip[i] = (1u << 30) / MAX(1u, (g->edge[i + 1] - g->edge[i]) >> 16);
        sum += ((g->amp[i] + g->amp[i + 1]) >> 2) * g->len[i];
    }
    g->dc = (int32_t)(sum * 2 / (int32_t)MAX(total, 1u));
}

void gendy_period(struct gendy_osc *g, const struct gendy_patch *p) {
    int n = g->n;
    uint32_t r = g->rng;
    const int16_t *ad = gendy_dist_q15 + MIN(p->adist, GD_DISTS - 1) * 256, *dd = gendy_dist_q15 + MIN(p->ddist, GD_DISTS - 1) * 256;
    int32_t amax = amax_of(p), astep = 1 + p->astep * p->astep * 82 / 100;         /* up to a quarter of full scale */
    int32_t keep = p->inertia * 655, lo, hi;                                        /* Q16 */
    len_range(p, &lo, &hi);
    int32_t dstep = (hi - lo) * p->dstep / 200;
    for (int i = 0; i < n; i++) {
        r = xorshift(r);
        int32_t v = ((g->vel[i] * keep) >> 16) + ((ad[r >> 24] * astep) >> 15);     /* the step's own walk, then the height's */
        v = mirror(v, -astep, astep);
        g->vel[i] = (int16_t)v;
        g->amp[i] = (int16_t)mirror(g->amp[i] + v, -amax, amax);
        r = xorshift(r);
        g->len[i] = (uint16_t)mirror(g->len[i] + ((dd[(r >> 16) & 255] * dstep) >> 15), lo, hi);
    }
    g->amp[n] = g->amp[0];
    if (p->drift) {                                                                 /* the period walks: two octaves at most */
        int32_t bound = p->drift * 6144 / 100, st = 1 + bound * p->dstep / 400;
        r = xorshift(r);
        g->lp = mirror(g->lp + ((dd[r >> 24] * st) >> 15), -bound, bound);
        g->pmul = synth_bend_mul(g->lp);
    } else { g->lp = 0; g->pmul = 65536; }
    g->rng = r;
    g->periods++;
    edges(g);
}

void gendy_osc_start(struct gendy_osc *g, const struct gendy_patch *p, uint32_t seed) {
    memset(g, 0, sizeof *g);
    g->rng = seed | 1; g->pmul = 65536;
    g->n = (uint8_t)CLAMP(p->points, 2, GENDY_POINTS);
    int32_t amax = amax_of(p);
    for (int i = 0; i < g->n; i++) {                                                /* a rounded start, then it wanders off */
        g->rng = xorshift(g->rng);
        g->amp[i] = (int16_t)CLAMP(((sine_q15_8192[(i * 8192 / g->n) & 8191] * amax) >> 15) * 3 / 5 + (int32_t)(g->rng % 1025) - 512, -amax, amax);
        g->len[i] = 256;
    }
    g->amp[g->n] = g->amp[0];
    edges(g);
}

static inline uint32_t stepped(uint32_t inc, uint32_t pmul) {                    /* the drifted increment, below Nyquist */
    if (pmul == 65536) return inc;
    uint64_t s = ((uint64_t)inc * pmul) >> 16;
    return s > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)s;
}
void gendy_render(struct gendy_osc *g, const struct gendy_patch *p, int32_t *out, int n, uint32_t inc) {
    uint32_t step = stepped(inc, g->pmul), ph = g->ph;
    int k = g->seg, last = g->n - 1;
    for (int i = 0; i < n; i++) {
        uint32_t nx = ph + step;
        if (nx < ph) {                                                              /* a period ended: the walk */
            gendy_period(g, p); k = 0; last = g->n - 1;
            step = stepped(inc, g->pmul);
        }
        ph = nx;
        while (k < last && ph >= g->edge[k + 1]) k++;
        int32_t fr = (int32_t)((((ph - g->edge[k]) >> 16) * g->recip[k]) >> 15);   /* 0..32767 across the segment */
        int32_t a0 = g->amp[k], a1 = g->amp[k + 1];
        int32_t s = ((a0 + (((a1 - a0) * fr) >> 15) - g->dc) * 3) >> 2;           /* without the mean it can reach twice */
        out[i] = CLAMP(s, -32767, 32767);                                          /* the mirrors: ¾, and kept in range */
    }
    g->ph = ph; g->seg = (uint8_t)k;
}
