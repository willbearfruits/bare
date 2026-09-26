/* The FX page's effects; see perf.h. The ring holds the target's last seconds (int16 stereo, a power of two frames),
   written as it plays — except while REPEAT loops a slice of it, when it holds still so the slice keeps. Each effect
   crossfades in and out over a few milliseconds, so pressing and letting go never clicks. */
#include "perf.h"
#include "seq.h"
#include "fx.h"
#include "audio.h"
#include "mix.h"
#include "tables.h"
#include "platform.h"
#include "libc.h"

struct perf_state perf;
const char *const perf_names[PERF_FX] = { "FILTER", "REPEAT", "REVERSE", "TAPE STOP", "GATE", "CRUSH", "DUB", "FREEZE" };
const char *const perf_x_names[PERF_FX] = { "low-pass · off · high-pass", "slice", "length", "stop time", "rate", "bits", "echo time", "tone" };
const char *const perf_y_names[PERF_FX] = { "resonance", "fade", "mix", "wobble", "depth", "rate", "repeats", "level" };
volatile uint8_t perf_echo_throw, perf_rev_throw;
volatile uint16_t perf_throw_mask;

static uint32_t rate;
static int16_t *ring;
static uint8_t *peaks;
static uint32_t ring_len, mask, ring_w;                   /* frames; ring_len - 1; frames written */
static volatile uint32_t fpb = 24000;                     /* frames a beat (the main loop keeps it) */
static bool was[PERF_FX];
static int32_t xf[PERF_FX];                               /* each effect's crossfade, Q15 */
static uint32_t rep_end, rep_len, rep_pos; static int32_t rep_gain;
static uint32_t rev_origin, rev_len, rev_pos;
static uint32_t tape_i, tape_f, tape_speed, tape_decel;       /* speed and its fall: Q24 (1 << 24 = as recorded) */
static uint32_t gate_pos; static int32_t gate_g = 32767;
static int32_t f_lo[2], f_bd[2];
static int32_t crush_l, crush_r; static uint32_t crush_n;
static uint32_t lfo;
static volatile int32_t level;                            /* the output's peak, for the pad's glow */

void perf_init(uint32_t r) {
    rate = r;
    memset(&perf, 0, sizeof perf);
    static const int16_t x0[PERF_FX] = { 300, 400, 300, 400, 600, 600, 400, 500 }, y0[PERF_FX] = { 300, 200, 1000, 150, 900, 400, 600, 700 };
    for (int k = 0; k < PERF_FX; k++) { perf.x[k] = x0[k]; perf.y[k] = y0[k]; }
    perf.sel = PF_FILTER; perf.source = PERF_ALL;
}

void perf_alloc(uint32_t bytes) {
    uint32_t frames = 1u << 18;                           /* 5.5 s at 48 kHz, or less on a small machine */
    while (frames > 4096 && (uint64_t)frames * 4 + frames / 256 > bytes) frames >>= 1;
    ring = plat_alloc(frames * 4); peaks = plat_alloc(frames / 256);
    if (!ring || !peaks) { ring = 0; ring_len = 0; return; }
    ring_len = frames; mask = frames - 1;
}
uint32_t perf_ring_frames(void) { return ring_len; }

/* the pad's X as a musical length: a slice of the beat, from a table per effect */
static uint32_t length_of(int k, int x) {
    static const uint8_t rep[5][2] = { { 1, 2 }, { 1, 4 }, { 1, 8 }, { 1, 16 }, { 1, 32 } };
    static const uint8_t rev[4][2] = { { 1, 4 }, { 1, 2 }, { 1, 1 }, { 2, 1 } };
    static const uint8_t gate[6][2] = { { 1, 4 }, { 1, 8 }, { 1, 12 }, { 1, 16 }, { 1, 24 }, { 1, 32 } };
    const uint8_t *d = k == PF_REPEAT ? rep[CLAMP(x * 5 / 1001, 0, 4)] : k == PF_REVERSE ? rev[CLAMP(x * 4 / 1001, 0, 3)] : gate[CLAMP(x * 6 / 1001, 0, 5)];
    return MAX(64u, fpb * d[0] / d[1]);
}
static void length_name(int k, int x, char *out, int cap) {
    static const char *const rep[5] = { "1/2", "1/4", "1/8", "1/16", "1/32" }, *const rev[4] = { "1/4", "1/2", "1", "2" };
    static const char *const gate[6] = { "1/4", "1/8", "1/12", "1/16", "1/24", "1/32" };
    snfmt(out, cap, "%s", k == PF_REPEAT ? rep[CLAMP(x * 5 / 1001, 0, 4)] : k == PF_REVERSE ? rev[CLAMP(x * 4 / 1001, 0, 3)] : gate[CLAMP(x * 6 / 1001, 0, 5)]);
}
/* the DJ filter: X below 450 a low-pass closing to the left, above 550 a high-pass rising to the right, off between */
static int filter_index(int x, int *mode) {
    if (x < 450) { *mode = FXF_LOW; return 20 + x * 90 / 450; }
    if (x > 550) { *mode = FXF_HIGH; return 10 + (x - 550) * 90 / 450; }
    *mode = -1; return 0;
}
static int32_t filter_wet(int x) {                        /* fading in at the edges of the middle */
    if (x < 430) return 32767; if (x < 450) return (450 - x) * 32767 / 20;
    if (x > 570) return 32767; if (x > 550) return (x - 550) * 32767 / 20;
    return 0;
}

void perf_value(int k, char *out, int cap) {
    int x = perf.x[k], y = perf.y[k]; char a[16];
    switch (k) {
    case PF_FILTER: {
        int mode, i = filter_index(x, &mode);
        uint32_t hz = 30; for (int j = 0; j < i; j += 14) hz *= 2;
        if (mode < 0) snfmt(out, cap, "off (the middle) · res %d %%", y / 10);
        else snfmt(out, cap, "%s ~%u Hz · res %d %%", mode == FXF_LOW ? "low-pass" : "high-pass", (unsigned)hz, y / 10);
        break; }
    case PF_REPEAT: length_name(k, x, a, sizeof a); snfmt(out, cap, "%s beat · fade %d %%", a, y / 10); break;
    case PF_REVERSE: length_name(k, x, a, sizeof a); snfmt(out, cap, "%s beat · mix %d %%", a, 50 + y / 20); break;
    case PF_TAPE: snfmt(out, cap, "stops in %d.%d s · wobble %d %%", (80 + x * 1920 / 1000) / 1000, (80 + x * 1920 / 1000) % 1000 / 100, y / 10); break;
    case PF_GATE: length_name(k, x, a, sizeof a); snfmt(out, cap, "%s beat · depth %d %%", a, y / 10); break;
    case PF_CRUSH: snfmt(out, cap, "%d bits · rate 1/%d", 16 - x * 14 / 1000, 1 + y * 31 / 1000); break;
    case PF_DUB: snfmt(out, cap, "%s beat · repeats %d %%", fx_echo_div_names[CLAMP(x * FX_ECHO_DIVS / 1001, 0, FX_ECHO_DIVS - 1)], 45 + y / 20); break;
    case PF_FREEZE: snfmt(out, cap, "%s · level %d %%", x < 330 ? "dark" : x < 660 ? "warm" : "bright", 40 + y * 60 / 1000); break;
    }
}

static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline int32_t xmix(int32_t dry, int32_t wet, int32_t w) { return w >= 32767 ? wet : w <= 0 ? dry : dry + (int32_t)(((int64_t)(wet - dry) * w) >> 15); }

void perf_block(int32_t *l, int32_t *r, uint32_t n) {
    bool on[PERF_FX];
    for (int k = 0; k < PERF_FX; k++) on[k] = perf.on[k] && (ring || (k != PF_REPEAT && k != PF_REVERSE && k != PF_TAPE));
    /* pressed just now: where each takes its sound from */
    if (on[PF_REPEAT] && !was[PF_REPEAT]) { rep_len = MIN(length_of(PF_REPEAT, perf.x[PF_REPEAT]), ring_len / 2); rep_end = ring_w; rep_pos = 0; rep_gain = 32767; }
    if (on[PF_REVERSE] && !was[PF_REVERSE]) { rev_origin = ring_w; rev_len = MIN(length_of(PF_REVERSE, perf.x[PF_REVERSE]), ring_len / 2); rev_pos = 0; }
    if (on[PF_TAPE] && !was[PF_TAPE]) {
        tape_i = ring_w - 1; tape_f = 0; tape_speed = 1u << 24;
        uint32_t stop = (80 + (uint32_t)perf.x[PF_TAPE] * 1920 / 1000) * rate / 1000;
        tape_decel = MAX(1u, (1u << 24) / MAX(1u, stop));
    }
    if (on[PF_GATE] && !was[PF_GATE]) gate_pos = 0;
    for (int k = 0; k < PERF_FX; k++) was[k] = on[k];
    /* the settings this block */
    int32_t fstep = 32767 / (int32_t)MAX(1u, rate / 200), tstep = 32767 / (int32_t)MAX(1u, rate / 50);   /* 5 ms; 20 ms for the tape */
    int fmode, fi = filter_index(perf.x[PF_FILTER], &fmode);
    int32_t ff = svf_f_q15[CLAMP(fi, 0, 127)], fq = 32767 - perf.y[PF_FILTER] * 29, fwet = filter_wet(perf.x[PF_FILTER]);
    uint32_t gperiod = length_of(PF_GATE, perf.x[PF_GATE]);
    int32_t gdepth = perf.y[PF_GATE] * 32767 / 1000, gslew = 32767 / (int32_t)MAX(1u, rate / 1000);
    int32_t rfade = perf.y[PF_REPEAT] * 23;                                  /* the fade a beat, Q15: none .. to half (ln 2) */
    int32_t revmix = 16384 + perf.y[PF_REVERSE] * 16;                      /* 50 % .. 100 % */
    int32_t wob = perf.y[PF_TAPE] * 3;                                      /* speed swing, Q16: up to ~5 % */
    int32_t cmask = ~((1 << (CLAMP(perf.x[PF_CRUSH] * 14 / 1000, 0, 14))) - 1);
    uint32_t cevery = 1 + (uint32_t)perf.y[PF_CRUSH] * 31 / 1000;
    int32_t pk = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = l[i], y = r[i];
        for (int k = 0; k < PERF_FX; k++) {
            int32_t st = k == PF_TAPE ? tstep : fstep;
            xf[k] = on[k] ? MIN(32767, xf[k] + st) : MAX(0, xf[k] - st);
        }
        /* the ring: what just played (it holds still under a repeat) */
        if (ring && xf[PF_REPEAT] == 0) {
            uint32_t w = ring_w & mask;
            ring[w * 2] = (int16_t)sat16(x); ring[w * 2 + 1] = (int16_t)sat16(y);
            if ((w & 255) == 0) peaks[w >> 8] = 0;
            int32_t a = (x < 0 ? -x : x) >> 7; if (a > 255) a = 255;
            if (a > peaks[w >> 8]) peaks[w >> 8] = (uint8_t)a;
            ring_w++;
        }
        if (xf[PF_TAPE]) {                                                  /* slowing to a stop, read behind the live head */
            uint32_t a = tape_i & mask, b = (tape_i + 1) & mask;
            int32_t tl = ring[a * 2] + (((ring[b * 2] - ring[a * 2]) * (int32_t)(tape_f >> 1)) >> 15);
            int32_t tr = ring[a * 2 + 1] + (((ring[b * 2 + 1] - ring[a * 2 + 1]) * (int32_t)(tape_f >> 1)) >> 15);
            if (on[PF_TAPE]) {
                lfo += (uint32_t)(4294967296ull * 3 / 2 / 48000);            /* the wobble: 1.5 Hz */
                int32_t sp = (int32_t)(tape_speed >> 8) + (tape_speed > (1u << 21) ? (sine_q15_8192[lfo >> 19] * wob) >> 15 : 0);
                tape_f += (uint32_t)MAX(0, sp); tape_i += tape_f >> 16; tape_f &= 0xFFFF;
                tape_speed = tape_speed > tape_decel ? tape_speed - tape_decel : 0;
                if (!tape_speed) { tl = 0; tr = 0; }
            }
            x = xmix(x, tl, xf[PF_TAPE]); y = xmix(y, tr, xf[PF_TAPE]);
        }
        if (xf[PF_REVERSE]) {                                               /* backwards from the press, looping its length */
            uint32_t at = (rev_origin - 1 - rev_pos) & mask;
            int32_t e = (int32_t)MIN(MIN(rev_pos, rev_len - 1 - rev_pos), 64u) * 511;   /* the loop's seam, faded */
            int32_t bl = (ring[at * 2] * e) >> 15, br = (ring[at * 2 + 1] * e) >> 15;
            int32_t w = (int32_t)(((int64_t)revmix * xf[PF_REVERSE]) >> 15);
            x = xmix(x, bl, w); y = xmix(y, br, w);
            if (on[PF_REVERSE] && ++rev_pos >= rev_len) rev_pos = 0;
        }
        if (xf[PF_REPEAT]) {                                                /* the last slice, again and again */
            uint32_t at = (rep_end - rep_len + rep_pos) & mask;
            int32_t e = (int32_t)MIN(MIN(rep_pos, rep_len - 1 - rep_pos), 64u) * 511;
            int32_t g = (int32_t)(((int64_t)e * rep_gain) >> 15);
            x = xmix(x, (ring[at * 2] * g) >> 15, xf[PF_REPEAT]); y = xmix(y, (ring[at * 2 + 1] * g) >> 15, xf[PF_REPEAT]);
            if (on[PF_REPEAT] && ++rep_pos >= rep_len) {
                rep_pos = 0;                                                     /* the fade, a beat's worth spread over its repeats */
                rep_gain = (int32_t)(((int64_t)rep_gain * (32767 - (int32_t)((int64_t)rfade * rep_len / MAX(1u, fpb)))) >> 15);
                rep_len = MIN(length_of(PF_REPEAT, perf.x[PF_REPEAT]), ring_len / 2);   /* the finger moved: a new slice */
            }
        }
        if (xf[PF_GATE]) {                                                  /* open for the first half of each step */
            bool open = gate_pos % gperiod < gperiod / 2;
            int32_t tg = open ? 32767 : 32767 - gdepth;
            gate_g = gate_g < tg ? MIN(tg, gate_g + gslew) : MAX(tg, gate_g - gslew);
            int32_t g = 32767 - (int32_t)(((int64_t)(32767 - gate_g) * xf[PF_GATE]) >> 15);
            x = (int32_t)(((int64_t)x * g) >> 15); y = (int32_t)(((int64_t)y * g) >> 15);
            gate_pos++;
        }
        if (xf[PF_FILTER] && fmode >= 0) {
            int32_t o[2], in[2] = { x, y };
            for (int c = 0; c < 2; c++) {
                int32_t s = CLAMP(in[c], -65535, 65535);
                f_lo[c] += (ff * f_bd[c]) >> 15;
                int32_t hp = CLAMP(s - f_lo[c] - ((fq * f_bd[c]) >> 15), -65535, 65535);
                f_bd[c] += (ff * hp) >> 15;
                f_bd[c] = CLAMP(f_bd[c], -65535, 65535); f_lo[c] = CLAMP(f_lo[c], -65535, 65535);
                o[c] = fmode == FXF_LOW ? f_lo[c] : hp;
            }
            int32_t w = (int32_t)(((int64_t)fwet * xf[PF_FILTER]) >> 15);
            x = xmix(x, o[0], w); y = xmix(y, o[1], w);
        }
        if (xf[PF_CRUSH]) {
            if (crush_n == 0) { crush_l = sat16(x) & cmask; crush_r = sat16(y) & cmask; }
            if (++crush_n >= cevery) crush_n = 0;
            x = xmix(x, crush_l, xf[PF_CRUSH]); y = xmix(y, crush_r, xf[PF_CRUSH]);
        }
        l[i] = x; r[i] = y;
        int32_t a = x < 0 ? -x : x; if (a > pk) pk = a;
    }
    level = MAX(pk, level - (level >> 4));
}

/* the main loop: the beat's length, and DUB / FREEZE holding the echo and reverb (put back once the tail is gone) */
void perf_work(uint64_t now) {
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    fpb = (uint32_t)(((uint64_t)rate * 60 << 16) / bpm_q16);
    perf_throw_mask = perf.source == PERF_INPUT ? (uint16_t)(1u << CH_INPUT) : 0xFFFF;
    static bool dub, frz; static uint64_t dub_off, frz_off;
    static bool s_echo; static uint8_t s_div, s_fb, s_size, s_damp, s_level;
    if (perf.on[PF_DUB]) {
        if (!dub) { dub = true; s_echo = audio_echo(); s_div = fx.echo_div; s_fb = fx.echo_feedback; }
        audio_set_echo(true);
        fx.echo_div = (uint8_t)CLAMP(perf.x[PF_DUB] * FX_ECHO_DIVS / 1001, 0, FX_ECHO_DIVS - 1);
        fx.echo_feedback = (uint8_t)(45 + perf.y[PF_DUB] / 20);
        perf_echo_throw = 100; dub_off = now;
    } else if (dub) {
        perf_echo_throw = 0;
        if (now - dub_off > 5000) { dub = false; audio_set_echo(s_echo); fx.echo_div = s_div; fx.echo_feedback = s_fb; }
    }
    if (perf.on[PF_FREEZE]) {
        if (!frz) { frz = true; s_size = fx.rev_size; s_damp = fx.rev_damp; s_level = fx.rev_level; }
        fx.rev_size = 100; fx.rev_damp = (uint8_t)(90 - perf.x[PF_FREEZE] * 80 / 1000); fx.rev_level = (uint8_t)(40 + perf.y[PF_FREEZE] * 60 / 1000);
        perf_rev_throw = 100; frz_off = now;
    } else if (frz) {
        perf_rev_throw = 0;
        if (now - frz_off > 8000) { frz = false; fx.rev_size = s_size; fx.rev_damp = s_damp; fx.rev_level = s_level; }
    }
}

void perf_view(struct perf_view *v) {
    v->peaks = peaks; v->blocks = ring_len / 256; v->w_block = (ring_w & mask) >> 8; v->frozen = xf[PF_REPEAT] > 0;
    v->rep_len = xf[PF_REPEAT] ? rep_len : 0; v->rep_from = ring_w - (rep_end - rep_len);
    v->rev_len = xf[PF_REVERSE] ? rev_len : 0; v->rev_from = ring_w - (rev_origin - rev_len);
    v->tape_at = xf[PF_TAPE] ? ring_w - tape_i : 0;
    v->gate_open = gate_g; v->level = level;
}
