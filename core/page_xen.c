/* XENAKIS (F12, Ctrl+=): Iannis Xenakis's ways of making music, as four views — UPIC (draw arcs, hear glissandi),
   GENDY (his stochastic waveforms, as sounds to play anywhere), CLOUDS (masses of notes set by probabilities) and
   SIEVES (scales and rhythms from residue classes). The page's key again steps to the next view; the bar under the
   title shows them, and a click picks one. */
#include "xen.h"
#include "gfx.h"
#include "keys.h"

static const struct xen_view *const views[] = { &xen_upic, &xen_gendy, &xen_cloud, &xen_sieve };
#define VIEWS ((int)ARRAY_LEN(views))
static int view = XV_GENDY;                                   /* GENDY first: a sound to play at once */
static int bar_x[VIEWS + 1];

static const char kb_low[] = "zsxdcvgbhnjm,l.;/", kb_high[] = "q2w3er5t6y7ui9o0p";
int xen_key_note(uint8_t code, int octave) {
    for (int i = 0; kb_low[i]; i++) if (code == (uint8_t)kb_low[i]) return 12 * (octave + 1) + i;
    for (int i = 0; kb_high[i]; i++) if (code == (uint8_t)kb_high[i]) return 12 * (octave + 2) + i;
    return -1;
}

void xen_line(int x0, int y0, int x1, int y1, uint8_t c) {
    gfx_line(x0, y0, x1, y1, c);
    if (text_font()->height >= 32) { gfx_line(x0 + 1, y0, x1 + 1, y1, c); gfx_line(x0, y0 + 1, x1, y1 + 1, c); }
}

static bool key(uint8_t code, bool down, uint64_t now) { return views[view]->key_event(code, down, now); }
static bool typing(void) { return views[view]->typing && views[view]->typing(); }
static bool midi(uint8_t note, uint8_t vel, uint64_t now) { return views[view]->midi ? views[view]->midi(note, vel, now) : false; }
static void again(uint64_t now) { (void)now; if (!typing()) view = (view + 1) % VIEWS; }
int  xen_current(void) { return view; }
void xen_goto(int v) { if (v >= 0 && v < VIEWS && !typing()) view = v; }

static void pointer(uint64_t now) {
    if (ptr.pressed && ptr.y / text_font()->height == 1) {   /* the view bar */
        int cx = ptr.x / text_font()->width;
        for (int i = 0; i < VIEWS; i++) if (cx >= bar_x[i] && cx < bar_x[i + 1] - 1) { view = i; return; }
    }
    if (views[view]->pointer) views[view]->pointer(now);
}

static void draw(uint64_t now) {
    int cols = text_cols(), x = 2;
    for (int i = 0; i < VIEWS; i++) {
        bool on = i == view;
        int w = ui_cells(views[i]->name) + 2;
        bar_x[i] = x;
        text_fill(x, 1, w, 1, ' ', C_TEXT, on ? C_CYAN : C_BG);
        text_str(x + 1, 1, views[i]->name, on ? C_BLACK : C_DIM, on ? C_CYAN : C_BG);
        x += w + 1;
    }
    bar_x[VIEWS] = x;
    if (x + 18 < cols) text_str(cols - 17, 1, "F12: next view", C_DIM, C_BG);
    views[view]->draw(now);
}

const struct page page_xen = { "XENAKIS", "F12", KEY_F12, false, key, typing, pointer, 0, draw, true, midi, "XEN", again };
