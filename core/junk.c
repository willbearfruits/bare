/* MERZBOW's noise (see junk.h). SCRAPE, FEEDBACK and BYTES are rendered here; METAL is synth voices (JUNK METAL, an
   FM patch at a struck plate's ratios) on the same LINEAGE bus, so drive, crush, chop and the cap take them too. */
#include "junk.h"
#include "harmony.h"
#include "omni.h"
#include "synth.h"
#include "tables.h"
#include "libc.h"

struct junk_state junk;
static uint32_t rate = 48000, seed = 0x6A756E6Bu;
#define TAG_METAL 0xC60                    /* | object */

/* SCRAPE: each finger a noise through a Chamberlin band-pass, its place the band, its speed the grit */
static struct { volatile bool on; volatile int32_t f, amp, grit; int32_t low, band, gain; int32_t lx; } sc[JUNK_SCRAPERS];
/* FEEDBACK: a delay of 4096 frames with a band in it; BYTES: where the reading is */
static int16_t loop_buf[4096]; static uint32_t loop_w; static int32_t fb_low, fb_band, fb_level, fb_gain;
static uint32_t byte_pos_q16;
static int32_t hold, rms_q8 = 0, chop_ph;                  /* CRUSH's held sample, the cap's measure, CHOP's phase */
static int32_t hold_left, cap_g = 32767;                  /* the cap's gain, ramped over a block */
static volatile uint32_t metal_left;                      /* frames the struck junk may still ring: the chain stays on */

void junk_defaults(void) {
    junk_all_off();
    junk.drive = 40; junk.bits = 8; junk.chop = 0; junk.feedback = 60; junk.grain = 50; junk.bytes_rate = 30; junk.level = 60;
}
void junk_init(uint32_t r) {
    rate = r ? r : 48000;
    memset(&junk, 0, sizeof junk); memset(sc, 0, sizeof sc); memset(loop_buf, 0, sizeof loop_buf);
    junk_defaults();
}

static uint32_t isqrt(uint32_t v) { uint32_t r = 0, b = 1u << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
/* the picture's measure of a source: up at once, down with a time constant of 2^shift frames */
static void follow(int s, int32_t a, uint32_t n, int shift) {
    int32_t v = junk.activity[s];
    v = a > v ? a : v - MAX(1, (int32_t)((v * (int32_t)n) >> shift));
    junk.activity[s] = (uint16_t)CLAMP(v, 0, 32767);
}
static inline int32_t noise(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (int32_t)(seed >> 16) - 32768; }

/* the junk's fundamentals: a spread of hashed pitches over three octaves, or, with keys following the chord, its tones */
void junk_strike(int k, int vel) {
    if (k < 0 || k >= JUNK_OBJECTS) return;
    int note = 36 + (int)((uint32_t)(k * 2654435761u) >> 27) + k;   /* 36 .. ~100, not in any scale */
    if (harmony_on) { uint8_t pcs[3]; omni_chord_tones(omni_chord_word, pcs); note = 36 + 12 * (k / 3 % 5) + pcs[k % 3]; }
    synth_note_on((uint8_t)CLAMP(note, 24, 108), (uint8_t)CLAMP(vel, 1, 127), P_METAL, (uint16_t)(TAG_METAL | k));
    junk.activity[JS_METAL] = 32767;
    metal_left = rate * 3;
}

void junk_scrape(int who, int x, int y, int z, bool on) {
    if (who < 0 || who >= JUNK_SCRAPERS) return;
    if (!on) { sc[who].on = false; return; }
    int ci = 40 + x * 80 / 32768;                                         /* the band: low left, high right */
    int32_t speed = x > sc[who].lx ? x - sc[who].lx : sc[who].lx - x;
    sc[who].lx = x;
    sc[who].f = svf_f_q15[CLAMP(ci, 0, 127)];
    sc[who].amp = CLAMP((z > 0 && z != 60 ? z - 20 : 60) * 300, 3000, 30000) * (100 - y * 70 / 32768) / 100;   /* lower down, quieter */
    sc[who].grit = CLAMP(speed * 8, 0, 32767);
    sc[who].on = true;
}

void junk_all_off(void) {
    for (int i = 0; i < JUNK_SCRAPERS; i++) sc[i].on = false;
    for (int k = 0; k < JUNK_OBJECTS; k++) synth_note_off_tag((uint16_t)(TAG_METAL | k));
    junk.feedback_on = junk.bytes_on = false;
}

static inline int32_t soft(int32_t x) {                                   /* x - x³/3, after a clip to ±32767: 32-bit only */
    if (x > 32767) x = 32767; else if (x < -32767) x = -32767;
    int32_t x2 = (x * x) >> 15;
    return x - ((x2 * x) >> 15) / 3;
}

bool junk_render(int32_t *l, int32_t *r, uint32_t n, bool add) {
    bool any_sc = false;
    for (int i = 0; i < JUNK_SCRAPERS; i++) any_sc |= sc[i].on || sc[i].gain;
    metal_left = metal_left > n ? metal_left - n : 0;
    bool active = any_sc || junk.feedback_on || fb_gain || fb_level > 8 || junk.bytes_on || metal_left;   /* its own sources: else the bus (CARLOS) passes */
    if (!active) { for (int s = 0; s < JUNK_SOURCES; s++) follow(s, 0, n, s == JS_METAL ? 14 : 12); return add; }
    if (!add) { memset(l, 0, n * sizeof *l); memset(r, 0, n * sizeof *r); }
    int32_t act[JUNK_SOURCES] = { 0 };
    /* SCRAPE */
    int32_t q = 32767 - junk.grain * 290;                                   /* the band's resonance */
    for (int i = 0; i < JUNK_SCRAPERS; i++) {
        int32_t target = sc[i].on ? sc[i].amp : 0, g = sc[i].gain, dg = (target - g) / (int32_t)n;
        if (!g && !target) continue;
        int32_t f = sc[i].f, low = sc[i].low, band = sc[i].band, grit = sc[i].grit;
        for (uint32_t k = 0; k < n; k++, g += dg) {
            int32_t x = noise();
            if (grit && (noise() & 32767) < grit) x = x > 0 ? 32767 : -32767;   /* the rougher the faster */
            low += (f * band) >> 15;
            int32_t high = x - low - ((q * band) >> 15);
            high = CLAMP(high, -65535, 65535); band += (f * high) >> 15; band = CLAMP(band, -65535, 65535); low = CLAMP(low, -65535, 65535);
            int32_t y = (band * g) >> 15;
            int32_t pan = sc[i].lx >> 7;                                     /* 0 .. 255 across */
            l[k] += (y * (256 - pan)) >> 8; r[k] += (y * pan) >> 8;
            act[JS_SCRAPE] = MAX(act[JS_SCRAPE], y < 0 ? -y : y);
        }
        sc[i].low = low; sc[i].band = band; sc[i].gain = target;
        if (!sc[i].on) sc[i].grit = 0;
    }
    /* BYTES: this program's own code, as 8-bit sound */
    if (junk.bytes_on) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)&junk_render;
        uint32_t step = (uint32_t)((2000u + junk.bytes_rate * 460u) << 16) / rate;   /* 2 .. 48 kHz */
        for (uint32_t k = 0; k < n; k++) {
            int32_t y = ((int32_t)code[(byte_pos_q16 >> 16) & 4095] - 128) << 7;   /* 4 KB of it, from this function on */
            l[k] += y; r[k] += y; byte_pos_q16 += step;
            act[JS_BYTES] = MAX(act[JS_BYTES], y < 0 ? -y : y);
        }
    }
    /* FEEDBACK: what sounds, into a delay, through a band, back in louder; the chord's root as the band, keys following it */
    int ci = 64;
    if (harmony_on) { uint8_t pcs[3]; omni_chord_tones(omni_chord_word, pcs); ci = 52 + pcs[0] * 3; }
    int32_t ff = svf_f_q15[CLAMP(ci, 0, 127)], fq = 32767 - 60 * 290;
    int32_t want = junk.feedback_on ? 20000 + junk.feedback * 180 : 0;    /* up to ~1.2; let go, it dies away in ~15 ms */
    fb_gain = want > fb_gain ? MIN(want, fb_gain + 1200) : MAX(want, fb_gain - 1200);
    int32_t gain = fb_gain;
    fb_level = 0;
    for (uint32_t k = 0; k < n; k++) {
        int32_t in = ((l[k] + r[k]) >> 1) + (junk.feedback_on ? noise() >> 9 : 0);
        int32_t d = loop_buf[loop_w & 4095];
        fb_low += (ff * fb_band) >> 15;
        int32_t high = d - fb_low - ((fq * fb_band) >> 15);
        high = CLAMP(high, -65535, 65535); fb_band += (ff * high) >> 15; fb_band = CLAMP(fb_band, -65535, 65535); fb_low = CLAMP(fb_low, -65535, 65535);
        int32_t y = soft((((fb_band >> 2) * gain) >> 12) + in / 4);     /* pre-shifted: 65535 × 38000 won't fit */
        loop_buf[loop_w++ & 4095] = (int16_t)y;
        int32_t o = (y * gain) >> 15;
        l[k] += o; r[k] += o;
        int32_t a = o < 0 ? -o : o; if (a > fb_level) fb_level = a;
    }
    act[JS_FEEDBACK] = fb_level;
    /* then DRIVE, CRUSH (fewer bits, a held sample), CHOP (a gate at a rate), and the cap */
    int32_t drive = 16 + junk.drive * junk.drive / 4;                       /* x1 .. x40, Q4 */
    uint32_t mask = junk.bits >= 16 ? 0xFFFFFFFFu : ~((1u << (16 - CLAMP(junk.bits, 1, 16))) - 1);
    int32_t hold_n = 1 + (16 - CLAMP(junk.bits, 1, 16)) / 3, chop_inc = junk.chop ? (1 + junk.chop * 30 / 100) * 131072 / (int32_t)rate : 0;   /* 1 .. 31 Hz */
    int64_t sq = 0; int32_t peak = 0;
    for (uint32_t k = 0; k < n; k++) {
        int32_t x = CLAMP((l[k] + r[k]) >> 1, -131072, 131072), side = CLAMP((l[k] - r[k]) >> 1, -32767, 32767);
        x = soft((x * drive) >> 4);
        if (--hold_left <= 0) { hold = (int32_t)((uint32_t)x & mask); hold_left = hold_n; }
        x = hold;
        if (chop_inc) { chop_ph += chop_inc; if ((chop_ph >> 16) & 1) x = 0; }
        int32_t y = (x * junk.level) / 100;
        l[k] = y + side / 2; r[k] = y - side / 2;
        sq += (int64_t)y * y; int32_t a = y < 0 ? -y : y; if (a > peak) peak = a;
    }
    /* the cap: about -12 dBFS of power at most, however it's driven; the gain the square root of the power's ratio */
    int32_t ms = (int32_t)(sq >> 8) / (int32_t)n, cap = 8000 * 8000 >> 8;
    rms_q8 = rms_q8 + ((ms - rms_q8) >> 3);
    int32_t g = 32767;
    if (rms_q8 > cap) g = MIN((int32_t)isqrt((uint32_t)((cap << 7) / (rms_q8 >> 8)) << 15), 32767);   /* the ratio in Q15, its root in Q15 */
    if (g < 32767 || cap_g < 32767) {
        int32_t g0 = cap_g, dg = (g - g0) / (int32_t)n;
        for (uint32_t k = 0; k < n; k++, g0 += dg) { l[k] = (l[k] * g0) >> 15; r[k] = (r[k] * g0) >> 15; }
        peak = (peak * MAX(g, cap_g)) >> 15;
    }
    cap_g = g;
    if (peak > junk.peak) junk.peak = peak;                                   /* held until the picture takes it */
    for (int s = 0; s < JUNK_SOURCES; s++) follow(s, CLAMP(act[s], 0, 32767), n, s == JS_METAL ? 14 : 12);
    return true;
}
