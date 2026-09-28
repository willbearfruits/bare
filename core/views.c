/* Pages made of views: the bar, and handing everything else to the view showing (see views.h). */
#include "views.h"
#include "lineage.h"
#include "fkeys.h"
#include "app.h"

bool views_key(struct viewset *s, uint8_t code, bool down, uint64_t now) { return s->v[s->cur]->key_event(code, down, now); }
bool views_typing(const struct viewset *s) { return s->v[s->cur]->typing && s->v[s->cur]->typing(); }
bool views_midi(struct viewset *s, uint8_t note, uint8_t vel, uint64_t now) { return s->v[s->cur]->midi ? s->v[s->cur]->midi(note, vel, now) : false; }
void views_again(struct viewset *s) { if (!views_typing(s)) s->cur = (s->cur + 1) % s->n; }
void views_goto(struct viewset *s, int v) {
    if (v < 0 || v >= s->n || views_typing(s) || v == s->cur) return;
    s->cur = v;
    ui_redraw_all();                                          /* as a page switch: nothing of the last view stays */
}

void views_pointer(struct viewset *s, uint64_t now) {
    if (ptr.pressed && ptr.y / text_font()->height == s->row) {   /* the bar */
        int cx = ptr.x / text_font()->width;
        for (int i = 0; i < s->n; i++) if (cx >= s->bar_x[i] && cx < s->bar_x[i + 1] - 1) { views_goto(s, i); return; }
    }
    if (s->v[s->cur]->pointer) s->v[s->cur]->pointer(now);
}

void views_draw(struct viewset *s, uint64_t now) {
    int cols = text_cols(), x = 2;
    bool years = true;                                        /* the years, where they fit */
    for (int pass = 0; pass < 2; pass++) {
        int end = 2;
        for (int i = 0; i < s->n; i++) end += ui_cells(s->v[i]->name) + 3 + (years && s->v[i]->year ? 1 + ui_cells(s->v[i]->year) : 0);
        if (end + 18 < cols) break;
        years = false;
    }
    for (int i = 0; i < s->n; i++) {
        bool on = i == s->cur;
        const char *yr = years ? s->v[i]->year : 0;
        int w = ui_cells(s->v[i]->name) + 2 + (yr ? 1 + ui_cells(yr) : 0);
        s->bar_x[i] = x;
        text_fill(x, s->row, w, 1, ' ', C_TEXT, on ? C_CYAN : C_BG);
        text_str(x + 1, s->row, s->v[i]->name, on ? C_BLACK : C_DIM, on ? C_CYAN : C_BG);
        if (yr) text_str(x + 2 + ui_cells(s->v[i]->name), s->row, yr, on ? C_BLACK : C_BORDER, on ? C_CYAN : C_BG);
        x += w + 1;
    }
    s->bar_x[s->n] = x;
    int k = s->step_key ? s->step_key() : -1;
    char nv[24]; snfmt(nv, sizeof nv, "%s: next", k >= 0 ? fkey_names[k] : "");
    if (k >= 0 && x + 12 < cols) text_str(cols - 3 - ui_cells(nv), s->row, nv, C_DIM, C_BG);
    s->v[s->cur]->draw(now);
}

const struct viewset *views_of(int page) { return page == PAGE_LINEAGE ? lineage_views() : 0; }
int views_count(int page) { const struct viewset *s = views_of(page); return s ? s->n : 0; }
const char *views_name(int page, int v) { const struct viewset *s = views_of(page); return s && v >= 0 && v < s->n ? s->v[v]->name : ""; }
