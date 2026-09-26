#include "fx.h"
#include "libc.h"
#include "tables.h"

struct fx_state fx;
const char *const fx_echo_div_names[FX_ECHO_DIVS] = { "1/16", "1/8", "1/8.", "1/4", "1/4." };
const uint8_t fx_echo_div_q4[FX_ECHO_DIVS] = { 1, 2, 3, 4, 6 };

/* ---- reverb: two diffusing allpasses into four delay lines that feed each other through a Hadamard matrix, each
   with a low-pass in its loop (the damping). Left is lines 0 + 2, right 1 + 3, so the tail is wide. ---- */
#define LINES 4
#define MAXLINE 5400                               /* room for 48 kHz and a bit more */
static const uint16_t line_base[LINES] = { 1553, 1867, 2251, 2683 }, ap_base[2] = { 142, 379 };
static int16_t line[LINES][MAXLINE], ap[2][400];
static uint32_t llen[LINES], lpos[LINES], aplen[2], appos[2];
static int32_t damp_st[LINES];
static uint32_t rev_silent;                        /* frames the lines have held only silence */

/* ---- master inserts ---- */
static int32_t f_low[2], f_band[2];
static int32_t hold_l, hold_r; static uint32_t hold_n;

void fx_init(uint32_t rate) {
    memset(&fx, 0, sizeof fx);
    fx.rev_size = 60; fx.rev_damp = 40; fx.rev_level = 70;
    fx.echo_div = 2; fx.echo_feedback = 37;
    fx.filter_mode = FXF_LOW; fx.filter_cut = 90; fx.filter_res = 30;
    fx.drive = 40; fx.crush_bits = 8; fx.crush_rate = 4;
    for (int k = 0; k < LINES; k++) { llen[k] = MIN(MAXLINE, line_base[k] * rate / 48000); lpos[k] = 0; damp_st[k] = 0; }
    for (int k = 0; k < 2; k++) { aplen[k] = MIN(400, ap_base[k] * rate / 48000); appos[k] = 0; }
    memset(line, 0, sizeof line); memset(ap, 0, sizeof ap);
    rev_silent = MAXLINE * 8;
}

static inline int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : x; }

void fx_reverb(const int32_t *send, int32_t *l, int32_t *r, uint32_t n) {
    int32_t loud = 0;
    for (uint32_t i = 0; i < n; i++) loud |= send[i];
    if (!loud && rev_silent > MAXLINE * 8) return;                 /* nothing going in, the tail long gone */
    int32_t g = 22938 + fx.rev_size * 92;                          /* feedback, Q15: 0.70 .. 0.98 */
    int32_t a = 32767 - fx.rev_damp * 290;                         /* the loop low-pass: bright .. dark */
    int32_t out = fx.rev_level * 328;
    int32_t peak = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = sat16(send[i]) >> 1;
        for (int k = 0; k < 2; k++) {                              /* diffusion: allpass, g = 0.6 */
            int32_t v = ap[k][appos[k]], w = sat16(x + ((v * 19661) >> 15));
            ap[k][appos[k]] = (int16_t)w;
            x = v - ((w * 19661) >> 15);
            if (++appos[k] >= aplen[k]) appos[k] = 0;
        }
        int32_t o0 = line[0][lpos[0]], o1 = line[1][lpos[1]], o2 = line[2][lpos[2]], o3 = line[3][lpos[3]];
        int32_t s1 = o0 + o1, s2 = o0 - o1, s3 = o2 + o3, s4 = o2 - o3;
        int32_t h[LINES] = { (s1 + s3) >> 1, (s2 + s4) >> 1, (s1 - s3) >> 1, (s2 - s4) >> 1 };
        for (int k = 0; k < LINES; k++) {
            damp_st[k] += ((h[k] - damp_st[k]) * a) >> 15;
            line[k][lpos[k]] = (int16_t)sat16(((damp_st[k] * g) >> 15) + (k & 1 ? -x : x));
            if (++lpos[k] >= llen[k]) lpos[k] = 0;
        }
        int32_t rl = ((o0 + o2) * out) >> 15, rr = ((o1 + o3) * out) >> 15;
        l[i] += rl; r[i] += rr;
        peak |= o0 | o1 | o2 | o3;
    }
    rev_silent = peak ? 0 : rev_silent + n;
}

/* filter (the voices' state-variable filter, on the mix) → drive (a cubic soft clip after up to +18 dB) → crush
   (fewer bits, then each frame held for several) */
void fx_master(int32_t *l, int32_t *r, uint32_t n) {
    if (fx.filter_on) {
        int32_t f = svf_f_q15[MIN(fx.filter_cut, 127)], q = 32767 - (int32_t)fx.filter_res * 290;
        for (int c = 0; c < 2; c++) {
            int32_t *b = c ? r : l, low = f_low[c], band = f_band[c];
            for (uint32_t i = 0; i < n; i++) {
                int32_t s = CLAMP(b[i], -65535, 65535);
                low += (f * band) >> 15;
                int32_t high = CLAMP(s - low - ((q * band) >> 15), -65535, 65535);
                band += (f * high) >> 15;
                band = CLAMP(band, -65535, 65535); low = CLAMP(low, -65535, 65535);
                b[i] = fx.filter_mode == FXF_LOW ? low : fx.filter_mode == FXF_BAND ? band : high;
            }
            f_low[c] = low; f_band[c] = band;
        }
    }
    if (fx.drive_on) {
        int32_t g = 4096 + fx.drive * 287;                          /* Q12: ×1 .. ×8 */
        for (uint32_t i = 0; i < n; i++) {
            int32_t x = sat16((l[i] * g) >> 12), y = sat16((r[i] * g) >> 12);
            l[i] = ((3 * x - ((x * ((x * x) >> 15)) >> 15)) >> 1) * 26000 >> 15;
            r[i] = ((3 * y - ((y * ((y * y) >> 15)) >> 15)) >> 1) * 26000 >> 15;
        }
    }
    if (fx.crush_on) {
        int32_t mask = ~((1 << (16 - CLAMP(fx.crush_bits, 1, 16))) - 1);
        uint32_t every = CLAMP(fx.crush_rate, 1, 32);
        for (uint32_t i = 0; i < n; i++) {
            if (hold_n == 0) { hold_l = sat16(l[i]) & mask; hold_r = sat16(r[i]) & mask; }
            if (++hold_n >= every) hold_n = 0;
            l[i] = hold_l; r[i] = hold_r;
        }
    }
}
