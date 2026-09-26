/* METASTASEIS: Iannis Xenakis's first orchestral work (1954), 46 strings each on its own glissando; he drew them as
   straight lines on graph paper, and the lines made curved surfaces (the Philips Pavilion came from the same
   drawings). Here the pen draws the score as it plays: all 46 start on one G, fan out into a cluster, then cross over
   onto a chord of stacked fifths. Violins I and II, violas, cellos and basses each in their own ink; the name is part
   of the score and appears as the pen passes it. */
#include "splash_int.h"

#define NV 46
#define T_A 400                                       /* the unison starts */
#define T_B 3000                                      /* the widest cluster */
#define T_C 5000                                      /* on the chord */
#define T_END 6600
#define M_LO 40                                       /* E2 .. E6 up the page */
#define M_HI 88
struct str { int32_t m0, mb, mc;                      /* MIDI note x256: the unison, the cluster, the chord */
             int32_t sa, sb;                          /* the two glissandi's slopes, note x256 a ms, Q16 */
             uint32_t ph, vib, vinc; int32_t gl, gr; uint8_t ink; };
static struct str sv[NV];
static uint32_t rate, seed, inc_a4;
static int32_t gain_now;
static struct sp_verb verb;
static int16_t tone[2048];                            /* one cycle: harmonics 1, 1/2, 1/3, 1/4 — a saw without aliasing */
static int gx0, gx1, gy0, gy1, pen_last;              /* the score on the page; how far the pen has drawn */
enum { C_PAPER = GFX_FREE_COLOR, C_MINOR, C_MAJOR, C_INK, M_RED, M_BLUE, M_GREEN, M_BROWN, C_NOTE };

static int32_t pitch_at(const struct str *s, int t) {      /* 32-bit only: it runs for every string, every block */
    if (t <= T_A) return s->m0;
    if (t <= T_B) return s->m0 + ((s->sa * (t - T_A)) >> 16);
    if (t <= T_C) return s->mb + ((s->sb * (t - T_B)) >> 16);
    return s->mc;
}
static int x_at(int t) { return gx0 + (int)((int64_t)(gx1 - gx0) * (t - T_A) / (T_END - T_A)); }
static int y_at(int32_t m) { return gy1 - (int)((int64_t)(gy1 - gy0) * (m - M_LO * 256) / ((M_HI - M_LO) * 256)); }

/* a MIDI note (x256) to a phase step: A4's, by octaves and semitones, the fraction between them linear */
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

static void start(uint32_t r) {
    rate = r; seed = 0x4D455441u; gain_now = 0; pen_last = -1;
    inc_a4 = (uint32_t)((440ull << 32) / r);
    /* the sections, as in the score: 12 + 12 violins, 8 violas, 8 cellos, 6 basses, from the top down */
    static const int chord[7] = { 43, 50, 57, 64, 71, 78, 85 };            /* G2 D3 A3 E4 B4 F#5 C#6 */
    for (int i = 0; i < NV; i++) {
        struct str *s = &sv[i];
        int k = NV - 1 - i;                                                  /* k: 0 lowest .. 45 highest in the cluster */
        s->m0 = 55 * 256 + (int32_t)(sp_rand(&seed) % 13) - 6;             /* one G, not quite in unison */
        s->mb = (int32_t)(M_LO * 256 + 128 + (int64_t)k * ((M_HI - M_LO - 2) * 256) / (NV - 1)) + (int32_t)(sp_rand(&seed) % 97) - 48;
        s->mc = chord[6 - k * 7 / NV] * 256;                                 /* crossed over: the high ones land low */
        s->ph = sp_rand(&seed); s->vib = sp_rand(&seed); s->vinc = (uint32_t)(((uint64_t)(5 * 256 + sp_rand(&seed) % 300) << 24) / r);
        s->sa = (s->mb - s->m0) * 65536 / (T_B - T_A); s->sb = (s->mc - s->mb) * 65536 / (T_C - T_B);
        s->ink = i < 12 ? C_INK : i < 24 ? M_BLUE : i < 32 ? M_GREEN : i < 40 ? M_BROWN : M_RED;
        int pan = i < 12 ? -170 : i < 24 ? -60 : i < 32 ? 30 : i < 40 ? 110 : 170;
        s->gl = 256 - pan; s->gr = 256 + pan;
    }
    for (int i = 0; i < 2048; i++) {
        uint32_t p = (uint32_t)i << 21;
        tone[i] = (int16_t)((sp_sin(p) + (sp_sin(p * 2) >> 1) + sp_sin(p * 3) / 3 + (sp_sin(p * 4) >> 2)) * 15 / 32);
    }
    sp_verb_init(&verb, r, 29000, 15000);
    splash_ramp(C_PAPER, 1, 0xf1ebdc, 0xf1ebdc); splash_ramp(C_MINOR, 1, 0xdde3e6, 0xdde3e6); splash_ramp(C_MAJOR, 1, 0xbccadb, 0xbccadb);
    splash_ramp(C_INK, 1, 0x25221e, 0x25221e); splash_ramp(M_RED, 1, 0xb32d25, 0xb32d25); splash_ramp(M_BLUE, 1, 0x2b4c8c, 0x2b4c8c);
    splash_ramp(M_GREEN, 1, 0x3b6a4c, 0x3b6a4c); splash_ramp(M_BROWN, 1, 0x7a4726, 0x7a4726); splash_ramp(C_NOTE, 1, 0x8a8170, 0x8a8170);
    int fh = sg.f->height;
    gx0 = sg.f->width * 7; gx1 = sg.W - sg.f->width * 3; gy0 = fh * 3 + LOGO_ROWS * sg.ch + fh; gy1 = sg.H - fh * 3;
}

static int32_t dynamics(int t) {                      /* pp on the unison, forte at the widest, the chord, a fade */
    if (t < T_A) return 0;
    if (t < T_A + 300) return (t - T_A) * 9000 / 300;
    if (t < T_B) return 9000 + (t - T_A - 300) * 23767 / (T_B - T_A - 300);
    if (t < T_C) return 32767 - (t - T_B) * 8000 / (T_C - T_B);
    if (t < T_END) return 24767;
    return MAX(0, 24767 - (t - T_END) * 24767 / 1300);
}

static void audio(int32_t *l, int32_t *r, uint32_t n, uint32_t t) {
    int tm = (int)(t * 1000 / rate);
    int32_t g0 = gain_now, g1 = dynamics(tm), dg = (g1 - g0) / (int32_t)n;
    gain_now = g1;
    if (!g0 && !g1) return;
    int32_t dl[64], dr[64];
    memset(dl, 0, sizeof dl); memset(dr, 0, sizeof dr);
    for (int v = 0; v < NV; v++) {
        struct str *s = &sv[v];
        s->vib += s->vinc * n;
        int32_t depth = 10 + g1 / 1200;                                      /* vibrato: wider as it gets louder */
        uint32_t inc = inc_of(pitch_at(s, tm) + ((sp_sin(s->vib) * depth) >> 15));
        uint32_t ph = s->ph;
        int32_t g = g0;
        for (uint32_t i = 0; i < n; i++, g += dg) {
            int32_t y = (tone[ph >> 21] * (g >> 7)) * 3 >> 13;                 /* ~-20 dBFS a string */
            dl[i] += (y * s->gl) >> 9; dr[i] += (y * s->gr) >> 9;
            ph += inc;
        }
        s->ph = ph;
    }
    for (uint32_t i = 0; i < n; i++) { l[i] += dl[i]; r[i] += dr[i]; }
    int32_t wl[64], wr[64];
    memset(wl, 0, sizeof wl); memset(wr, 0, sizeof wr);
    sp_verb_run(&verb, dl, dr, wl, wr, n);
    for (uint32_t i = 0; i < n; i++) { l[i] += wl[i] / 2; r[i] += wr[i] / 2; }
}

static uint8_t paper(int x, int y) {                 /* graph paper: a fine grid, every fifth line stronger */
    int g = MAX(8, sg.W / 80);
    int mx = (x - gx0) % (g * 5), my = (y - gy1) % (g * 5);
    if (mx < 0) mx += g * 5; if (my < 0) my += g * 5;
    if (mx == 0 || my == 0) return C_MAJOR;
    if (mx % g == 0 || my % g == 0) return C_MINOR;
    return C_PAPER;
}

static void draw(int t, bool first) {
    const struct font *f = sg.f;
    if (first) {
        for (int y = 0; y < sg.H; y++) { uint8_t *row = gfx_row(y); for (int x = 0; x < sg.W; x++) row[x] = y >= gy0 - 4 && y <= gy1 + 4 && x >= gx0 && x <= gx1 ? paper(x, y) : C_PAPER; }
        gfx_dirty(0, 0, sg.W, sg.H);
        for (int m = M_LO; m <= M_HI; m += 12) {                              /* the pitches: E2 … E6 */
            char lab[4] = { 'E', (char)('2' + (m - M_LO) / 12), 0 };
            gfx_text(gx0 - f->width * 4, y_at(m * 256) - f->height / 2, lab, f, C_NOTE, -1, 1);
        }
        gfx_text(gx0, f->height, "METASTASEIS", f, C_INK, -1, 1);
        gfx_text(gx0 + f->width * 13, f->height, "46 strings, each its own glissando · straight lines, curved surfaces", f, C_NOTE, -1, 1);
        splash_captions("after Iannis Xenakis, Metastaseis, 1954", C_NOTE);
    }
    /* the pen: every string's line from where it stopped to now */
    int pen = t < T_A ? -1 : MIN(t, T_END);
    if (pen > T_A) {
        int from = MAX(T_A, pen_last), th = sg.W >= 1600 ? 2 : 1;
        for (int i = 0; i < NV; i++) {
            const struct str *s = &sv[i];
            for (int tt = from; tt < pen; tt += 20) {
                int t2 = MIN(pen, tt + 20);
                int xa = x_at(tt), xb = x_at(t2), ya = y_at(pitch_at(s, tt)), yb = y_at(pitch_at(s, t2));
                for (int k = 0; k < th; k++) gfx_line(xa, ya + k, xb, yb + k, s->ink);
            }
        }
        pen_last = pen;
    }
    /* the name, in ink, as the pen passes under it */
    int ly = f->height * 2 + f->height / 2, px = pen < 0 ? gx0 - 1 : x_at(pen);
    for (int r = 0; r < LOGO_ROWS; r++)
        for (int c = 0; c < LOGO_COLS; c++) {
            char ch = splash_logo[r][c];
            int x = sg.lx + c * sg.cw;
            if (ch != ' ' && x + sg.cw <= px + sg.cw * 6) splash_glyph(x, ly + r * sg.ch, ch, x + sg.cw <= px ? C_INK : C_NOTE, sg.scale);
        }
}

const struct splash_piece splash_meta = { "METASTASEIS", "after Iannis Xenakis, Metastaseis, 1954", T_END, 1400, start, draw, audio };
