#include "stretch.h"
#include "synth.h"
#include "keys.h"
#include "libc.h"
#include "platform.h"
#include "tables.h"

struct stretch_state stretch;
static uint32_t rate;

#define CAP_LEN (48000 * STRETCH_CAP_SEC)
static int16_t cap[CAP_LEN];
static volatile uint32_t cap_head;

#define FIFO_LEN (STRETCH_MAX_WIN * 4)               /* stereo frames, power of two */
static int16_t fifo[FIFO_LEN * 2];
static volatile uint32_t fifo_w, fifo_r;

static int32_t re[STRETCH_MAX_WIN], im[STRETCH_MAX_WIN];
static int32_t mag[STRETCH_MAX_WIN / 2 + 1];
static int32_t ola_l[STRETCH_MAX_WIN * 2], ola_r[STRETCH_MAX_WIN * 2];   /* overlap-add rings of 2 windows */
static uint32_t ola_pos;
static uint32_t rng = 0x1234567u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

void stretch_init(uint32_t r) {
    rate = r;
    memset(&stretch, 0, sizeof stretch);
    stretch.factor = 16; stretch.win = 4096; stretch.mix = 70;
    stretch.cap_len = CAP_LEN;
    cap_head = 0; fifo_w = fifo_r = 0; ola_pos = 0;
}

const int16_t *stretch_capture_buf(uint32_t *head) { *head = cap_head; return cap; }

static volatile bool held;                      /* the sampler is reading or filling the ring */
void stretch_hold(bool on) { held = on; }
int16_t *stretch_ring(uint32_t *len) { *len = CAP_LEN; return cap; }

void stretch_capture(const int32_t *l, const int32_t *r, uint32_t n) {
    if (stretch.frozen || held) return;         /* the frozen material must not be overwritten by what you play on top */
    uint32_t h = cap_head;
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = (l[i] + r[i]) >> 1; if (x > 32767) x = 32767; else if (x < -32767) x = -32767;
        cap[h] = (int16_t)x; h = h + 1 == CAP_LEN ? 0 : h + 1;
    }
    cap_head = h;
}

void stretch_pull(int32_t *l, int32_t *r, uint32_t n) {
    if (!stretch.frozen) return;
    int32_t g = stretch.mix * 327;        /* Q15 */
    for (uint32_t i = 0; i < n; i++) {
        if (fifo_r == fifo_w) return;
        uint32_t k = (fifo_r & (FIFO_LEN - 1)) * 2;
        l[i] += (fifo[k] * g) >> 15; r[i] += (fifo[k + 1] * g) >> 15;
        fifo_r++;
    }
}

/* ---- the phrase to freeze: walk back from now in 10 ms blocks to the last sound, then back to the silence before it ---- */
static int32_t block_peak(uint32_t end, uint32_t len) {           /* the len samples before ring position end */
    int32_t p = 0; uint32_t i = (end + CAP_LEN - len) % CAP_LEN;
    for (uint32_t k = 0; k < len; k++) { int32_t a = cap[i]; if (a < 0) a = -a; if (a > p) p = a; if (++i == CAP_LEN) i = 0; }
    return p;
}
/* the region as ring start + length; false when nothing was played (the region is then the last window) */
bool stretch_phrase(uint32_t *start_out, uint32_t *len_out) {
    uint32_t blk = rate / 100, nblk = CAP_LEN / blk, head = cap_head;
    int last = -1;
    for (uint32_t b = 0; b < nblk; b++) if (block_peak((head + CAP_LEN - b * blk) % CAP_LEN, blk) > 600) { last = (int)b; break; }
    uint32_t start_back, end_back;                 /* in blocks before now */
    if (last < 0) { start_back = (stretch.win + blk - 1) / blk; end_back = 0; }       /* nothing played: the last window */
    else {
        int first = last, quiet = 0;
        for (uint32_t b = (uint32_t)last; b < nblk - 1; b++) {
            if (block_peak((head + CAP_LEN - b * blk) % CAP_LEN, blk) > 150) { first = (int)b; quiet = 0; }
            else if (++quiet >= 40) break;         /* 400 ms of quiet: the phrase starts after it */
        }
        start_back = (uint32_t)first + 1;
        end_back = last > 10 ? (uint32_t)last - 10 : 0;   /* keep 100 ms of the sound's tail */
    }
    uint32_t len = (start_back - end_back) * blk;
    if (len < stretch.win) len = stretch.win;
    *len_out = len;
    *start_out = (head + CAP_LEN - end_back * blk - len) % CAP_LEN;
    return last >= 0;
}

static void restart(void) {
    uint32_t st = plat_irq_save();
    fifo_w = fifo_r = 0;
    plat_irq_restore(st);
    ola_pos = 0; memset(ola_l, 0, sizeof ola_l); memset(ola_r, 0, sizeof ola_r);
}

/* freeze on a region the caller put into the ring (the sampler: a sample to stretch) */
void stretch_freeze_region(uint32_t start, uint32_t len) {
    stretch.frozen = true;
    stretch.region_start = start % CAP_LEN;
    stretch.region_len = CLAMP(len, (uint32_t)stretch.win, (uint32_t)CAP_LEN);
    stretch.pos = stretch.region_start;
    restart();
}

void stretch_freeze(bool on) {
    if (on == stretch.frozen) return;
    if (on) {
        stretch.frozen = true;                  /* capture stops first, so the ring holds still while we look at it */
        stretch_phrase(&stretch.region_start, &stretch.region_len);
        stretch.pos = stretch.region_start;
        restart();
    } else stretch.frozen = false;
}

/* In-place radix-2 FFT of n complex points (n a power of two up to 8192). Block floating point: each stage looks at
   how big the data has become and scales its output down by 0, 1 or 2 bits, so values stay below 2^14 — products fit
   32 bits — while keeping as many significant bits as possible. Returns the total down-shift: out = DFT(in) * 2^-shift. */
static int fft(int32_t *xr, int32_t *xi, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; i++) {                       /* bit reversal */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { int32_t t = xr[i]; xr[i] = xr[j]; xr[j] = t; t = xi[i]; xi[i] = xi[j]; xi[j] = t; }
    }
    uint32_t m = 0;
    for (int i = 0; i < n; i++) m |= (uint32_t)(xr[i] < 0 ? -xr[i] : xr[i]) | (uint32_t)(xi[i] < 0 ? -xi[i] : xi[i]);
    int total = 0;
    for (int len = 2; len <= n; len <<= 1) {
        int s = m >= 8192 ? 2 : m >= 4096 ? 1 : 0;             /* worst-case growth per stage is 1 + sqrt 2 */
        int32_t round = s ? 1 << (s - 1) : 0;
        total += s; m = 0;
        int half = len >> 1, step = 8192 / len;
        for (int k = 0; k < half; k++) {
            int32_t wr = sine_q15_8192[(k * step + 2048) & 8191], wi = sine_q15_8192[k * step];
            if (!inverse) wi = -wi;
            for (int a = k; a < n; a += len) {
                int b = a + half;
                int32_t tr = (xr[b] * wr - xi[b] * wi) >> 15, ti = (xr[b] * wi + xi[b] * wr) >> 15;
                int32_t ar = xr[a] + round, ai = xi[a] + round;
                int32_t o0 = (ar + tr) >> s, o1 = (ai + ti) >> s, o2 = (ar - tr) >> s, o3 = (ai - ti) >> s;
                xr[a] = o0; xi[a] = o1; xr[b] = o2; xi[b] = o3;
                m |= (uint32_t)(o0 < 0 ? -o0 : o0) | (uint32_t)(o1 < 0 ? -o1 : o1) | (uint32_t)(o2 < 0 ? -o2 : o2) | (uint32_t)(o3 < 0 ? -o3 : o3);
            }
        }
    }
    return total;
}

/* Hann window for a power-of-two length n = 8192 >> shift, straight from the sine table */
static inline int32_t hann(int i, int shift) { return (32767 - sine_q15_8192[(2048 + (i << shift)) & 8191]) >> 1; }
/* |z| within 4 %: alpha max plus beta min */
static inline int32_t magnitude(int32_t x, int32_t y) {
    if (x < 0) x = -x; if (y < 0) y = -y;
    int32_t a = x > y ? x : y, b = x > y ? y : x;
    return (a * 31472 + b * 13036) >> 15;
}
static int ilog2(uint32_t v) { int n = -1; while (v) { v >>= 1; n++; } return n; }
static inline int32_t shift_by(int32_t v, int e) { return e >= 0 ? v << e : v >> -e; }

static void emit_hop(int hop) {
    for (int i = 0; i < hop; i++) {
        uint32_t idx = (ola_pos + (uint32_t)i) & (2 * STRETCH_MAX_WIN - 1);
        int32_t l = CLAMP(ola_l[idx], -32767, 32767), r = CLAMP(ola_r[idx], -32767, 32767);
        ola_l[idx] = 0; ola_r[idx] = 0;
        uint32_t k = (fifo_w & (FIFO_LEN - 1)) * 2;
        fifo[k] = (int16_t)l; fifo[k + 1] = (int16_t)r;
        __asm__ volatile("" ::: "memory");                /* the samples are stored before the interrupt can see them */
        fifo_w++;
    }
    ola_pos = (ola_pos + (uint32_t)hop) & (2 * STRETCH_MAX_WIN - 1);
}

static void advance(int hop) {
    if (stretch.stay) return;
    uint32_t adv = (uint32_t)hop / stretch.factor; if (adv == 0) adv = 1;
    /* distance from the region's start, so the read window loops back before it runs off the phrase */
    uint32_t into = (stretch.pos + CAP_LEN - stretch.region_start) % CAP_LEN;
    uint32_t room = stretch.region_len > stretch.win ? stretch.region_len - stretch.win : 0;
    if (into > stretch.region_len || into + adv > room) stretch.pos = stretch.region_start;
    else stretch.pos = (stretch.pos + adv) % CAP_LEN;
}

static void compute_frame(void) {
    int n = stretch.win, half = n / 2, hop = n / 4;
    int shift = 0; while ((8192 >> shift) > n) shift++;
    int lg = ilog2((uint32_t)n);
    /* the window at pos, Hann-weighted, two real samples per complex point */
    uint32_t idx = stretch.pos; int32_t peak = 0;
    for (int i = 0; i < n; i += 2) {
        int32_t a = (cap[idx] * hann(i, shift)) >> 15; if (++idx == CAP_LEN) idx = 0;
        int32_t b = (cap[idx] * hann(i + 1, shift)) >> 15; if (++idx == CAP_LEN) idx = 0;
        re[i >> 1] = a; im[i >> 1] = b;
        if (a < 0) a = -a; if (b < 0) b = -b;
        if (a > peak) peak = a; if (b > peak) peak = b;
    }
    if (peak == 0) { memset(stretch.bands, 0, sizeof stretch.bands); emit_hop(hop); advance(hop); return; }
    int e = 0;                                         /* normalise into [4096, 8192): e is the scale in bits */
    while (peak >= 8192) { peak >>= 1; e--; }
    while (peak < 4096) { peak <<= 1; e++; }
    for (int i = 0; i < half; i++) { re[i] = shift_by(re[i], e); im[i] = shift_by(im[i], e); }
    int ex = e - fft(re, im, half, false);             /* spectrum values are true DFT * 2^ex */
    /* untangle the half-size transform into the real signal's spectrum, keeping magnitudes */
    int32_t mmax = 0;
    for (int k = 0; k <= half; k++) {
        int a = k % half, b = (half - k) % half;
        int32_t ar = re[a], ai = im[a], br = re[b], bi = -im[b];
        int32_t fer = (ar + br) >> 1, fei = (ai + bi) >> 1, dr = (ar - br) >> 1, di = (ai - bi) >> 1;
        int32_t c = sine_q15_8192[((k << shift) + 2048) & 8191], sn = sine_q15_8192[(k << shift) & 8191];
        int32_t xr = fer + ((c * di - sn * dr) >> 15), xi = fei + ((-c * dr - sn * di) >> 15);
        mag[k] = magnitude(xr, xi);
        if (mag[k] > mmax) mmax = mag[k];
    }
    /* spectrum display: 4 bands per octave from ~23 Hz, placed by frequency (bin index at 8192-point resolution),
       level in dB over a 60 dB range — a Hann-windowed full-scale sine reads 0 dB (its peak bin is A * n / 4) */
    memset(stretch.bands, 0, sizeof stretch.bands);
    for (int k = 1; k < half; k++) {
        uint32_t kf = (uint32_t)k << shift;
        int bl = ilog2(kf);
        if (bl < 2 || !mag[k]) continue;
        int b = (bl - 2) * 4 + (int)((kf >> (bl - 2)) & 3);
        if (b >= STRETCH_BANDS) continue;
        int ml = ilog2((uint32_t)mag[k]);
        int quarter = (mag[k] >> MAX(0, ml - 2)) & 3;              /* next two bits: a quarter-octave of level */
        int db = (ml + 2 - ex - lg - 15) * 6 + quarter * 3 / 2;
        int v = (db + 60) * 100 / 60;
        if (v > stretch.bands[b]) stretch.bands[b] = (uint8_t)CLAMP(v, 0, 100);
    }
    /* new phases: independent random ones for left and right, packed as one complex spectrum L + iR */
    int p = 0;                                          /* bring magnitudes into [4096, 8192) */
    while (mmax >= 8192) { mmax >>= 1; p--; }
    while (mmax && mmax < 4096) { mmax <<= 1; p++; }
    re[0] = im[0] = 0; re[half] = im[half] = 0;        /* no DC, no Nyquist */
    for (int k = 1; k < half; k++) {
        int32_t m = shift_by(mag[k], p);
        uint32_t r = rnd(), pl = r & 8191, pr = (r >> 13) & 8191;
        int32_t lr = (m * sine_q15_8192[(pl + 2048) & 8191]) >> 15, li = (m * sine_q15_8192[pl]) >> 15;
        int32_t rr = (m * sine_q15_8192[(pr + 2048) & 8191]) >> 15, ri = (m * sine_q15_8192[pr]) >> 15;
        re[k] = lr - ri;     im[k] = li + rr;           /* Z[k]   = L[k] + i R[k]               */
        re[n - k] = lr + ri; im[n - k] = rr - li;       /* Z[n-k] = conj(L[k]) + i conj(R[k])   */
    }
    int s = fft(re, im, n, true);
    /* back to sample scale: y = x * 2^(s - ex - p - lg); window it, times 4/3 (random-phase frames at 75 % overlap
       add up to 3/4 of the input's level under a Hann² weighting), and overlap-add */
    int total = s - ex - p - lg - 29;                   /* the window (Q15) times the gain (Q14) is Q29 */
    for (int i = 0; i < n; i++) {
        int64_t w = (int64_t)hann(i, shift) * 21845;    /* Q15 * (4/3 in Q14) */
        int64_t vl = re[i] * w, vr = im[i] * w;
        if (total >= 0) { vl <<= total; vr <<= total; } else { vl >>= -total; vr >>= -total; }
        uint32_t o = (ola_pos + (uint32_t)i) & (2 * STRETCH_MAX_WIN - 1);
        ola_l[o] += (int32_t)CLAMP(vl, -1000000, 1000000);
        ola_r[o] += (int32_t)CLAMP(vr, -1000000, 1000000);
    }
    emit_hop(hop);
    advance(hop);
}

void stretch_work(void) {
    if (!stretch.frozen) return;
    int guard = 0;
    while (fifo_w - fifo_r < (uint32_t)stretch.win && guard++ < 4) compute_frame();
}

bool stretch_key(uint8_t code, bool down, uint64_t now) {
    (void)now;
    if (!down) return false;
    switch (code) {
    case KEY_SPACE: stretch_freeze(!stretch.frozen); return true;
    case KEY_ENTER: stretch.stay = !stretch.stay; return true;
    case KEY_RIGHT: if (stretch.factor < 1024) stretch.factor *= 2; return true;
    case KEY_LEFT:  if (stretch.factor > 1) stretch.factor /= 2; return true;
    case KEY_UP:    if (stretch.win < STRETCH_MAX_WIN) stretch.win *= 2; return true;
    case KEY_DOWN:  if (stretch.win > 1024) stretch.win /= 2; return true;
    case '[': stretch.pos = (stretch.pos + CAP_LEN - rate / 4) % CAP_LEN; return true;
    case ']': stretch.pos = (stretch.pos + rate / 4) % CAP_LEN; return true;
    case '-': if (stretch.mix >= 5) stretch.mix -= 5; return true;
    case '=': if (stretch.mix <= 95) stretch.mix += 5; return true;
    }
    return false;
}
