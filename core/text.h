#pragma once
/* Cell grid on top of the pixel layer (gfx.h). Pages redraw the whole grid every frame; text_flush() rasterises only
   the cells that changed into the back buffer. A rectangle of cells can be handed to graphics for the frame
   (text_gfx): the grid leaves those pixels alone and the page draws them itself. */
#include <stdint.h>
#include <stdbool.h>
#include "gfx.h"

enum {
    C_BG = 0, C_PANEL, C_BORDER, C_DIM, C_TEXT, C_BRIGHT, C_AMBER, C_AMBER_D,
    C_GREEN, C_GREEN_D, C_CYAN, C_PINK, C_RED, C_SCOPE, C_SCOPE_D, C_BLACK,
};

/* Code-page indices for glyphs used directly by drawing code (see tools/psf2c.py). */
enum {
    G_HLINE = 0x80, G_VLINE, G_TL, G_TR, G_BL, G_BR, G_LT, G_RT, G_TT, G_BT, G_CROSS,
    G_DHLINE = 0x8B, G_DVLINE, G_DTL, G_DTR, G_DBL, G_DBR,
    G_RTL = 0x96, G_RTR, G_RBL, G_RBR,
    G_FULL = 0xA0, G_UPPER, G_LOWER, G_LEFT, G_RIGHT, G_SHADE1, G_SHADE2, G_SHADE3,
    G_DOT = 0xC0, G_BULLET = 0xBF, G_DIAMOND = 0xBD, G_NOTE = 0xB9, G_NOTES = 0xBA,
    G_TRI_R = 0xB5, G_TRI_L = 0xB6, G_TRI_U = 0xB7, G_TRI_D = 0xB8, G_DISC = 0xB0, G_CIRCLE = 0xB1, G_SQUARE = 0xB2,
    G_V1 = 0xA8,                                   /* ▁ … █: G_V1 + n - 1 fills the bottom n eighths (n = 1..8) */
    G_H1 = 0xD2,                                   /* ▏: 1/8 from the left; see text_hpart() for n eighths */
};
/* a cell filled n/8 from the left (n = 1..8) */
static inline uint8_t text_hpart(int n) { static const uint8_t g[9] = { ' ', 0xD2, 0xD8, 0xD9, 0xA3, 0xDA, 0xDB, 0xDC, 0xA0 }; return g[n < 0 ? 0 : n > 8 ? 8 : n]; }

void text_init(const struct font *f);            /* after gfx_init */
const struct font *text_font_doubled(const struct font *f);   /* the same glyphs at twice the size, for 4K screens */
const struct font *text_font(void);
int  text_cols(void);
int  text_rows(void);
int  text_px(int col);                           /* pixel position of a cell's left / top edge */
int  text_py(int row);
struct rect text_rect(int x, int y, int w, int h);   /* pixels covered by a rectangle of cells */
void text_clear(uint8_t bg);
void text_put(int x, int y, uint8_t glyph, uint8_t fg, uint8_t bg);
uint8_t text_peek(int x, int y);                 /* glyph currently in a cell */
void text_str(int x, int y, const char *utf8, uint8_t fg, uint8_t bg);     /* UTF-8 → code page */
void text_str_n(int x, int y, const char *utf8, int max_cells, uint8_t fg, uint8_t bg);
void text_fill(int x, int y, int w, int h, uint8_t glyph, uint8_t fg, uint8_t bg);
void text_box(int x, int y, int w, int h, uint8_t fg, uint8_t bg, bool dbl);
void text_box_title(int x, int y, int w, int h, const char *title, uint8_t fg, uint8_t title_fg, uint8_t bg, bool dbl);
struct rect text_gfx(int x, int y, int w, int h); /* hand these cells to graphics this frame; returns their pixels */
/* Rasterise a strip of the screen into a 256-sample wave: what is on screen becomes the oscillator. */
void text_scan(int row, int col0, int ncols, int16_t out[256]);
void text_flush(void);                           /* rasterise changed cells into the back buffer */
void text_invalidate(void);                      /* force every cell to be redrawn */
