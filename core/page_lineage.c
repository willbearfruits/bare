/* LINEAGE (F11 by default): the homages, as views (see lineage.h, views.h), XENAKIS among them with views of its own
   (core/page_xen.c). The page's key again steps to the next; the bar under the title names them, with their years,
   and a click picks one. It opens on ANS, as F11 did before XENAKIS moved in. */
#include "lineage.h"
#include "xen.h"
#include "fkeys.h"
#include "app.h"
#include "gfx.h"

/* the key stepping the homages: the one that opened the page (XENAKIS's steps its own views), else one opening it whole */
static int step_key(void) {
    int k = app_key();
    if (fkey_page(&fkeys[k]) == PAGE_LINEAGE && !(fkeys[k].kind == FK_VIEW && fkeys[k].view == LV_XEN)) return k;
    for (int i = 0; i < FKEYS; i++) if (fkeys[i].kind == FK_PAGE && fkeys[i].page == PAGE_LINEAGE) return i;
    return -1;
}

static const struct view *const views[] = { &lin_xen, &lin_ans, &lin_reich, &lin_carlos, &lin_radigue, &lin_merzbow };
static struct viewset set = { .v = views, .n = (int)ARRAY_LEN(views), .cur = LV_ANS, .row = 1, .step_key = step_key };

const struct viewset *lineage_views(void) { return &set; }
int  lineage_current(void) { return set.cur; }
void lineage_goto(int v) { views_goto(&set, v); }

void lineage_layout(struct lin_layout *L, int knob_w) {
    int cols = text_cols(), rows = text_rows();
    L->x = 2; L->y = 2;
    L->lh = ui_lesson_rows();
    L->ly = rows - 1 - L->lh;
    L->kw = knob_w;
    L->pw = cols - 4 - knob_w - 1;
    L->kx = L->x + L->pw + 1;
    L->ph = L->ly - L->y;
}
void lineage_knob(int x, int y, int w, const char *name, const char *value, bool on) {
    text_put(x, y, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
    text_str_n(x + 2, y, name, 9, on ? C_BRIGHT : C_TEXT, C_PANEL);
    text_str_n(x + 11, y, value, w - 12, on ? C_AMBER : C_DIM, C_PANEL);
}

static bool key(uint8_t code, bool down, uint64_t now) { return views_key(&set, code, down, now); }
static bool typing(void) { return views_typing(&set); }
static bool midi(uint8_t note, uint8_t vel, uint64_t now) { return views_midi(&set, note, vel, now); }
static void again(uint64_t now) { (void)now; views_again(&set); }
static void pointer(uint64_t now) { views_pointer(&set, now); }
static void draw(uint64_t now) { views_draw(&set, now); }

const struct page page_lineage = { "LINEAGE", false, key, typing, pointer, 0, draw, true, midi, "LIN", again };
