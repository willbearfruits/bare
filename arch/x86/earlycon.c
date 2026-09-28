/* Until the instrument takes the screen, the log is also drawn on the framebuffer, under the name in ASCII, so a
   machine that stops during boot shows where (most laptops have no serial port). Log lines wrap round to the top of
   their area instead of scrolling: reading back an uncached framebuffer to scroll it is slow. The line after the
   newest is kept blank. */
#include "earlycon.h"
#include "font.h"
#include "log.h"
#include "splash.h"
#include "app.h"

static struct fb_info fb;
static bool on;
static int col, row, cols, rows, top, bytes;
static uint32_t grey, amber, dim;

static uint32_t rgb(uint32_t c) {
    uint32_t r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    return (r >> (8 - fb.r_size)) << fb.r_shift | (g >> (8 - fb.g_size)) << fb.g_shift | (b >> (8 - fb.b_size)) << fb.b_shift;
}

static void put(int x, int y, uint32_t v) {
    uint8_t *p = (uint8_t *)fb.addr + (size_t)y * fb.pitch + (size_t)x * bytes;
    if (bytes == 4) *(volatile uint32_t *)p = v;
    else if (bytes == 3) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); }
    else *(volatile uint16_t *)p = (uint16_t)v;
}

/* a character with its top left at pixel x, y, each font pixel s x s */
static void glyph(int x, int y, uint8_t ch, uint32_t color, int s) {
    const uint8_t *g = font_t8x16.bits + (size_t)ch * 16;
    for (int gy = 0; gy < 16; gy++)
        for (int gx = 0; gx < 8; gx++)
            for (int k = 0; k < s * s; k++) put(x + gx * s + k % s, y + gy * s + k / s, (g[gy] & (0x80 >> gx)) ? color : 0);
}
static void cell(int cx, int cy, uint8_t ch) { glyph(cx * 8, cy * 16, ch, grey, 1); }

static void clear_row(int r) { for (int c = 0; c < cols; c++) cell(c, r, ' '); }

static void newline(void) {
    col = 0;
    row = top + (row - top + 1) % (rows - top);
    clear_row(row);
    clear_row(top + (row - top + 1) % (rows - top));
}

void earlycon_putc(char c) {
    if (!on) return;
    uint8_t u = (uint8_t)c;
    if (c == '\n') { newline(); return; }
    if (u >= 0x80) { if ((u & 0xC0) == 0x80) return; u = '-'; }       /* UTF-8: one '-' per character */
    if (col >= cols) newline();
    cell(col++, row, u);
}

void earlycon_start(const struct fb_info *f) {
    fb = *f; bytes = (fb.bpp + 7) / 8;
    if (!fb.addr || bytes < 2 || bytes > 4) return;
    cols = (int)fb.width / 8; rows = (int)fb.height / 16;
    if (cols < 20 || rows < 4) return;
    grey = rgb(0x8a96a3); amber = rgb(0xffb454); dim = rgb(0x5a6879);
    for (int r = 0; r < rows; r++) clear_row(r);
    /* the name, then the release, then the log under them */
    top = 0;
    if (rows >= 16 && cols >= SPLASH_LOGO_COLS + 2) {
        int s = fb.width >= SPLASH_LOGO_COLS * 8 * 2 + 64 && rows >= 26 ? 2 : 1;
        int x0 = ((int)fb.width - SPLASH_LOGO_COLS * 8 * s) / 2, y0 = 16;
        for (int r = 0; r < SPLASH_LOGO_ROWS; r++)
            for (int c = 0; c < SPLASH_LOGO_COLS; c++)
                if (splash_logo[r][c] != ' ') glyph(x0 + c * 8 * s, y0 + r * 16 * s, (uint8_t)splash_logo[r][c], amber, s);
        const char *rel = "release " BARE_RELEASE BARE_STAGE;
        int rl = 0; while (rel[rl]) rl++;
        int ry = (y0 + SPLASH_LOGO_ROWS * 16 * s) / 16 + 1, rx = (cols - rl) / 2;
        for (const char *p = rel; *p; p++) glyph(rx++ * 8, ry * 16, (uint8_t)*p, dim, 1);
        top = ry + 2;
    }
    on = true; col = 0; row = top;
    char line[160]; int n = 0;                                          /* what was logged before the screen was known */
    while (n < 400 && log_line(n, line, sizeof line) >= 0) n++;
    for (int i = n - 1; i >= 0; i--) {
        log_line(i, line, sizeof line);
        for (const char *p = line; *p; p++) earlycon_putc(*p);
        earlycon_putc('\n');
    }
}

void earlycon_stop(void) { on = false; }
