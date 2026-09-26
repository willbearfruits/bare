#include "text.h"
#include "libc.h"

#define MAX_COLS 256
#define MAX_ROWS 128
#define GFX_CELL 0xFF                   /* fg value marking a cell that graphics own this frame */

struct cell { uint8_t ch, fg, bg; };
static struct cell cur[MAX_ROWS * MAX_COLS], prev[MAX_ROWS * MAX_COLS];
static const struct font *font;
static int cols, rows, ox, oy;
static bool dirty_all = true;

void text_init(const struct font *f) {
    font = f;
    int w = gfx_width(), h = gfx_height();
    cols = w / font->width; rows = h / font->height;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    ox = ((w - cols * font->width) / 2) & ~3;        /* word-aligned: glyph rows go out four pixels per store */
    oy = (h - rows * font->height) / 2;
    gfx_fill(0, 0, w, h, C_BG);                      /* the centering margins */
    text_clear(C_BG);
    text_invalidate();
}

const struct font *text_font(void) { return font; }

const struct font *text_font_doubled(const struct font *f) {
    static uint8_t bits[256 * 48 * 3];               /* room for 12x24 doubled */
    static struct font big;
    big = (struct font){ (uint8_t)(f->width * 2), (uint8_t)(f->height * 2), (uint8_t)((f->width * 2 + 7) / 8), bits };
    memset(bits, 0, sizeof bits);
    for (int g = 0; g < 256; g++)
        for (int r = 0; r < f->height; r++)
            for (int c = 0; c < f->width; c++) {
                if (!(f->bits[(g * f->height + r) * f->stride + (c >> 3)] & (0x80 >> (c & 7)))) continue;
                for (int d = 0; d < 2; d++) bits[(g * big.height + r * 2 + d) * big.stride + (c >> 2)] |= (uint8_t)(0xC0 >> ((c * 2) & 7));
            }
    return &big;
}
int text_cols(void) { return cols; }
int text_rows(void) { return rows; }
int text_px(int col) { return ox + col * font->width; }
int text_py(int row) { return oy + row * font->height; }
struct rect text_rect(int x, int y, int w, int h) { return (struct rect){ text_px(x), text_py(y), w * font->width, h * font->height }; }

void text_clear(uint8_t bg) {
    for (int i = 0; i < rows * cols; i++) cur[i] = (struct cell){ ' ', C_TEXT, bg };
}
void text_invalidate(void) { dirty_all = true; }

void text_put(int x, int y, uint8_t glyph, uint8_t fg, uint8_t bg) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) return;
    cur[y * cols + x] = (struct cell){ glyph, fg, bg };
}
uint8_t text_peek(int x, int y) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) return ' ';
    return cur[y * cols + x].ch;
}

void text_str_n(int x, int y, const char *s, int max_cells, uint8_t fg, uint8_t bg) {
    while (*s && max_cells != 0) {
        uint8_t g; s = gfx_utf8_next(s, &g);
        text_put(x++, y, g, fg, bg);
        if (max_cells > 0) max_cells--;
    }
}
void text_str(int x, int y, const char *s, uint8_t fg, uint8_t bg) { text_str_n(x, y, s, -1, fg, bg); }

void text_fill(int x, int y, int w, int h, uint8_t glyph, uint8_t fg, uint8_t bg) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) text_put(x + i, y + j, glyph, fg, bg);
}

void text_box(int x, int y, int w, int h, uint8_t fg, uint8_t bg, bool dbl) {
    uint8_t hl = dbl ? G_DHLINE : G_HLINE, vl = dbl ? G_DVLINE : G_VLINE;
    uint8_t tl = dbl ? G_DTL : G_RTL, tr = dbl ? G_DTR : G_RTR, bl = dbl ? G_DBL : G_RBL, br = dbl ? G_DBR : G_RBR;
    text_fill(x + 1, y + 1, w - 2, h - 2, ' ', fg, bg);
    for (int i = 1; i < w - 1; i++) { text_put(x + i, y, hl, fg, bg); text_put(x + i, y + h - 1, hl, fg, bg); }
    for (int j = 1; j < h - 1; j++) { text_put(x, y + j, vl, fg, bg); text_put(x + w - 1, y + j, vl, fg, bg); }
    text_put(x, y, tl, fg, bg); text_put(x + w - 1, y, tr, fg, bg);
    text_put(x, y + h - 1, bl, fg, bg); text_put(x + w - 1, y + h - 1, br, fg, bg);
}
void text_box_title(int x, int y, int w, int h, const char *title, uint8_t fg, uint8_t title_fg, uint8_t bg, bool dbl) {
    text_box(x, y, w, h, fg, bg, dbl);
    int n = 0; for (const char *p = title; *p; p++) if ((*p & 0xC0) != 0x80) n++;
    text_put(x + 2, y, ' ', fg, bg);
    text_str(x + 3, y, title, title_fg, bg);
    text_put(x + 3 + n, y, ' ', fg, bg);
}

struct rect text_gfx(int x, int y, int w, int h) {
    for (int j = MAX(0, y); j < MIN(rows, y + h); j++)
        for (int i = MAX(0, x); i < MIN(cols, x + w); i++) cur[j * cols + i] = (struct cell){ 0, GFX_CELL, GFX_CELL };
    return text_rect(x, y, w, h);
}

void text_flush(void) {
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            struct cell *c = &cur[y * cols + x], *p = &prev[y * cols + x];
            if (!dirty_all && c->ch == p->ch && c->fg == p->fg && c->bg == p->bg) continue;
            *p = *c;
            if (c->fg != GFX_CELL) gfx_glyph(ox + x * font->width, oy + y * font->height, font, c->ch, c->fg, c->bg, 1);
        }
    dirty_all = false;
}
