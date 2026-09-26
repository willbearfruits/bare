/* CMI: for the Fairlight CMI (1979), the sampler that put the orchestra hit on every 80s record. A green terminal: the
   name typed out with a block cursor, then a stab that is a sample — synthesized here into an 8-bit, 24 kHz buffer
   (no recording of anyone's orchestra) and played back at other rates for the other pitches, as the CMI did, with
   the grit of no interpolation — and its waveform drawn as the CMI's 3D mountain, one slice of the sound per line,
   growing as it plays. A choir of the same kind holds the last chord. */
#include "splash_int.h"

#define SR 24000                                      /* the samples' rate: half the output's, held, not interpolated */
#define STAB_N (SR * 9 / 10)
#define CHOIR_N (SR * 3)
#define SLICES 32
#define SLICE_LEN 240                                 /* samples shown in a slice: 10 ms */
#define T_TYPE0 300
#define T_TYPE1 1500
#define T_END 6300
static int8_t stab[STAB_N], choir[CHOIR_N];
static uint32_t rate, seed, step_q16;                 /* a sample's step at the original pitch, Q16 */
struct play { const int8_t *buf; uint32_t len, pos_q16, inc_q16; int32_t gain; bool on; };
static struct play pv[4];
static struct sp_verb verb;
static int shown_slices, tick_char;
/* when things happen: stab hits (ms, semitones from C, gain), then the choir */
static const struct { int t, semi, gain; } hits[] = { { 1700, 0, 32767 }, { 2230, 0, 26000 }, { 2620, -4, 29000 }, { 2990, -2, 31000 }, { 3360, 0, 32767 } };
#define T_CHOIR 3360
enum { C_G0 = GFX_FREE_COLOR };                      /* a 16-step phosphor green ramp */

/* semitones to a playback step (Q16): 2^(s/12) by table */
static uint32_t semis_q16(int s) {
    static const uint32_t up[12] = { 65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715 };
    uint32_t v = 65536;
    while (s < 0) { s += 12; v >>= 1; }
    while (s >= 12) { s -= 12; v <<= 1; }
    return (uint32_t)(((uint64_t)v * up[s]) >> 16);
}

/* ---- the samples, made once at the start ---- */
static int32_t saw(uint32_t ph) { return (int32_t)(ph >> 17) - 16384; }            /* ±16384, naive: the grit is wanted */

static int32_t make_stab(int32_t norm) {                 /* norm 0: only measure the peak */
    /* C minor across four octaves: brass (two detuned saws a note), a string layer an octave up, a timpani thump and a
       noise crack at the attack, all through a low-pass that closes as it decays */
    static const uint16_t notes_q4[] = { 523, 1047, 1568, 2093, 2489, 3136, 4186 };   /* C2 C3 G3 C4 Eb4 G4 C5, Hz x16 */
    uint32_t ph[16] = { 0 }, inc[16];
    int nn = (int)ARRAY_LEN(notes_q4);
    for (int k = 0; k < nn; k++) {
        inc[2 * k] = (uint32_t)(((uint64_t)notes_q4[k] << 28) / SR);                 /* Hz x16 → a 32-bit phase step */
        inc[2 * k + 1] = inc[2 * k] + inc[2 * k] / 160;                               /* ~11 cents sharp */
        ph[2 * k] = sp_rand(&seed); ph[2 * k + 1] = sp_rand(&seed);
    }
    int32_t lp = 0, lp2 = 0, bp_lo = 0, bp_bd = 0, peak = 1;
    uint32_t tph = 0;
    for (int i = 0; i < STAB_N; i++) {
        int ms = i * 1000 / SR;
        int32_t s = 0;
        for (int k = 0; k < 2 * nn; k++) { s += saw(ph[k]) >> 3; ph[k] += inc[k]; }
        /* envelope: 4 ms up, to 35 % by 180 ms, gone by 900 */
        int32_t env = ms < 4 ? ms * 8192 : ms < 180 ? 32767 - (ms - 4) * 21000 / 176 : MAX(0, 11767 - (ms - 180) * 11767 / 720);
        env = MIN(env, 32767);
        int32_t cut = 26000 - MIN(ms, 600) * 36;                                       /* the filter closes as it decays */
        lp += ((s - lp) * (cut >> 3)) >> 12; lp2 += ((lp - lp2) * (cut >> 3)) >> 12;
        int32_t x = (lp2 * (env >> 3)) >> 12;
        /* the timpani: a sine falling from 90 to 55 Hz over 150 ms */
        int hz = ms < 150 ? 90 - ms * 35 / 150 : 55;
        tph += (uint32_t)hz * 178957u;                                                 /* 2^32 / 24000 a Hz */
        int32_t te = ms < 400 ? 32767 - ms * 32767 / 400 : 0;
        x += (sp_sin(tph) * (te >> 4)) >> 12;
        /* the crack: noise through a band-pass at ~1.5 kHz, 60 ms */
        if (ms < 60) {
            int32_t nz = (int32_t)(sp_rand(&seed) >> 18) - 8192;
            bp_lo += (12000 * bp_bd) >> 15;
            int32_t hp = nz - bp_lo - ((14000 * bp_bd) >> 15);
            bp_bd += (12000 * hp) >> 15;
            x += (bp_bd * (60 - ms)) / 60;
        }
        peak = MAX(peak, x < 0 ? -x : x);
        if (norm) stab[i] = (int8_t)CLAMP((int32_t)((int64_t)x * 120 / norm), -127, 127);   /* 8 bits */
    }
    return peak;
}

static int32_t make_choir(int32_t norm) {
    /* "aah": a C minor 9 of saws with vibrato through three formant band-passes (700, 1150, 2600 Hz) */
    static const uint16_t notes_q4[] = { 2093, 2489, 3136, 3729, 4699 };                /* C4 Eb4 G4 Bb4 D5 */
    int nn = (int)ARRAY_LEN(notes_q4);
    uint32_t ph[10], inc[10];
    for (int k = 0; k < nn; k++) {
        inc[2 * k] = (uint32_t)(((uint64_t)notes_q4[k] << 28) / SR); inc[2 * k + 1] = inc[2 * k] - inc[2 * k] / 230;
        ph[2 * k] = sp_rand(&seed); ph[2 * k + 1] = sp_rand(&seed);
    }
    static const int32_t fq[3] = { 5800, 9300, 18500 }, qd[3] = { 5000, 6000, 8000 }, fg[3] = { 3, 2, 1 };   /* f = 2 sin(pi fc / SR), Q15 */
    int32_t lo[3] = { 0 }, bd[3] = { 0 }, peak = 1;
    uint32_t vib = 0;
    for (int i = 0; i < CHOIR_N; i++) {
        int ms = i * 1000 / SR;
        vib += (uint32_t)(5 * 4294967296ull / SR);                                     /* 5 Hz */
        int32_t v = sp_sin(vib) >> 9;                                                  /* ±64: ~0.4 % */
        int32_t s = 0;
        for (int k = 0; k < 2 * nn; k++) { s += saw(ph[k]) >> 3; ph[k] += inc[k] + (int32_t)((int64_t)inc[k] * v >> 14); }
        int32_t y = 0;
        for (int f = 0; f < 3; f++) {
            lo[f] += (fq[f] * bd[f]) >> 15;
            int32_t hp = s - lo[f] - ((qd[f] * bd[f]) >> 15);
            bd[f] += (fq[f] * hp) >> 15;
            lo[f] = CLAMP(lo[f], -200000, 200000); bd[f] = CLAMP(bd[f], -200000, 200000);
            y += bd[f] * fg[f];
        }
        int32_t env = ms < 600 ? ms * 32767 / 600 : ms < 2300 ? 32767 : MAX(0, 32767 - (ms - 2300) * 32767 / 700);
        int32_t o = (int32_t)(((int64_t)y * env) >> 15);
        peak = MAX(peak, o < 0 ? -o : o);
        if (norm) choir[i] = (int8_t)CLAMP((int32_t)((int64_t)o * 110 / norm), -127, 127);
    }
    return peak;
}

static void start(uint32_t r) {
    rate = r; shown_slices = 0; tick_char = -1;
    seed = 0x464C43u; int32_t ps = make_stab(0); seed = 0x464C43u; make_stab(ps);    /* twice: the peak, then 8 bits */
    seed = 0x43484Fu; int32_t pc = make_choir(0); seed = 0x43484Fu; make_choir(pc);
    step_q16 = (uint32_t)(((uint64_t)SR << 16) / r);
    memset(pv, 0, sizeof pv);
    sp_verb_init(&verb, r, 22000, 14000);
    splash_ramp(C_G0, 9, 0x000a02, 0x2a9a46); splash_ramp(C_G0 + 8, 8, 0x2a9a46, 0xe4ffec);
}

static void trigger(int slot, const int8_t *buf, uint32_t len, int semi, int32_t gain) {
    struct play *p = &pv[slot];
    p->on = false;
    p->buf = buf; p->len = len; p->pos_q16 = 0; p->inc_q16 = (uint32_t)(((uint64_t)step_q16 * semis_q16(semi)) >> 16); p->gain = gain;
    p->on = true;
}

static void audio(int32_t *l, int32_t *r, uint32_t n, uint32_t t) {
    uint32_t t1 = t + n;
    for (unsigned h = 0; h < ARRAY_LEN(hits); h++) {
        uint32_t at = (uint32_t)hits[h].t * rate / 1000;
        if (at >= t && at < t1) trigger((int)(h % 2), stab, STAB_N, hits[h].semi, hits[h].gain);
    }
    uint32_t ca = T_CHOIR * rate / 1000;
    if (ca >= t && ca < t1) trigger(2, choir, CHOIR_N, 0, 30000);
    /* the typing: a soft tick a character */
    int32_t d[64];
    memset(d, 0, sizeof d);
    for (int k = 0; k < 4; k++) {
        struct play *p = &pv[k];
        if (!p->on) continue;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t at = p->pos_q16 >> 16;
            if (at >= p->len) { p->on = false; break; }
            d[i] += (p->buf[at] * (p->gain >> 4)) * 3 >> 5;                         /* held: no interpolation */
            p->pos_q16 += p->inc_q16;
        }
    }
    int tm = (int)(t * 1000 / rate);
    if (tm >= T_TYPE0 && tm < T_TYPE1) {
        int ch = (tm - T_TYPE0) * 150 / (T_TYPE1 - T_TYPE0);
        if (ch != tick_char) {
            tick_char = ch;
            for (uint32_t i = 0; i < MIN(n, 24u); i++) d[i] += (int32_t)((sp_rand(&seed) >> 20) - 2048) * (24 - (int)i) / 8;
        }
    }
    int32_t wl[64], wr[64];
    memset(wl, 0, sizeof wl); memset(wr, 0, sizeof wr);
    sp_verb_run(&verb, d, d, wl, wr, n);
    for (uint32_t i = 0; i < n; i++) { l[i] += d[i] + wl[i] / 3; r[i] += d[i] + wr[i] / 3; }
}

/* a slice of the stab as a line in the mountain: slice k is k/SLICES of the way in, drawn back to front so the near
   ones hide the far ones (everything under a line is blacked out first) */
static void slice(int k, int x0, int base, int w, int amp, uint8_t line) {
    int from = k * (STAB_N * 2 / 3) / SLICES, py = 0;
    for (int x = 0; x < w; x++) {
        int s = stab[from + x * SLICE_LEN / w];
        int y = base - s * 2 * amp / ((s < 0 ? -s : s) + 128);             /* a gentle compression: the tail shows too */
        gfx_vline(x0 + x, y + 1, MAX(0, base + amp - y), C_G0);
        if (x) gfx_line(x0 + x - 1, py, x0 + x, y, line); else gfx_pixel(x0, y, line);
        py = y;
    }
}

static void draw(int t, bool first) {
    const struct font *f = sg.f;
    if (first) gfx_fill(0, 0, sg.W, sg.H, C_G0);
    /* the page header, in inverse video */
    int hy = f->height / 2;
    gfx_fill(0, hy, sg.W, f->height, C_G0 + 12);
    gfx_text(f->width * 2, hy, "PAGE D", f, C_G0, -1, 1);
    gfx_text(f->width * 12, hy, "WAVEFORM", f, C_G0, -1, 1);
    gfx_text(f->width * 24, hy, "VOICE 1  STAB  24 KHZ  8 BIT", f, C_G0, -1, 1);
    gfx_text(sg.W - f->width * 8, hy, "BARE!", f, C_G0, -1, 1);
    /* the name, typed */
    int ly = f->height * 3, chars = t < T_TYPE0 ? 0 : MIN(1000, (t - T_TYPE0) * 150 / (T_TYPE1 - T_TYPE0));
    gfx_fill(0, ly, sg.W, LOGO_ROWS * sg.ch, C_G0);                  /* the cursor moved: the lines typed so far, again */
    int n = 0, cx = sg.lx, cy = ly;
    for (int r = 0; r < LOGO_ROWS; r++)
        for (int c = 0; c < LOGO_COLS; c++) {
            char ch = splash_logo[r][c];
            if (ch == ' ') continue;
            if (n++ >= chars) goto typed;
            splash_glyph(sg.lx + c * sg.cw, ly + r * sg.ch, ch, (uint8_t)(C_G0 + 14), sg.scale);
            cx = sg.lx + (c + 1) * sg.cw; cy = ly + r * sg.ch;
        }
typed:
    if (t < T_END - 300) {                                            /* the cursor: a block, blinking once typed */
        bool on = t < T_TYPE1 || (t / 400) % 2 == 0;
        gfx_fill(cx, cy, sg.cw, sg.ch, on ? (uint8_t)(C_G0 + 12) : C_G0);
    }
    /* the mountain: a slice appears as the first hit plays through it */
    int amp = sg.H * 9 / 100, top = ly + LOGO_ROWS * sg.ch + f->height * 2;
    int mx0 = sg.W * 14 / 100, mw = sg.W * 50 / 100, base0 = sg.H - f->height * 3 - amp, dx = sg.W * 9 / 1000;
    int dy = MIN(sg.H * 11 / 1000, (base0 - amp - top - f->height) / SLICES);
    int want = t < hits[0].t ? 0 : MIN(SLICES, (t - hits[0].t) * SLICES / 700 + 1);
    if (want != shown_slices) {
        gfx_fill(0, top, sg.W, sg.H - top - f->height * 2, C_G0);
        for (int k = want - 1; k >= 0; k--) slice(k, mx0 + k * dx, base0 - k * dy, mw, amp, (uint8_t)(C_G0 + 15 - k * 7 / SLICES));
        shown_slices = want;
        char lab[64];
        snfmt(lab, sizeof lab, "%d SEGMENTS  0 - %d MS", want, want * (STAB_N * 2 / 3) / SLICES * 1000 / SR);
        gfx_text(mx0, top, lab, f, C_G0 + 9, -1, 1);
    }
    if (first) splash_captions("for the Fairlight CMI, 1979", C_G0 + 8);
}

const struct splash_piece splash_cmi = { "CMI", "for the Fairlight CMI, 1979", T_END, 1800, start, draw, audio };
