/* GENDY: Iannis Xenakis's dynamic stochastic synthesis (GENDY3, 1991). A wave is a polygon of breakpoints; after every
   period each point's height and each segment's length take a random step (Cauchy-distributed: mostly small, now and
   then huge), and elastic walls — mirrors — throw back whatever crosses them. Loose mirrors and big steps: noise and
   wandering pitch; tight ones: a steady tone with a shape of its own. Three voices start calm, go wild, and then the
   mirrors close in on an A major chord while the letters of the name random-walk home. */
#include "splash_int.h"
#include "platform.h"

#define NP 12                                         /* breakpoints a period */
#define NV 3
#define T_END 6200
#define TRAILS 6
static const int16_t cauchy[256] = {                  /* tan(pi (u - 1/2)), x256, clipped at 24 */
    -6144, -6144, -6144, -5957, -4631, -3787, -3203, -2774, -2445, -2186, -1976, -1802, -1656, -1531, -1423, -1330,
    -1247, -1174, -1108, -1049, -996, -948, -903, -863, -826, -791, -759, -730, -702, -676, -652, -629, -607, -587,
    -568, -550, -533, -516, -501, -486, -472, -458, -446, -433, -421, -410, -399, -388, -378, -368, -359, -350, -341,
    -332, -324, -316, -308, -300, -293, -286, -279, -272, -266, -259, -253, -247, -241, -235, -229, -224, -218, -213,
    -207, -202, -197, -192, -187, -183, -178, -173, -169, -164, -160, -156, -151, -147, -143, -139, -135, -131, -127,
    -123, -119, -115, -112, -108, -104, -101, -97, -93, -90, -86, -83, -79, -76, -73, -69, -66, -62, -59, -56, -53,
    -49, -46, -43, -40, -36, -33, -30, -27, -24, -20, -17, -14, -11, -8, -5, -2, 2, 5, 8, 11, 14, 17, 20, 24, 27, 30,
    33, 36, 40, 43, 46, 49, 53, 56, 59, 62, 66, 69, 73, 76, 79, 83, 86, 90, 93, 97, 101, 104, 108, 112, 115, 119, 123,
    127, 131, 135, 139, 143, 147, 151, 156, 160, 164, 169, 173, 178, 183, 187, 192, 197, 202, 207, 213, 218, 224, 229,
    235, 241, 247, 253, 259, 266, 272, 279, 286, 293, 300, 308, 316, 324, 332, 341, 350, 359, 368, 378, 388, 399, 410,
    421, 433, 446, 458, 472, 486, 501, 516, 533, 550, 568, 587, 607, 629, 652, 676, 702, 730, 759, 791, 826, 863, 903,
    948, 996, 1049, 1108, 1174, 1247, 1330, 1423, 1531, 1656, 1802, 1976, 2186, 2445, 2774, 3203, 3787, 4631, 5957,
    6144, 6144, 6144,
};

struct gvoice {
    int32_t y[NP], d_q8[NP], vd[NP];                  /* heights (±32767), segment lengths (samples, Q8), their drift */
    int seg; int32_t pos_q8, val_q12, slope_q12, lp1, lp2, dc_q8;
    int32_t target_q8, env;                           /* the segment length the chord wants; the level (Q15) */
    int pan;                                          /* -256 (left) .. 256 */
};
static struct gvoice vo[NV];
static uint32_t rate, seed;
static volatile int temp;                             /* the temperature, 0..256: step sizes and mirror widths */
static volatile int32_t mirror_y;
static struct sp_verb verb;
/* the picture's copy of the shapes, and the last frames' for trails */
static int32_t shape_y[TRAILS][NV][NP], shape_d[TRAILS][NV][NP];
static int trail_n;
enum { C_BGG = GFX_FREE_COLOR, C_MIRROR, C_DIMT, C_TXT, C_V0 = GFX_FREE_COLOR + 6, C_V1 = C_V0 + 16, C_V2 = C_V1 + 16, C_LET = C_V2 + 16 };

static int temperature(int t) {                       /* calm, a storm, then the crystal */
    if (t < 700) return 30 + t * 30 / 700;
    if (t < 2200) return 60 + (t - 700) * 196 / 1500;
    if (t < 3000) return 256;
    if (t < 4800) { int u = (4800 - t) * 256 / 1800; return u * u / 256; }
    return 0;
}

static void segment(struct gvoice *v) {               /* entering a segment: the slope to the next point */
    int nx = (v->seg + 1) % NP;
    v->val_q12 = v->y[v->seg] << 12;
    int32_t len = MAX(1, v->d_q8[v->seg] >> 8);
    v->slope_q12 = ((v->y[nx] - v->y[v->seg]) << 12) / len;
}

static void walk(struct gvoice *v) {                  /* a period has passed: every point takes a step */
    int tp = temp, ym = mirror_y;
    int32_t ystep = 150 + 2800 * tp / 256;
    int32_t w = v->target_q8 * (tp * 6 / 10 + (tp ? 8 : 0)) / 256;          /* the length mirrors: ±60 % in a storm */
    int32_t dstep = v->target_q8 * (1 + 12 * tp / 256) / 256, vmax = v->target_q8 * (4 + 26 * tp / 256) / 256;
    for (int i = 0; i < NP; i++) {
        int32_t y = v->y[i] + cauchy[sp_rand(&seed) & 255] * ystep / 256;
        if (y > ym) y = 2 * ym - y;                                          /* the mirrors throw it back */
        if (y < -ym) y = -2 * ym - y;
        v->y[i] = CLAMP(y, -ym, ym);
        int32_t vd = v->vd[i] + cauchy[sp_rand(&seed) & 255] * dstep / 256;
        if (!tp) vd = vd * 7 / 8;
        v->vd[i] = CLAMP(vd, -vmax, vmax);
        int32_t d = v->d_q8[i] + v->vd[i], lo = v->target_q8 - w, hi = v->target_q8 + w;
        if (d > hi) { d = 2 * hi - d; v->vd[i] = -v->vd[i]; }
        if (d < lo) { d = 2 * lo - d; v->vd[i] = -v->vd[i]; }
        v->d_q8[i] = CLAMP(d, MAX(lo, 256), MAX(hi, 256));
    }
}

static void start(uint32_t r) {
    rate = r; seed = 0x47454E44u; temp = 30; mirror_y = 14000; trail_n = 0;
    static const uint32_t hz_q8[NV] = { 28160, 42192, 70958 };         /* A2 E3 C#4, Q8 Hz */
    static const int pans[NV] = { 0, -150, 150 };
    for (int k = 0; k < NV; k++) {
        struct gvoice *v = &vo[k];
        memset(v, 0, sizeof *v);
        v->target_q8 = (int32_t)(((uint64_t)r << 16) / hz_q8[k] / NP);        /* samples a segment, Q8 */
        for (int i = 0; i < NP; i++) {
            v->d_q8[i] = v->target_q8;
            v->y[i] = (int32_t)(sp_sin((uint32_t)i * (0xFFFFFFFFu / NP)) * 3 / 5) + (int32_t)(sp_rand(&seed) % 2001) - 1000;
        }
        v->pan = pans[k];
        segment(v);
    }
    sp_verb_init(&verb, r, 25000, 12000);
    splash_ramp(C_BGG, 1, 0x07060c, 0x07060c); splash_ramp(C_MIRROR, 1, 0x2c2446, 0x2c2446);
    splash_ramp(C_DIMT, 1, 0x6a5f8a, 0x6a5f8a); splash_ramp(C_TXT, 1, 0xc8bff0, 0xc8bff0);
    splash_ramp(C_V0, 13, 0x07060c, 0xff4fa0); splash_ramp(C_V0 + 12, 4, 0xff4fa0, 0xffe4f2);
    splash_ramp(C_V1, 13, 0x07060c, 0x38d8ff); splash_ramp(C_V1 + 12, 4, 0x38d8ff, 0xe0f8ff);
    splash_ramp(C_V2, 13, 0x07060c, 0xffb454); splash_ramp(C_V2 + 12, 4, 0xffb454, 0xfff0d8);
    splash_ramp(C_LET, 13, 0x07060c, 0xb8b0e8); splash_ramp(C_LET + 12, 4, 0xb8b0e8, 0xffffff);
}

static int env_target(int k, int t) {                 /* the voices come in one by one and fade under the instrument */
    static const int in_ms[NV] = { 0, 900, 1500 };
    if (t < in_ms[k]) return 0;
    int a = MIN(32767, (t - in_ms[k]) * 32767 / 500);
    if (t > T_END - 900) a = (int)((int64_t)a * MAX(0, T_END + 800 - t) / 1700);
    return a;
}

static void audio(int32_t *l, int32_t *r, uint32_t n, uint32_t t) {
    int tm = (int)(t * 1000 / rate);
    temp = temperature(tm);
    mirror_y = 12000 + 16000 * temp / 256;
    int32_t dl[64], dr[64];
    memset(dl, 0, sizeof dl); memset(dr, 0, sizeof dr);
    for (int k = 0; k < NV; k++) {
        struct gvoice *v = &vo[k];
        int32_t e0 = v->env, e1 = e0 + ((env_target(k, tm) - e0) >> 3), de = (e1 - e0) / (int32_t)n, e = e0;
        if (!e0 && !e1) continue;
        for (uint32_t i = 0; i < n; i++, e += de) {
            int32_t x = v->val_q12 >> 12;
            v->val_q12 += v->slope_q12;
            v->pos_q8 += 256;
            if (v->pos_q8 >= v->d_q8[v->seg]) {
                v->pos_q8 -= v->d_q8[v->seg];
                if (++v->seg == NP) { v->seg = 0; walk(v); }
                segment(v);
            }
            v->lp1 += ((x - v->lp1) * 5) >> 4;                          /* two poles near 3 kHz: the edges softened */
            v->lp2 += ((v->lp1 - v->lp2) * 5) >> 4;
            v->dc_q8 += ((v->lp2 << 8) - v->dc_q8) >> 9;                 /* a random polygon has a mean: take it out (~15 Hz) */
            int32_t ac = v->lp2 - (v->dc_q8 >> 8);
            int32_t s = (ac * (e >> 3)) * 3 >> 14;                         /* ~-9 dBFS a voice at full level */
            dl[i] += (s * (256 - v->pan)) >> 9; dr[i] += (s * (256 + v->pan)) >> 9;
        }
        v->env = e1;
    }
    for (uint32_t i = 0; i < n; i++) { l[i] += dl[i]; r[i] += dr[i]; }
    int32_t wl[64], wr[64];
    memset(wl, 0, sizeof wl); memset(wr, 0, sizeof wr);
    sp_verb_run(&verb, dl, dr, wl, wr, n);
    for (uint32_t i = 0; i < n; i++) { l[i] += wl[i] * 2 / 5; r[i] += wr[i] * 2 / 5; }
}

/* a smooth random wander for the letters: three sines with random speeds and phases, per letter and axis */
static int wander(uint32_t id, int t) {
    int s = 0;
    for (int k = 0; k < 3; k++) {
        uint32_t h = (id * 2654435761u + (uint32_t)k * 40503u) ^ 0x9E3779B9u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        uint32_t speed = 300 + (h & 1023), ph = h << 12;
        s += sp_sin(ph + (uint32_t)t * speed * 4096u) >> (k + 1);
    }
    return s;                                         /* about ±28000 */
}

static void draw(int t, bool first) {
    const struct font *f = sg.f;
    (void)first;
    gfx_fill(0, 0, sg.W, sg.H, C_BGG);
    int x0 = sg.W * 6 / 100, x1 = sg.W * 94 / 100, yc = sg.H * 35 / 100, amp = sg.H * 22 / 100;
    /* the mirrors, dashed */
    int my = amp * mirror_y / 32767;
    for (int x = x0; x < x1; x += 12) { gfx_hline(x, yc - my, 6, C_MIRROR); gfx_hline(x, yc + my, 6, C_MIRROR); }
    gfx_hline(x0, yc, x1 - x0, C_MIRROR);
    /* this frame's shapes: copied from the voices in one go, the older ones kept for the trails */
    memmove(shape_y[1], shape_y[0], sizeof shape_y[0] * (TRAILS - 1)); memmove(shape_d[1], shape_d[0], sizeof shape_d[0] * (TRAILS - 1));
    uint32_t fl = plat_irq_save();
    for (int k = 0; k < NV; k++) for (int i = 0; i < NP; i++) { shape_y[0][k][i] = vo[k].y[i]; shape_d[0][k][i] = vo[k].d_q8[i]; }
    int32_t env[NV]; for (int k = 0; k < NV; k++) env[k] = vo[k].env;
    plat_irq_restore(fl);
    if (trail_n < TRAILS) trail_n++;
    for (int a = trail_n - 1; a >= 0; a--)
        for (int k = 0; k < NV; k++) {
            if (env[k] < 800) continue;
            int lvl = a == 0 ? 14 : 11 - a * 2;
            if (lvl <= 0) continue;
            lvl = lvl * MIN(32767, env[k] * 2) / 32767;
            uint8_t c = (uint8_t)((k == 0 ? C_V0 : k == 1 ? C_V1 : C_V2) + MAX(1, lvl));
            int32_t total = 0; for (int i = 0; i < NP; i++) total += shape_d[a][k][i];
            int32_t acc = 0, px = x0, py = yc - amp * shape_y[a][k][0] / 32767;
            for (int i = 1; i <= NP; i++) {
                acc += shape_d[a][k][i - 1];
                int nx = x0 + (int)((int64_t)(x1 - x0) * acc / MAX(1, total)), ny = yc - amp * shape_y[a][k][i % NP] / 32767;
                gfx_line(px, py, nx, ny, c);
                if (a == 0) gfx_fill(px - 2, py - 2, 5, 5, (uint8_t)((k == 0 ? C_V0 : k == 1 ? C_V1 : C_V2) + 15));
                px = nx; py = ny;
            }
        }
    /* the words */
    char line[120];
    gfx_text(f->width * 2, f->height, "GENDY", f, C_TXT, -1, 1);
    gfx_text(f->width * 9, f->height, "dynamic stochastic synthesis", f, C_DIMT, -1, 1);
    snfmt(line, sizeof line, "3 voices · %d breakpoints · Cauchy steps · temperature %d.%02d · mirrors ±%d.%02d", NP,
          temp / 256, temp * 100 / 256 % 100, mirror_y / 32767, mirror_y * 100 / 32767 % 100);
    gfx_text(f->width * 2, f->height * 2 + f->height / 3, line, f, C_DIMT, -1, 1);
    /* the name: each character wanders at the storm's temperature and is pulled home as it cools */
    int ly = sg.H * 63 / 100, pull = t < 1200 ? 0 : MIN(256, (t - 1200) * 256 / 3400);
    pull = pull * pull / 256;
    uint32_t id = 0;
    for (int r = 0; r < LOGO_ROWS; r++)
        for (int c = 0; c < LOGO_COLS; c++) {
            char ch = splash_logo[r][c];
            if (ch == ' ') continue;
            id++;
            int hx = sg.lx + c * sg.cw, hy = ly + r * sg.ch;
            uint32_t h = id * 2654435761u; h ^= h >> 16;
            int sx = (int)(h % (uint32_t)sg.W), sy = (int)((h >> 8) % (uint32_t)(sg.H - sg.ch));
            int far = 256 - pull, jig = temperature(t) * sg.W / 2600;
            int x = hx + (sx - hx) * far / 256 + wander(id * 2, t) * jig / 28000;
            int y = hy + (sy - hy) * far / 256 + wander(id * 2 + 1, t) * jig / 28000;
            int lvl = 4 + pull * 11 / 256;
            if (t < 400) lvl = lvl * t / 400;
            if (lvl > 0) splash_glyph(x, y, ch, (uint8_t)(C_LET + lvl), sg.scale);
        }
    splash_captions("after Iannis Xenakis, GENDY3, 1991", C_DIMT);
    gfx_dirty(0, 0, sg.W, sg.H);
}

const struct splash_piece splash_gendy = { "GENDY", "after Iannis Xenakis, GENDY3, 1991", T_END, 1500, start, draw, audio };
