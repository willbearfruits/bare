/* ANS: Evgeny Murzin's photoelectronic synthesizer (finished 1957). The composer scratched a black coating off a glass
   plate; light through the scratches, read by a moving slit, played pure tones — 72 to the octave — at the heights
   of the scratches. Here the name is scratched into the plate (each ASCII character is a gesture: / and \ glissandi,
   _ a held tone, | a cluster, ( ) curves), with a drone of C and G along the bottom and a chord at the end; then the
   slit crosses it and plays what it lights. 360 tones over five octaves from C2, each a sine that fades in and out
   as the scratches under the slit come and go. */
#include "splash_int.h"

#define ROWS 360                                      /* five octaves of 72 */
#define MAXS 200
#define T_SCAN0 1500                                  /* ms: engraving before, the slit crosses after */
#define T_SCAN1 6300
#define T_END   6900
enum { K_LINE, K_BAR };
/* a scratch, in screen pixels; t0: when it is engraved (ms) */
struct stroke { int16_t x0, y0, x1, y1; uint8_t kind, bright; uint16_t t0, dur; };
static struct stroke st[MAXS];
static int ns;
static int px0, px1, py0, py1, pw, ph;               /* the plate */
static uint32_t rate, scan_step_q16, f_scan0, f_scan1;
static uint32_t row_inc[ROWS], row_ph[ROWS];
static int32_t row_amp[ROWS], row_tgt[ROWS];
static uint32_t seed;
static struct sp_verb verb;
static int drawn_to;                                  /* the columns re-rendered last frame reach this x */

/* colours (the splash's own palette entries) */
enum { C_SURROUND = GFX_FREE_COLOR, C_PLATE, C_GRID, C_RULER, C_EDGE, C_SCRATCH0 = GFX_FREE_COLOR + 6,
       C_GLOW0 = C_SCRATCH0 + 32, C_HALO0 = C_GLOW0 + 16, C_SLIT = C_HALO0 + 16 };

static uint32_t hash2(uint32_t a, uint32_t b) { uint32_t h = a * 2654435761u ^ b * 2246822519u; h ^= h >> 15; h *= 2654435761u; return h ^ (h >> 13); }

static void add(int x0, int y0, int x1, int y1, int kind, int bright, int t0, int dur) {
    if (ns >= MAXS) return;
    st[ns++] = (struct stroke){ (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (uint8_t)kind, (uint8_t)bright, (uint16_t)t0, (uint16_t)dur };
}
static int row_y(int row) { return py1 - 1 - (row * 2 + 1) * (ph - 2) / (ROWS * 2); }   /* a tone's height on the plate */

/* the scratches: the logo's characters as gestures in their cells, engraved left to right */
static void build(void) {
    ns = 0;
    int lw = pw * 84 / 100, cwp = lw / LOGO_COLS, chp = MIN(cwp * 22 / 10, ph * 60 / 100 / LOGO_ROWS);
    int lx = px0 + (pw - cwp * LOGO_COLS) / 2, ly = py0 + (ph - chp * LOGO_ROWS) / 2 - chp / 4;
    for (int c = 0; c < LOGO_COLS; c++)
        for (int r = 0; r < LOGO_ROWS; r++) {
            char ch = splash_logo[r][c];
            int x0 = lx + c * cwp, y0 = ly + r * chp, x1 = x0 + cwp, y1 = y0 + chp, xm = (x0 + x1) / 2;
            int t = 60 + c * 1250 / LOGO_COLS + r * 18, d = 70;
            switch (ch) {
            case '_':  add(x0, y1 - chp / 8, x1, y1 - chp / 8, K_LINE, 235, t, d); break;
            case '/':  add(x0, y1, x1, y0, K_LINE, 255, t, d); break;
            case '\\': add(x0, y0, x1, y1, K_LINE, 255, t, d); break;
            case '|':  add(xm, y0, xm, y1, K_BAR, 200, t, d); break;
            case '(':  add(x1 - cwp / 5, y0 + chp / 8, x0 + cwp / 3, y0 + chp * 4 / 10, K_LINE, 230, t, d / 2);
                       add(x0 + cwp / 3, y0 + chp * 4 / 10, x0 + cwp / 3, y1 - chp * 4 / 10, K_BAR, 190, t + d / 2, d / 2);
                       add(x0 + cwp / 3, y1 - chp * 4 / 10, x1 - cwp / 5, y1 - chp / 8, K_LINE, 230, t + d, d / 2); break;
            case ')':  add(x0 + cwp / 5, y0 + chp / 8, x1 - cwp / 3, y0 + chp * 4 / 10, K_LINE, 230, t, d / 2);
                       add(x1 - cwp / 3, y0 + chp * 4 / 10, x1 - cwp / 3, y1 - chp * 4 / 10, K_BAR, 190, t + d / 2, d / 2);
                       add(x1 - cwp / 3, y1 - chp * 4 / 10, x0 + cwp / 5, y1 - chp / 8, K_LINE, 230, t + d, d / 2); break;
            case ',':  add(xm, y1 - chp / 4, x0 + cwp / 5, y1 + chp / 8, K_LINE, 220, t, d); break;
            default: break;
            }
        }
    /* the drone, C2 and G2 the length of the plate; C3 fainter; a chord at the end, after the name */
    add(px0 + pw / 60, row_y(0), px1 - pw / 60, row_y(0), K_LINE, 170, 1180, 300);
    add(px0 + pw / 40, row_y(42), px1 - pw / 30, row_y(42), K_LINE, 120, 1230, 280);
    add(px0 + pw / 25, row_y(72), px0 + pw * 3 / 10, row_y(72), K_LINE, 90, 1270, 180);
    static const int chord[] = { 144, 168, 186, 210, 228 };                         /* C4 E4 G4 B4 D5 */
    for (int i = 0; i < 5; i++) add(lx + LOGO_COLS * cwp + cwp / 2, row_y(chord[i]), px1 - pw / 60, row_y(chord[i] - i % 2), K_LINE, 170 - i * 12, 1300 + i * 25, 150);
}

/* what one scratch leaves in column x: rows ylo..yhi, and how bright (0..31), with the jitter of a hand */
static bool stroke_col(const struct stroke *s, int i, int x, int *ylo, int *yhi, int *lvl) {
    int th = MAX(1, sg.W / 700);                      /* half the scratch's width */
    if (s->kind == K_BAR) {
        if (x < s->x0 - th || x > s->x0 + th) return false;
        *ylo = s->y0; *yhi = s->y1;
    } else {
        int xa = MIN(s->x0, s->x1), xb = MAX(s->x0, s->x1);
        if (x < xa - th || x > xb + th) return false;
        int dx = s->x1 - s->x0;
        int xc = CLAMP(x, xa, xb);
        int ya = dx ? s->y0 + (s->y1 - s->y0) * (xc - s->x0) / dx : s->y0;
        int yb = dx ? s->y0 + (s->y1 - s->y0) * (CLAMP(xc + 1, xa, xb) - s->x0) / dx : s->y1;
        int j = (int)(hash2((uint32_t)i, (uint32_t)x / 3) % 3) - 1;
        *ylo = MIN(ya, yb) - th + j; *yhi = MAX(ya, yb) + th + j;
    }
    *lvl = CLAMP(s->bright * 31 / 255 - (int)(hash2((uint32_t)x, (uint32_t)i) & 3), 6, 31);
    return true;
}

/* a column of the plate: background, scratches, lit by the slit by `glow` (0..15) */
static void column(int x, int glow, int halo) {
    if (x < px0 || x >= px1) return;
    uint8_t bg = halo ? (uint8_t)(C_HALO0 + halo) : C_PLATE;
    for (int y = py0; y < py1; y++) gfx_row(y)[x] = bg;
    for (int o = 0; o < 5; o++) { int y = row_y(o * 72); if ((x & 3) == 0 && y > py0 && y < py1) gfx_row(y)[x] = C_GRID; }
    for (int i = 0; i < ns; i++) {
        int ylo, yhi, lvl;
        if (!stroke_col(&st[i], i, x, &ylo, &yhi, &lvl)) continue;
        uint8_t c = glow ? (uint8_t)(C_GLOW0 + MIN(15, glow * lvl / 31 + 2)) : (uint8_t)(C_SCRATCH0 + lvl);
        for (int y = MAX(py0, ylo); y <= MIN(py1 - 1, yhi); y++) gfx_row(y)[x] = c;
    }
    gfx_dirty(x, py0, 1, ph);
}

static void start(uint32_t r) {
    rate = r;
    seed = 0x4E53u;
    int fw = sg.f->width, fh = sg.f->height;
    px0 = fw * 7; px1 = sg.W - fw * 3; py0 = fh * 2; py1 = sg.H - fh * 3;
    pw = px1 - px0; ph = py1 - py0;
    build();
    splash_ramp(C_SURROUND, 1, 0x0c0d10, 0x0c0d10); splash_ramp(C_PLATE, 1, 0x030405, 0x030405);
    splash_ramp(C_GRID, 1, 0x1a1e24, 0x1a1e24); splash_ramp(C_RULER, 1, 0x56606c, 0x56606c); splash_ramp(C_EDGE, 1, 0x2a3038, 0x2a3038);
    splash_ramp(C_SCRATCH0, 32, 0x14171b, 0xe6ebf0);
    splash_ramp(C_GLOW0, 12, 0x3a2a14, 0xffb454); splash_ramp(C_GLOW0 + 12, 4, 0xffc878, 0xfff8e8);
    splash_ramp(C_HALO0, 16, 0x030405, 0x2e1c08);
    splash_ramp(C_SLIT, 1, 0xfff4dc, 0xfff4dc);
    /* 72 tones an octave from C2: the first octave by 2^(1/72) steps, the rest by doubling */
    row_inc[0] = sp_inc(16744, r);                    /* 65.406 Hz in Q8 */
    for (int i = 1; i < 72; i++) row_inc[i] = (uint32_t)(((uint64_t)row_inc[i - 1] * 1084128701u) >> 30);
    for (int i = 72; i < ROWS; i++) row_inc[i] = row_inc[i - 72] * 2;
    for (int i = 0; i < ROWS; i++) { row_ph[i] = sp_rand(&seed); row_amp[i] = row_tgt[i] = 0; }
    f_scan0 = T_SCAN0 * r / 1000; f_scan1 = T_SCAN1 * r / 1000;
    scan_step_q16 = (uint32_t)(((uint64_t)pw << 24) / (f_scan1 - f_scan0));   /* pixels (Q8) a frame, Q16 */
    sp_verb_init(&verb, r, 27500, 16000);
    drawn_to = px0;
}

static int scan_x_q8(uint32_t f) {                    /* the slit's x (Q8) at audio frame f */
    if (f <= f_scan0) return px0 << 8;
    return (px0 << 8) + (int)(((uint64_t)(f - f_scan0) * scan_step_q16) >> 16);
}

static void draw(int t, bool first) {
    const struct font *f = sg.f;
    if (first) {
        gfx_fill(0, 0, sg.W, sg.H, C_SURROUND);
        gfx_fill(px0, py0, pw, ph, C_PLATE);
        gfx_frame(px0 - 1, py0 - 1, pw + 2, ph + 2, C_EDGE);
        for (int o = 0; o <= 5; o++) {                                 /* the scale: octaves and semitones */
            int y = row_y(MIN(o * 72, ROWS - 1));
            char lab[4] = { 'C', (char)('2' + o), 0 };
            gfx_text(px0 - f->width * 5, y - f->height / 2, lab, f, C_RULER, -1, 1);
            gfx_hline(px0 - f->width * 2, y, f->width * 2 - 2, C_RULER);
            for (int k = 1; k < 12 && o < 5; k++) gfx_hline(px0 - f->width, row_y(o * 72 + k * 6), f->width - 2, C_EDGE);
        }
        splash_captions("after Evgeny Murzin's ANS synthesizer, 1957", C_RULER);
        gfx_text(px0, py0 - f->height - f->height / 3, "A N S", f, C_RULER, -1, 1);
        gfx_text(px0 + f->width * 7, py0 - f->height - f->height / 3, "720 pure tones · 72 to the octave · light through scratches", f, C_EDGE, -1, 1);
    }
    /* engraving: each scratch grows from its start over its duration */
    if (t < T_SCAN0 + 200)
        for (int i = 0; i < ns; i++) {
            const struct stroke *s = &st[i];
            if (t < s->t0) continue;
            int xa = MIN(s->x0, s->x1), xb = MAX(s->x0, s->x1), th = MAX(1, sg.W / 700);
            int upto = t >= s->t0 + s->dur ? xb + th : xa - th + (xb - xa + 2 * th) * (t - s->t0) / MAX(1, s->dur);
            for (int x = xa - th; x <= upto; x++) {
                int ylo, yhi, lvl;
                if (!stroke_col(s, i, x, &ylo, &yhi, &lvl)) continue;
                for (int y = MAX(py0, ylo); y <= MIN(py1 - 1, yhi); y++) gfx_pixel(x, y, (uint8_t)(C_SCRATCH0 + lvl));
            }
        }
    /* the slit: columns behind it glow and fade, ahead of it nothing is lit yet */
    if (t >= T_SCAN0) {
        int xs = scan_x_q8((uint32_t)((uint64_t)t * rate / 1000)) >> 8, trail = pw / 12, halo = sg.W / 90;
        int from = MAX(px0, MIN(drawn_to, xs) - trail - 2), to = MIN(px1 - 1, xs + halo + 2);
        for (int x = from; x <= to; x++) {
            int d = xs - x;                                               /* behind the slit: > 0 */
            int glow = d < 0 ? 0 : d <= 2 ? 15 : MAX(0, 15 - d * 15 / trail);
            int hl = d < -halo || d > halo ? 0 : 15 - (d < 0 ? -d : d) * 15 / (halo + 1);
            if (t > T_SCAN1) { glow = glow * MAX(0, T_END - t) / (T_END - T_SCAN1); hl = hl * MAX(0, T_END - t) / (T_END - T_SCAN1); }
            column(x, glow, hl);
        }
        if (xs >= px0 && xs < px1 && t <= T_SCAN1) for (int k = -1; k <= 1; k++) gfx_vline(xs + k, py0, ph, k ? (uint8_t)(C_GLOW0 + 13) : C_SLIT);
        drawn_to = xs;
    }
}

static void audio(int32_t *l, int32_t *r, uint32_t n, uint32_t t) {
    int32_t dry[64];
    memset(dry, 0, sizeof dry);
    /* engraving: a burst of filtered noise as each scratch is made */
    static int32_t bp_lo, bp_bd, env;
    uint32_t tm = t * 1000 / rate, tm2 = (t + n) * 1000 / rate;
    for (int i = 0; i < ns; i++) if (st[i].t0 >= tm && st[i].t0 < tm2 && st[i].t0 < T_SCAN0) env = 9000 + (int32_t)(sp_rand(&seed) & 4095);
    if (env > 8) {
        int32_t fq = 11000 + (int32_t)(sp_rand(&seed) & 8191), qd = 9000;          /* ~2-4 kHz, a rough resonance */
        for (uint32_t i = 0; i < n; i++) {
            int32_t x = (int32_t)(sp_rand(&seed) >> 17) - 16384;
            if ((sp_rand(&seed) & 15) == 0) x *= 3;                             /* grit */
            bp_lo += (fq * bp_bd) >> 15;
            int32_t hp = ((x * env) >> 15) - bp_lo - ((qd * bp_bd) >> 15);
            bp_bd += (fq * hp) >> 15;
            bp_lo = CLAMP(bp_lo, -60000, 60000); bp_bd = CLAMP(bp_bd, -60000, 60000);
            dry[i] += bp_bd >> 1;
            env -= env >> 9;
        }
    }
    /* the slit: every tone it lights, faded in and out over a few ms */
    if (t + n > f_scan0 && t < f_scan1 + rate / 10) {
        int xq = scan_x_q8(t), slit = MAX(2 << 8, (pw << 8) / 700);
        for (int i = 0; i < ROWS; i++) row_tgt[i] = 0;
        if (t < f_scan1)
            for (int i = 0; i < ns; i++) {
                const struct stroke *s = &st[i];
                int xa = MIN(s->x0, s->x1) << 8, xb = MAX(s->x0, s->x1) << 8;
                if (xq < xa - slit || xq > xb + slit) continue;
                int edge = xq < xa ? xa - xq : xq > xb ? xq - xb : 0;
                int w = s->bright * (slit - edge) / slit;                       /* 0..255, the slit's overlap */
                if (s->kind == K_BAR) {
                    int ra = (py1 - 1 - s->y1) * ROWS / ph, rb = (py1 - 1 - s->y0) * ROWS / ph;
                    for (int k = MAX(0, ra); k <= MIN(ROWS - 1, rb); k++) row_tgt[k] = MAX(row_tgt[k], w * 50);
                } else {
                    int dx = s->x1 - s->x0, xc = CLAMP(xq, xa, xb);
                    int y_q8 = dx ? (s->y0 << 8) + (int)((int64_t)(s->y1 - s->y0) * (xc - (s->x0 << 8)) / dx) : s->y0 << 8;
                    int rq = ((py1 - 1) * 256 - y_q8) * ROWS / ph;               /* the row, Q8 */
                    int k0 = rq >> 8, fr = rq & 255;
                    if (k0 >= 0 && k0 < ROWS) row_tgt[k0] = MAX(row_tgt[k0], w * (256 - fr) / 2);
                    if (k0 + 1 >= 0 && k0 + 1 < ROWS) row_tgt[k0 + 1] = MAX(row_tgt[k0 + 1], w * fr / 2);
                }
            }
    } else for (int i = 0; i < ROWS; i++) row_tgt[i] = 0;
    for (int k = 0; k < ROWS; k++) {
        int32_t a0 = row_amp[k], tg = row_tgt[k] * (int32_t)(640 - k) / 512;       /* low tones a little louder */
        if (!a0 && !tg) continue;
        int32_t a1 = a0 + ((tg - a0) >> 4);
        if (a1 < 24 && tg == 0) a1 = 0;
        int32_t da = (a1 - a0) / (int32_t)n, a = a0;
        uint32_t ph = row_ph[k], inc = row_inc[k];
        for (uint32_t i = 0; i < n; i++, a += da) { dry[i] += (sp_sin(ph) * ((a >> 4) * 3)) >> 15; ph += inc; }   /* a line: ~-14 dBFS */
        row_ph[k] = ph; row_amp[k] = a1;
    }
    int32_t wl[64], wr[64];
    for (uint32_t i = 0; i < n; i++) { wl[i] = wr[i] = 0; l[i] += dry[i]; r[i] += dry[i]; }
    sp_verb_run(&verb, dry, dry, wl, wr, n);
    for (uint32_t i = 0; i < n; i++) { l[i] += wl[i] * 3 / 5; r[i] += wr[i] * 3 / 5; }
}

const struct splash_piece splash_ans = { "ANS", "after Evgeny Murzin's ANS synthesizer, 1957", T_END, 2200, start, draw, audio };
