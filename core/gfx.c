#include "gfx.h"
#include "libc.h"
#include "log.h"

#define MAX_H 2400
#define MAX_W 4096
#define CHUNK 32                                   /* dirty tracking granularity: 32 pixels of a row */
#define WORDS (MAX_W / CHUNK / 32)

uint64_t gfx_px_written;

static struct fb_info fb;
static int W, H, stride, bpp_bytes;
static uint8_t *back, *front;                /* indexed pixels; front mirrors what the framebuffer shows */
static uint32_t dirty[MAX_H][WORDS];         /* a bit per 32-pixel chunk of each row that may differ from the screen */
static uint8_t row_dirty[MAX_H];
static uint32_t pal_px[256];
static uint8_t pal_luma[256];
static uint32_t pal_rgb[256];
static int cx0, cy0, cx1, cy1;               /* clip rect [x0, x1) */
static int ptr_k = 1;                        /* pointer scale: 2 with the doubled fonts */

/* ---- palette: the colour schemes ----
   Each scheme is the 16 UI colours (C_* in text.h: roles, not hues — C_AMBER is the first accent, C_BLACK the ink for
   text on an accent's fill, light in the light schemes) and the ramps' accents with their hot cores. */
static const struct theme { const char *name; uint32_t base[16], ramp[R_COUNT][2]; } themes[GFX_THEMES] = {
    { "NIGHT", /* the first one: blue-grey panels, amber, green and cyan */
      { 0x0c0f14, 0x141a22, 0x2c3746, 0x5a6879, 0xb6c2ce, 0xf0f4f7, 0xffb454, 0x6e5025,
        0x7ee787, 0x2b5a3a, 0x58c7f3, 0xf286c4, 0xff6b6b, 0x5ef0a8, 0x1c4533, 0x000000 },
      { { 0x5ef0a8, 0xe0ffee }, { 0xffb454, 0xfff0d8 }, { 0x58c7f3, 0xe0f6ff }, { 0xf286c4, 0xffe4f2 },
        { 0xff6b6b, 0xffe0e0 }, { 0xb6c2ce, 0xffffff }, { 0x5b8cff, 0xe0eaff }, { 0x2c3746, 0x5a6879 } } },
    { "PHOSPHOR", /* green on black, a P1 phosphor terminal */
      { 0x020904, 0x071409, 0x164a24, 0x3a9a58, 0x6ee895, 0xc6ffd6, 0xb4ffca, 0x1f6a36,
        0x5fe08a, 0x1c5a30, 0x8cf7bc, 0x49c276, 0xe8fff0, 0x6bff9e, 0x0f3a1f, 0x000000 },
      { { 0x57e888, 0xd8ffe6 }, { 0xa8ffc2, 0xf2fff6 }, { 0x7af2b0, 0xe4fff0 }, { 0x3fae68, 0xa8eec0 },
        { 0xd8ffe4, 0xffffff }, { 0x6ee895, 0xd0ffe0 }, { 0x3a9a58, 0xa0e8b8 }, { 0x164a24, 0x3a9a58 } } },
    { "AMBER", /* orange on black, an amber monitor or a plasma display */
      { 0x0a0602, 0x170d04, 0x4a2c0a, 0x9a6220, 0xf0a040, 0xffe0b0, 0xffc878, 0x6a3e10,
        0xffb050, 0x5a3510, 0xffd898, 0xd88a38, 0xfff4e4, 0xffb040, 0x4a2808, 0x000000 },
      { { 0xffa838, 0xfff0d0 }, { 0xffc878, 0xfff6e8 }, { 0xffd898, 0xfffaf0 }, { 0xd88a38, 0xffd8a0 },
        { 0xfff0dc, 0xffffff }, { 0xf0a040, 0xffe8c8 }, { 0xb87428, 0xffd8a8 }, { 0x4a2c0a, 0x9a6220 } } },
    { "PAPER", /* dark ink on warm paper, like a printed score */
      { 0xefe9dc, 0xf9f6ee, 0xcdc3ad, 0x7d7462, 0x34302a, 0x100e0b, 0xc4461c, 0xefc7b3,
        0x2c7a4c, 0xc8ddcc, 0x1d64a0, 0xa6306a, 0xbb2230, 0x1f5a38, 0xd8dccd, 0xf9f6ee },
      { { 0x2c7a4c, 0x0f3a22 }, { 0xc4461c, 0x6e2208 }, { 0x1d64a0, 0x0a2e52 }, { 0xa6306a, 0x560a32 },
        { 0xbb2230, 0x5e0a10 }, { 0x34302a, 0x000000 }, { 0x2a4fb0, 0x0c1c50 }, { 0xcdc3ad, 0x7d7462 } } },
    { "BLUEPRINT", /* white and pale cyan on deep blue */
      { 0x0f2d5c, 0x163a70, 0x3d64a0, 0x86a6d2, 0xdce8f8, 0xffffff, 0xffe07a, 0x5a5030,
        0x9ff0ff, 0x2a5a70, 0x7fd3ff, 0xffb3d9, 0xff8f85, 0xe6f2ff, 0x2a4f86, 0x07183a },
      { { 0x9ff0ff, 0xecfdff }, { 0xffe07a, 0xfff8dc }, { 0x7fd3ff, 0xe6f6ff }, { 0xffb3d9, 0xffe8f4 },
        { 0xff8f85, 0xffe4e0 }, { 0xdce8f8, 0xffffff }, { 0xa8c4ff, 0xeef3ff }, { 0x3d64a0, 0x86a6d2 } } },
    { "LCD", /* olive greens, a handheld's reflective screen */
      { 0xb8cc4a, 0xacc23c, 0x6a9426, 0x33581c, 0x0f340f, 0x082808, 0x0f380f, 0x8fb03a,
        0x1f5a26, 0x9fbd40, 0x1c4e3a, 0x3d5a0c, 0x061c06, 0x0f380f, 0xa6bf3e, 0xd6e68a },
      { { 0x1f5a26, 0x0a2a0e }, { 0x0f380f, 0x041804 }, { 0x1c4e3a, 0x0a2418 }, { 0x3d5a0c, 0x1a2a04 },
        { 0x061c06, 0x000000 }, { 0x143c14, 0x041804 }, { 0x244a40, 0x0a2418 }, { 0x6a9426, 0x33581c } } },
    { "STAGE", /* black, white and yellow: for projectors and sunlight */
      { 0x000000, 0x0a0a0a, 0x707070, 0xd0d0d0, 0xffffff, 0xffffff, 0xffe600, 0x5a5000,
        0x2dff6a, 0x005a20, 0x00e5ff, 0xff4fcf, 0xff2d2d, 0xffe600, 0x403a00, 0x000000 },
      { { 0x2dff6a, 0xe0ffe8 }, { 0xffe600, 0xfffbd0 }, { 0x00e5ff, 0xd8fbff }, { 0xff4fcf, 0xffe0f6 },
        { 0xff2d2d, 0xffe0e0 }, { 0xd0d0d0, 0xffffff }, { 0x5a8cff, 0xe0eaff }, { 0x707070, 0xd0d0d0 } } },
    { "CREAM", /* 80s cream and brown, an Omnichord's */
      { 0xe6d9bb, 0xf3ead2, 0xb6a178, 0x86704c, 0x3a2816, 0x1c1208, 0xa8480c, 0xf2c7a0,
        0x587f2e, 0xd7dcb4, 0x1f6f7e, 0xb3405e, 0xb02a1a, 0x5a3818, 0xd8c8a4, 0xf7f0dc },
      { { 0x587f2e, 0x26400e }, { 0xa8480c, 0x5a2204 }, { 0x1f6f7e, 0x0a3440 }, { 0xb3405e, 0x5a1026 },
        { 0xb02a1a, 0x5a0e06 }, { 0x3a2816, 0x000000 }, { 0x3a58a0, 0x142a58 }, { 0xb6a178, 0x86704c } } },
    { "ACID", /* neon on black */
      { 0x08000f, 0x150624, 0x4a1a7a, 0x9a6ad8, 0xece4ff, 0xffffff, 0xffe600, 0x5a4f00,
        0x39ff14, 0x145a08, 0x00f0ff, 0xff2bd6, 0xff3b3b, 0x39ff14, 0x10400a, 0x000000 },
      { { 0x39ff14, 0xe4ffd8 }, { 0xffe600, 0xfffbd0 }, { 0x00f0ff, 0xd8ffff }, { 0xff2bd6, 0xffd8f6 },
        { 0xff3b3b, 0xffe0e0 }, { 0xc8b8ff, 0xffffff }, { 0x7a5cff, 0xe8e0ff }, { 0x4a1a7a, 0x9a6ad8 } } },
    { "SUNSET", /* orange, pink and teal on deep purple */
      { 0x180a2c, 0x29124a, 0x6b2a86, 0xb47ab0, 0xffdbe9, 0xfff5e6, 0xff9e2c, 0x5c2c0c,
        0x2cffc8, 0x0f5a48, 0x38d8ff, 0xff4fa0, 0xff4a4a, 0xff7ad0, 0x4a1a4a, 0x0c0418 },
      { { 0x2cffc8, 0xe0fff6 }, { 0xff9e2c, 0xfff0d0 }, { 0x38d8ff, 0xe0f8ff }, { 0xff4fa0, 0xffe0f0 },
        { 0xff4a4a, 0xffe0e0 }, { 0xffdbe9, 0xffffff }, { 0x7a6aff, 0xe8e4ff }, { 0x6b2a86, 0xb47ab0 } } },
};
static int theme_now;
static bool repaint;

static void set_pal(int i, int r, int g, int b) {
    uint32_t rv = (uint32_t)r >> (8 - fb.r_size), gv = (uint32_t)g >> (8 - fb.g_size), bv = (uint32_t)b >> (8 - fb.b_size);
    pal_px[i] = (rv << fb.r_shift) | (gv << fb.g_shift) | (bv << fb.b_shift);
    pal_luma[i] = (uint8_t)((r * 2 + g * 5 + b) / 8);
    pal_rgb[i] = (uint32_t)(r << 16 | g << 8 | b);
}
static int mix(int a, int b, int num, int den) { return a + (b - a) * num / den; }
static int ch(uint32_t c, int k) { return (int)(c >> (16 - 8 * k) & 0xFF); }

/* each ramp: background → accent over levels 0..12, then accent → the hot core over 13..15 */
static void build_palette(void) {
    const struct theme *t = &themes[theme_now];
    for (int i = 0; i < 16; i++) set_pal(i, ch(t->base[i], 0), ch(t->base[i], 1), ch(t->base[i], 2));
    for (int r = 0; r < R_COUNT; r++)
        for (int l = 0; l < 16; l++) {
            int c[3];
            for (int k = 0; k < 3; k++) {
                int bg = ch(t->base[0], k), a = ch(t->ramp[r][0], k), h = ch(t->ramp[r][1], k);
                c[k] = l <= 12 ? mix(bg, a, l, 12) : mix(a, h, l - 12, 3);
            }
            set_pal(16 + r * 16 + l, c[0], c[1], c[2]);
        }
}

void gfx_theme(int t) {
    theme_now = ((t % GFX_THEMES) + GFX_THEMES) % GFX_THEMES;
    build_palette();
    gfx_repaint();
}
int gfx_theme_now(void) { return theme_now; }
const char *gfx_theme_name(int t) { return themes[((t % GFX_THEMES) + GFX_THEMES) % GFX_THEMES].name; }
void gfx_color(int i, int r, int g, int b) { if (i >= 0 && i < 256) set_pal(i, r, g, b); }
uint32_t gfx_rgb(uint8_t i) { return pal_rgb[i]; }
void gfx_repaint(void) { repaint = true; gfx_dirty(0, 0, W, H); }

bool gfx_init(const struct fb_info *info) {
    fb = *info;
    W = (int)fb.width; H = (int)fb.height; if (H > MAX_H) H = MAX_H; if (W > MAX_W) W = MAX_W;
    stride = (W + 3) & ~3;
    bpp_bytes = (fb.bpp + 7) / 8;
    back = plat_alloc((uint32_t)(stride * H));
    front = plat_alloc((uint32_t)(stride * H));
    if (!back || !front) { logf("gfx: no memory for a %dx%d back buffer", W, H); return false; }
    build_palette();
    repaint = true;                             /* the first present writes everything */
    gfx_noclip();
    gfx_dirty(0, 0, W, H);
    return true;
}
uint8_t gfx_luma(uint8_t i) { return pal_luma[i]; }
int gfx_width(void) { return W; }
int gfx_height(void) { return H; }
uint8_t *gfx_row(int y) { return back + (size_t)y * stride; }

static inline void mark(int x0, int x1, int y) {
    int c0 = x0 / CHUNK, c1 = (x1 - 1) / CHUNK;
    uint32_t *d = dirty[y];
    if (c0 >> 5 == c1 >> 5) d[c0 >> 5] |= (0xFFFFFFFFu >> (31 - (c1 & 31))) & (0xFFFFFFFFu << (c0 & 31));
    else for (int c = c0; c <= c1; c++) d[c >> 5] |= 1u << (c & 31);
    row_dirty[y] = 1;
}
void gfx_dirty(int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
    for (int j = y; j < y1; j++) mark(x, x1, j);
}

/* ---- clipping + primitives ---- */
void gfx_clip(int x, int y, int w, int h) {
    cx0 = MAX(0, x); cy0 = MAX(0, y); cx1 = MIN(W, x + w); cy1 = MIN(H, y + h);
}
void gfx_noclip(void) { cx0 = 0; cy0 = 0; cx1 = W; cy1 = H; }

void gfx_fill(int x, int y, int w, int h, uint8_t c) {
    int x1 = MIN(x + w, cx1), y1 = MIN(y + h, cy1);
    x = MAX(x, cx0); y = MAX(y, cy0);
    if (x >= x1 || y >= y1) return;
    int n = x1 - x;
    uint8_t *p = back + (size_t)y * stride + x;
    if (n < 16) {                                   /* narrow: plain stores beat a memset call per row */
        for (int j = y; j < y1; j++, p += stride) { for (int i = 0; i < n; i++) p[i] = c; mark(x, x1, j); }
        return;
    }
    for (int j = y; j < y1; j++, p += stride) { memset(p, c, (size_t)n); mark(x, x1, j); }
}
void gfx_pixel(int x, int y, uint8_t c) {
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    back[(size_t)y * stride + x] = c; mark(x, x + 1, y);
}
void gfx_hline(int x, int y, int w, uint8_t c) { gfx_fill(x, y, w, 1, c); }
void gfx_vline(int x, int y, int h, uint8_t c) {
    if (h < 0) { y += h; h = -h; }
    if (x < cx0 || x >= cx1) return;
    int y1 = MIN(y + h, cy1); y = MAX(y, cy0);
    uint8_t *p = back + (size_t)y * stride + x;
    for (int j = y; j < y1; j++, p += stride) { *p = c; mark(x, x + 1, j); }
}
void gfx_frame(int x, int y, int w, int h, uint8_t c) {
    gfx_hline(x, y, w, c); gfx_hline(x, y + h - 1, w, c); gfx_vline(x, y, h, c); gfx_vline(x + w - 1, y, h, c);
}
void gfx_line(int x0, int y0, int x1, int y1, uint8_t c) {
    if (y0 == y1) { if (x1 < x0) { int t = x0; x0 = x1; x1 = t; } gfx_hline(x0, y0, x1 - x0 + 1, c); return; }
    if (x0 == x1) { if (y1 < y0) { int t = y0; y0 = y1; y1 = t; } gfx_vline(x0, y0, y1 - y0 + 1, c); return; }
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int bx0 = MIN(x0, x1), bx1 = MAX(x0, x1), by0 = MIN(y0, y1), by1 = MAX(y0, y1);
    if (bx0 >= cx0 && by0 >= cy0 && bx1 < cx1 && by1 < cy1) {
        /* all inside: mark the bounding box once, then plain stores */
        for (int y = by0; y <= by1; y++) mark(bx0, bx1 + 1, y);
        uint8_t *p = back + (size_t)y0 * stride + x0;
        int step_y = sy * stride;
        for (;;) {
            *p = c;
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; p += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; p += step_y; }
        }
        return;
    }
    for (;;) {
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
/* how far row j of a rounded box is pulled in by a corner of radius r (pixel centres inside the circle) */
static int corner_inset(int j, int h, int r) {
    int jj = j < r ? j : j >= h - r ? h - 1 - j : -1;
    if (jj < 0) return 0;
    int dy = 2 * r - 2 * jj - 1;
    for (int i = 0; i < r; i++) { int dx = 2 * r - 2 * i - 1; if (dx * dx + dy * dy <= 4 * r * r) return i; }
    return r;
}
void gfx_round(int x, int y, int w, int h, int r, uint8_t fill, int outline) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) { int in = corner_inset(j, h, r); gfx_hline(x + in, y + j, w - 2 * in, fill); }
    if (outline < 0) return;
    for (int j = 0; j < h; j++) {
        int in = corner_inset(j, h, r);
        if (j == 0 || j == h - 1) { gfx_hline(x + in, y + j, w - 2 * in, (uint8_t)outline); continue; }
        /* reach over to the neighbouring rows' edges so the curve has no gaps */
        int reach = MAX(corner_inset(j - 1, h, r), corner_inset(j + 1, h, r));
        int span = MAX(1, reach - in);
        gfx_hline(x + in, y + j, span, (uint8_t)outline);
        gfx_hline(x + w - in - span, y + j, span, (uint8_t)outline);
    }
}
void gfx_disc(int cx, int cy, int r, uint8_t c) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0; while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        gfx_hline(cx - dx, cy + dy, 2 * dx + 1, c);
    }
}
void gfx_ring(int cx, int cy, int r, uint8_t c) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_pixel(cx + x, cy + y, c); gfx_pixel(cx - x, cy + y, c); gfx_pixel(cx + x, cy - y, c); gfx_pixel(cx - x, cy - y, c);
        gfx_pixel(cx + y, cy + x, c); gfx_pixel(cx - y, cy + x, c); gfx_pixel(cx + y, cy - x, c); gfx_pixel(cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}

/* ---- text ---- */
uint8_t gfx_codepage(uint32_t cp) {
    if (cp < 0x80) return (uint8_t)cp;
    int lo = 0, hi = CODEPAGE_SYMBOLS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (codepage_symbols[mid].cp == cp) return codepage_symbols[mid].idx;
        if (codepage_symbols[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return '?';
}
const char *gfx_utf8_next(const char *s, uint8_t *glyph) {
    const uint8_t *p = (const uint8_t *)s;
    uint32_t cp; int len;
    if (*p < 0x80) { cp = *p; len = 1; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; len = 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; len = 3; }
    else { cp = *p & 0x07; len = 4; }
    for (int i = 1; i < len; i++) { if (!p[i]) { len = i; break; } cp = (cp << 6) | (p[i] & 0x3F); }
    *glyph = gfx_codepage(cp);
    return s + len;
}

/* nibble of glyph bits (MSB = leftmost pixel) → 4 byte-lane mask, little endian */
static const uint32_t nib_mask[16] = {
    0x00000000, 0xFF000000, 0x00FF0000, 0xFFFF0000, 0x0000FF00, 0xFF00FF00, 0x00FFFF00, 0xFFFFFF00,
    0x000000FF, 0xFF0000FF, 0x00FF00FF, 0xFFFF00FF, 0x0000FFFF, 0xFF00FFFF, 0x00FFFFFF, 0xFFFFFFFF,
};
typedef struct __attribute__((packed, may_alias)) { uint32_t v; } u32u;

void gfx_glyph(int x, int y, const struct font *f, uint8_t g, uint8_t fg, int bg, int scale) {
    const uint8_t *bits = f->bits + (size_t)g * f->height * f->stride;
    int w = f->width * scale, h = f->height * scale;
    bool inside = x >= cx0 && y >= cy0 && x + w <= cx1 && y + h <= cy1;
    if (inside && bg >= 0 && scale == 1 && (f->width & 3) == 0) {
        /* fast path: four pixels per store */
        uint32_t fg4 = fg * 0x01010101u, bg4 = (uint8_t)bg * 0x01010101u;
        for (int r = 0; r < f->height; r++) {
            uint8_t *d = back + (size_t)(y + r) * stride + x;
            const uint8_t *row = bits + r * f->stride;
            for (int n = 0; n < f->width / 4; n++) {
                uint32_t m = nib_mask[(row[n >> 1] >> ((n & 1) ? 0 : 4)) & 15];
                ((u32u *)(d + n * 4))->v = (fg4 & m) | (bg4 & ~m);
            }
            mark(x, x + w, y + r);
        }
        return;
    }
    if (inside && scale == 1) {                    /* transparent background: store only the set pixels */
        for (int r = 0; r < f->height; r++) {
            const uint8_t *row = bits + r * f->stride;
            uint8_t *d = back + (size_t)(y + r) * stride + x;
            for (int c = 0; c < f->width; c++) {
                if (row[c >> 3] & (0x80 >> (c & 7))) d[c] = fg;
                else if (bg >= 0) d[c] = (uint8_t)bg;
            }
            mark(x, x + w, y + r);
        }
        return;
    }
    for (int r = 0; r < f->height; r++) {
        const uint8_t *row = bits + r * f->stride;
        for (int c = 0; c < f->width; c++) {
            bool on = row[c >> 3] & (0x80 >> (c & 7));
            if (!on && bg < 0) continue;
            gfx_fill(x + c * scale, y + r * scale, scale, scale, on ? fg : (uint8_t)bg);
        }
    }
}
int gfx_text(int x, int y, const char *s, const struct font *f, uint8_t fg, int bg, int scale) {
    int x0 = x;
    while (*s) { uint8_t g; s = gfx_utf8_next(s, &g); gfx_glyph(x, y, f, g, fg, bg, scale); x += f->width * scale; }
    return x - x0;
}
int gfx_text_width(const char *s, const struct font *f, int scale) {
    int n = 0; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++;
    return n * f->width * scale;
}

/* ---- pointer sprite ---- */
struct sprite { int w, h, hx, hy; const char *rows[20]; };
static const struct sprite sprites[3] = {
    { 0, 0, 0, 0, { 0 } },
    { 12, 19, 0, 0, {
        "K...........", "KK..........", "KWK.........", "KWWK........", "KWWWK.......", "KWWWWK......",
        "KWWWWWK.....", "KWWWWWWK....", "KWWWWWWWK...", "KWWWWWWWWK..", "KWWWWWWWWWK.", "KWWWWWWKKKKK",
        "KWWWKWWK....", "KWWKKWWK....", "KWK..KWWK...", "KK...KWWK...", "K.....KWWK..", "......KWWK..", ".......KK..." } },
    { 13, 13, 6, 6, {
        ".....KKK.....", ".....KPK.....", ".....KPK.....", ".....KPK.....", ".....KPK.....", "KKKKKK.KKKKKK",
        "KPPPP.....PPK", "KKKKKK.KKKKKK", ".....KPK.....", ".....KPK.....", ".....KPK.....", ".....KPK.....", ".....KKK....." } },
};
static int ptr_x, ptr_y, ptr_shape, shown_x, shown_y, shown_shape;

void gfx_pointer(int x, int y, int shape) { ptr_x = x; ptr_y = y; ptr_shape = shape; }
void gfx_pointer_scale(int k) { ptr_k = k < 1 ? 1 : k; }

static int sprite_px(const struct sprite *s, int sx, int sy, int px, int py) {
    if (px < sx || py < sy) return -1;
    int u = (px - sx) / ptr_k, v = (py - sy) / ptr_k;
    if (u >= s->w || v >= s->h) return -1;
    char ch = s->rows[v][u];
    return ch == 'K' ? 15 : ch == 'W' ? 5 : ch == 'P' ? 11 : -1;     /* C_BLACK, C_BRIGHT, C_PINK */
}

/* ---- present ---- */
static void put_run(int y, int x0, int x1, const uint8_t *src) {
    uint8_t *dst = (uint8_t *)fb.addr + (size_t)y * fb.pitch;
    if (bpp_bytes == 4) {
        uint32_t *d = (uint32_t *)dst + x0;
        for (int x = x0; x < x1; x++) *d++ = pal_px[src[x]];
    } else if (bpp_bytes == 3) {
        uint8_t *d = dst + x0 * 3;
        for (int x = x0; x < x1; x++) { uint32_t p = pal_px[src[x]]; d[0] = (uint8_t)p; d[1] = (uint8_t)(p >> 8); d[2] = (uint8_t)(p >> 16); d += 3; }
    } else {
        uint16_t *d = (uint16_t *)dst + x0;
        for (int x = x0; x < x1; x++) *d++ = (uint16_t)pal_px[src[x]];
    }
    gfx_px_written += (uint64_t)(x1 - x0);
}

/* compare one chunk with what is on screen; write the pixels that differ, in runs of whole words */
static void present_chunk(int y, int x0, int x1, const uint8_t *src, uint8_t *fr, bool all) {
    if (all) { put_run(y, x0, MIN(x1, W), src); memcpy(fr + x0, src + x0, (size_t)(x1 - x0)); return; }
    int x = x0;
    while (x < x1) {
        if (((const u32u *)(src + x))->v == ((const u32u *)(fr + x))->v) { x += 4; continue; }
        int s = x;
        while (x < x1 && ((const u32u *)(src + x))->v != ((const u32u *)(fr + x))->v) x += 4;
        int e = MIN(x, W);
        if (s < e) put_run(y, s, e, src);
        memcpy(fr + s, src + s, (size_t)(x - s));        /* whole words, so the row padding matches too */
    }
}

uint32_t gfx_speed_mbs;
void gfx_present(void) {
    static uint8_t tmp[MAX_W + 4];
    bool all = repaint;                                  /* the palette changed: every pixel, once */
    repaint = false;
    uint64_t t0 = all && !gfx_speed_mbs ? plat_us() : 0; /* the first whole frame is timed: the screen memory's speed */
    const struct sprite *sn = &sprites[ptr_shape], *so = &sprites[shown_shape];
    const int k = ptr_k, nw = sn->w * k, nh = sn->h * k, ow = so->w * k, oh = so->h * k;
    int nx = ptr_x - sn->hx * k, ny = ptr_y - sn->hy * k, ox = shown_x - so->hx * k, oy = shown_y - so->hy * k;
    for (int y = 0; y < H; y++) {
        bool in_new = nh && y >= ny && y < ny + nh, in_old = oh && y >= oy && y < oy + oh;
        if (in_new && nx + nw > 0 && nx < W) mark(MAX(0, nx), MIN(W, nx + nw), y);
        if (in_old && ox + ow > 0 && ox < W) mark(MAX(0, ox), MIN(W, ox + ow), y);
        if (!row_dirty[y]) continue;
        row_dirty[y] = 0;
        const uint8_t *src = back + (size_t)y * stride;
        uint8_t *fr = front + (size_t)y * stride;
        if (in_new) {                                    /* the pointer: composite it into a copy of the row */
            memcpy(tmp, src, (size_t)stride);
            for (int x = MAX(0, nx); x < MIN(W, nx + nw); x++) { int c = sprite_px(sn, nx, ny, x, y); if (c >= 0) tmp[x] = (uint8_t)c; }
            src = tmp;
        }
        for (int w = 0; w < WORDS; w++) {
            uint32_t bits = dirty[y][w];
            if (!bits) continue;
            dirty[y][w] = 0;
            while (bits) {
                int b = __builtin_ctz(bits); bits &= bits - 1;
                int x0 = (w * 32 + b) * CHUNK;
                if (x0 >= stride) break;
                present_chunk(y, x0, MIN(stride, x0 + CHUNK), src, fr, all);
            }
        }
    }
    shown_x = ptr_x; shown_y = ptr_y; shown_shape = ptr_shape;
    if (t0) {
        uint64_t us = plat_us() - t0, bytes = (uint64_t)W * H * bpp_bytes;
        gfx_speed_mbs = (uint32_t)MAX(1u, us ? bytes / us : bytes);       /* bytes a µs = MB/s */
        logf("gfx: a whole frame (%dx%d, %d bytes a pixel) written in %u.%02u ms: %u MB/s", W, H, bpp_bytes,
             (unsigned)(us / 1000), (unsigned)(us % 1000 / 10), gfx_speed_mbs);
    }
}
