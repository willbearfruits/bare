/* XENAKIS, inside LINEAGE (F12 opens it by default): Iannis Xenakis's ways of making music, as views in the order of
   their music — METASTASEIS (string glissandi as ruled surfaces, 1954), CLOUDS (masses of notes set by probabilities,
   1956), SIEVES (scales and rhythms from residue classes, 1966), UPIC (draw arcs, hear glissandi, 1977) and GENDY (his
   stochastic waveforms, as sounds to play anywhere, 1991). To LINEAGE it is one view, XENAKIS 1954, whose hooks run this
   set; its bar is the row under LINEAGE's, a click picks a view, and the key that opened XENAKIS pressed again steps to
   the next (core/app.c). */
#include "xen.h"
#include "lineage.h"
#include "fkeys.h"
#include "app.h"
#include "gfx.h"

/* the key stepping these views: the one that opened XENAKIS (or one of its views), else any key that opens it */
static int step_key(void) {
    int k = app_key();
    if (fkeys[k].kind == FK_VIEW && fkeys[k].view == LV_XEN) return k;
    for (int i = 0; i < FKEYS; i++) if (fkeys[i].kind == FK_VIEW && fkeys[i].view == LV_XEN) return i;
    return -1;
}

static const struct view *const views[] = { &xen_meta, &xen_cloud, &xen_sieve, &xen_upic, &xen_gendy };
static struct viewset set = { .v = views, .n = (int)ARRAY_LEN(views), .cur = XV_META, .row = 2, .step_key = step_key };   /* Metastaseis first: where it began */

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

const struct viewset *xen_views(void) { return &set; }
int  xen_current(void) { return set.cur; }
const char *xen_view_name(int v) { return v >= 0 && v < set.n ? set.v[v]->name : ""; }
void xen_goto(int v) { views_goto(&set, v); }
void xen_again(void) { views_again(&set); }

static bool key(uint8_t code, bool down, uint64_t now) { return views_key(&set, code, down, now); }
static bool typing(void) { return views_typing(&set); }
static bool midi(uint8_t note, uint8_t vel, uint64_t now) { return views_midi(&set, note, vel, now); }
static void pointer(uint64_t now) { views_pointer(&set, now); }
static void draw(uint64_t now) { views_draw(&set, now); }

const struct view lin_xen = { "XENAKIS", key, typing, pointer, draw, midi, "1954" };
