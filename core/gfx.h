#pragma once
/* Pixel layer. Everything on screen — text cells and graphics — is drawn into an 8-bit indexed back buffer in RAM;
   gfx_present() compares it with what is already on screen and writes only the pixels that changed. Framebuffer
   memory is often uncached on real machines, so the number of pixels written is what the UI costs. */
#include <stdint.h>
#include <stdbool.h>
#include "platform.h"
#include "font.h"

/* Palette: 0..15 are the UI colours (C_* in text.h), then 16-step ramps from the background to an accent colour,
   used for glow, fades and gradients: ramp(R_GREEN, 15) is full green, ramp(R_GREEN, 0) is the background.
   The colour scheme decides them all; entries from GFX_FREE_COLOR on are for pictures (the splash sets its own). */
enum { R_GREEN = 0, R_AMBER, R_CYAN, R_PINK, R_RED, R_GRAY, R_BLUE, R_PANEL, R_COUNT };
static inline uint8_t ramp(int r, int level) { return (uint8_t)(16 + r * 16 + (level < 0 ? 0 : level > 15 ? 15 : level)); }

#define GFX_FREE_COLOR 144
#define GFX_THEMES 10
void gfx_theme(int t);                                /* the colour scheme: the palette rebuilt, every pixel rewritten */
int  gfx_theme_now(void);
const char *gfx_theme_name(int t);
void gfx_color(int index, int r, int g, int b);      /* one palette entry (pictures: GFX_FREE_COLOR and up) */
uint32_t gfx_rgb(uint8_t index);                      /* 0xRRGGBB */
void gfx_repaint(void);                               /* every pixel rewritten at the next present */

struct rect { int x, y, w, h; };

bool     gfx_init(const struct fb_info *fb);
int      gfx_width(void);
int      gfx_height(void);
uint8_t *gfx_row(int y);                              /* back buffer row; callers that write through it mark it dirty */
void     gfx_dirty(int x, int y, int w, int h);

/* drawing (clipped to the clip rect, which starts as the whole screen) */
void gfx_clip(int x, int y, int w, int h);
void gfx_noclip(void);
void gfx_fill(int x, int y, int w, int h, uint8_t c);
void gfx_pixel(int x, int y, uint8_t c);
void gfx_hline(int x, int y, int w, uint8_t c);
void gfx_vline(int x, int y, int h, uint8_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint8_t c);
void gfx_frame(int x, int y, int w, int h, uint8_t c);            /* 1-pixel outline */
void gfx_round(int x, int y, int w, int h, int r, uint8_t fill, int outline);   /* rounded box; outline < 0 = none */
void gfx_disc(int cx, int cy, int r, uint8_t c);
void gfx_ring(int cx, int cy, int r, uint8_t c);
/* text: bg < 0 draws only the set pixels; scale >= 1 enlarges each font pixel */
void gfx_glyph(int x, int y, const struct font *f, uint8_t g, uint8_t fg, int bg, int scale);
int  gfx_text(int x, int y, const char *utf8, const struct font *f, uint8_t fg, int bg, int scale);   /* returns width */
int  gfx_text_width(const char *utf8, const struct font *f, int scale);

/* pointer sprite, composited when presenting (it never touches the back buffer) */
enum { PTR_HIDDEN = 0, PTR_ARROW, PTR_CROSS };
void gfx_pointer(int x, int y, int shape);
void gfx_pointer_scale(int k);                  /* 2: the sprite drawn at twice the size (4K) */

void gfx_present(void);
extern uint64_t gfx_px_written;                      /* framebuffer pixels written since boot */
extern uint32_t gfx_speed_mbs;                       /* how fast the first whole frame went to the screen, MB/s */
uint8_t gfx_luma(uint8_t index);                     /* brightness of a palette entry, 0..255 */

/* UTF-8 decoding onto the font's code page (shared with the text layer) */
uint8_t gfx_codepage(uint32_t codepoint);
const char *gfx_utf8_next(const char *s, uint8_t *glyph);
