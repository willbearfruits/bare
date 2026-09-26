#pragma once
/* Pages and the parts they are built from. A page is one file (core/page_*.c): its state, its keys, its pointer
   handling and its drawing. The frame around it (title bar, footer, pointer) and the shared widgets are core/ui.c. */
#include <stdint.h>
#include <stdbool.h>
#include "text.h"
#include "libc.h"

enum { PAGE_PLAY, PAGE_SEQ, PAGE_WAVE, PAGE_STRETCH, PAGE_FM, PAGE_TAPE, PAGE_FILE, PAGE_MIX, PAGE_TOUCH, PAGE_FX, PAGE_ANS, PAGE_XEN, PAGE_COUNT };   /* F1 … F12 */

struct page {
    const char *name;                  /* tab label */
    const char *key_name;              /* the key that opens it, as printed on the tab */
    uint8_t     key;                   /* KEY_F1 … */
    bool        plays_omni;            /* keys the page doesn't use still play the chord buttons and strum plate */
    bool (*key_event)(uint8_t code, bool down, uint64_t now);   /* true if the page used the key */
    bool (*typing)(void);              /* optional: true while the page takes typed text (global keys step aside) */
    void (*pointer)(uint64_t now);     /* optional: pointer interaction, once per frame before drawing */
    int  (*strum_sound)(void);         /* optional: the preset the strum plate plays on this page, -1 = normal */
    void (*draw)(uint64_t now);
    bool        owns_pad;              /* the page plays the touchpad itself (strum plate, pen); else a finger moves the pointer */
    bool (*midi)(uint8_t note, uint8_t vel, uint64_t now);   /* optional: a MIDI note the page takes (vel 0: its note off) */
    const char *short_name;            /* optional: the tab's label where the full ones don't fit (100 columns) */
    void (*again)(uint64_t now);       /* optional: the page's own key pressed on it (a page with views steps to the next) */
};
extern const struct page page_play, page_seq, page_wave, page_stretch, page_fm, page_file, page_tape, page_mix, page_touch, page_fx, page_ans,
                         page_xen;
extern const struct page *const ui_pages[PAGE_COUNT];
void play_show(int k);                  /* PLAY shows the omnichord (0) or instrument k - 1 (F1 again steps) */
extern bool ui_shift;                   /* a Shift key is down: Shift+key is the function layer (app.c) */
extern bool ui_help;                    /* ⇧? shows the global keys in place of the page */

void ui_draw(uint64_t now, int page);                          /* a whole frame: chrome, page, pointer */
void ui_notice(const char *msg, uint64_t now);                 /* a few words in the title bar for a moment ("undone: …") */
void ui_boot_message(const char *line1, const char *line2);    /* full-screen status before the UI runs */
void ui_redraw_all(void);                                      /* after something else had the screen: all of it anew */

/* ---- pointer, in pixels ---- */
struct pointer {
    int x, y, buttons;
    bool down, pressed, released;      /* left button held / went down / went up since the last frame */
    uint64_t moved_ms;
    uint8_t shape;                     /* PTR_*: pages set it while drawing */
};
extern struct pointer ptr;
void ui_pointer_update(uint64_t now, bool page_owns_pad);

/* ---- a touchpad in absolute mode: where the fingers are (x, y 0..32767 across the pad, 0,0 top left). The first
   fields are finger 0, which moves the pointer; f[] has every finger (a slot keeps its finger while it stays down), with
   its pressure z (30 and up; 60 where the pad can't tell) and contact size (0..255, 0 = unknown). ---- */
#define FINGERS 5
struct finger { bool on, touched, lifted; int x, y, z, size; };     /* touched/lifted: since the last frame */
struct touchpad { bool present, on, touched, lifted; int x, y, z; int n; struct finger f[FINGERS]; };
extern struct touchpad pad;
static inline bool ui_in(struct rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/* ---- building blocks (positions in cells unless the name says px) ---- */
void ui_panel(int x, int y, int w, int h, const char *title, uint8_t accent);
struct rect ui_canvas(int x, int y, int w, int h, uint8_t bg); /* cells handed to graphics, cleared; their pixels */
/* The same, for a canvas whose picture depends only on `key`: if this canvas was drawn with this key last frame, its
   pixels are still on screen and it returns false — skip drawing. Hash what the picture shows into the key. */
bool ui_canvas_keyed(struct rect *r, int x, int y, int w, int h, uint8_t bg, uint32_t key);
static inline uint32_t ui_hash(uint32_t h, const void *data, int n) {
    const uint8_t *p = data; for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h;
}
static inline uint32_t ui_hash_int(uint32_t h, int32_t v) { return ui_hash(h, &v, sizeof v); }
#define UI_HASH0 2166136261u
/* key legend: pairs of strings, key then what it does ("SPACE", "play"). Wraps into at most maxrows rows and drops
   what doesn't fit, so put the important ones first. y < 0 only measures. Returns the rows used. */
int  ui_legend(int x, int y, int w, int maxrows, const char *const *pairs, int npairs, uint8_t bg);
#define LEGEND(x, y, w, rows, bg, ...) ({ static const char *const k_[] = { __VA_ARGS__ }; ui_legend(x, y, w, rows, k_, (int)(ARRAY_LEN(k_) / 2), bg); })
void ui_footer(const char *const *pairs, int npairs);
#define FOOTER(...) do { static const char *const k_[] = { __VA_ARGS__ }; ui_footer(k_, (int)(ARRAY_LEN(k_) / 2)); } while (0)
void ui_label(int x, int y, const char *label, const char *value, uint8_t value_fg, uint8_t bg);   /* "label value" */
void ui_led(int x, int y, bool on, uint8_t color, const char *label, uint8_t bg);
void ui_bar(int x, int y, int w, int value, int max, uint8_t fg, uint8_t bg);       /* eighth-cell resolution */
int  ui_cells(const char *utf8);                                                     /* display width */

/* pixel widgets */
void ui_scope(int x, int y, int w, int h);           /* a canvas: the phosphor scope of the output (updates itself) */
void ui_stereo(int x, int y, int w, int h);          /* a canvas: the stereo image, L/R Lissajous */
void ui_wave_px(struct rect r, const int16_t *tab, int len, uint8_t line, uint8_t fill);   /* one cycle, filled */
void ui_meter_px(struct rect r, int level, int max, bool vertical);                  /* green → amber → red */
void ui_scan(int16_t out[256]);        /* the SCAN wave: the pixels under the pointer, read as a waveform */
