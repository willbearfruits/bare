/* METASTASEIS (XENAKIS, a view; after Iannis Xenakis, Metastaseis, 1953-54): families of string glissandi on graph
   paper (core/meta.c). Drag a line with the mouse to set the family's guide A, another for guide B (Tab picks which);
   two fingers on the touchpad hold the guide's two ends and move it live, the strings re-strung as they go. Up to 46
   strings a family, evenly spaced or in the golden section's proportions, straight or crossed; four families. Space
   plays the page: the cursor sounds every string it crosses. Enter writes the family onto UPIC's page. The picture is
   redrawn where the cursor was and is, and whole when the families change. */
#include "xen.h"
#include "meta.h"
#include "harmony.h"
#include "omni.h"
#include "upic.h"
#include "gfx.h"
#include "keys.h"
#include "undo.h"
#include "lessons.h"

enum { K_FAMILY, K_ON, K_STRINGS, K_CROSS, K_MODULOR, K_SECTION, K_LEVEL, K_LENGTH, KNOBS };
static const char *const knob_names[KNOBS] = { "family", "on", "strings", "crossed", "modulor", "section", "level", "length" };
static const uint8_t lengths[] = { 1, 2, 4, 8, 16, 32 };                 /* bars; then seconds */
static const uint8_t secs[] = { 10, 20, 30, 60, 120, 240 };
static int knob = K_STRINGS, fam, guide;                                 /* guide: which a line drawn sets, 0 A, 1 B */
static struct rect page_px;
static int drag_t0 = -1, drag_p0, drag_t1, drag_p1, drag_x0 = -1, drag_x1;
static int shown_cursor = -1;
static uint64_t clear_ms;
static uint32_t chord_seen; static bool harmony_seen;

static struct meta_family *F(void) { return &meta.fam[fam]; }
static int length_index(void) {
    if (meta.bars) { for (int i = 0; i < 6; i++) if (lengths[i] >= meta.bars) return i; return 5; }
    for (int i = 0; i < 6; i++) if (secs[i] >= meta.seconds) return 6 + i;
    return 11;
}
static void set_length(int i) { i = CLAMP(i, 0, 11); if (i < 6) meta.bars = lengths[i]; else { meta.bars = 0; meta.seconds = secs[i - 6]; } }
static void edit(uint64_t now) { char w[32]; snfmt(w, sizeof w, "METASTASEIS family %d", fam + 1); undo_one(U_META, 0, w, now); }

/* ---- the page's geometry: time across, pitch up (C1 .. C8, as UPIC's) ---- */
static int t_of(int x) { return CLAMP((x - page_px.x) * 65535 / MAX(1, page_px.w - 1), 0, 65535); }
static int p_of(int y) { return CLAMP(UPIC_LO + (page_px.y + page_px.h - 1 - y) * (UPIC_HI - UPIC_LO) / MAX(1, page_px.h - 1), UPIC_LO, UPIC_HI); }
static int x_of(int t) { return page_px.x + (int)((uint32_t)t * (uint32_t)(page_px.w - 1) / 65535); }
static int y_of(int p) { return page_px.y + page_px.h - 1 - (CLAMP(p, UPIC_LO, UPIC_HI) - UPIC_LO) * (page_px.h - 1) / (UPIC_HI - UPIC_LO); }
/* each section its ink, as the score's pages are coloured in books about it: violins I, II, violas, cellos, basses */
static uint8_t ink(int section, int f, int bright) {
    static const uint8_t r[5] = { R_AMBER, R_CYAN, R_GREEN, R_PINK, R_RED };
    return ramp(r[section % 5], CLAMP((f == fam ? 9 : 6) + bright, 1, 15));
}

/* ---- the picture: graph paper, the strings, the current family's guides ---- */
static void band(int x0, int x1) {
    struct rect r = page_px;
    x0 = MAX(x0, r.x); x1 = MIN(x1, r.x + r.w);
    if (x1 <= x0) return;
    gfx_clip(x0, r.y, x1 - x0, r.h);
    gfx_fill(x0, r.y, x1 - x0, r.h, C_BG);
    int g = MAX(6, r.w / 96);                                  /* graph paper: a fine grid, every fifth line stronger */
    for (int x = x0; x < x1; x++) {
        int k = (x - r.x) / g, on = (x - r.x) % g == 0;
        if (on) for (int y = r.y; y < r.y + r.h; y += 2) gfx_fill(x, y, 1, 1, ramp(R_PANEL, k % 5 ? 3 : 6));
    }
    for (int y = r.y + r.h - 1, k = 0; y >= r.y; y -= g, k++)
        for (int x = x0 + ((2 - (x0 - r.x) % 2) % 2); x < x1; x += 2) gfx_fill(x, y, 1, 1, ramp(R_PANEL, k % 5 ? 3 : 6));
    const struct meta_string *s; int n = meta_strings(&s);
    for (int i = 0; i < n; i++) {
        int xa = x_of(s[i].t0), xb = x_of(s[i].t1);
        if (xb < x0 - 1 || xa > x1) continue;
        xen_line(xa, y_of(s[i].p0), xb, y_of(s[i].p1), ink(s[i].section, s[i].fam, 0));
    }
    const struct meta_line *gd[2] = { &F()->a, &F()->b };
    for (int k = 0; k < 2; k++) {                              /* the guides: bright, their ends marked */
        int xa = x_of(gd[k]->t0), xb = x_of(gd[k]->t1), ya = y_of(gd[k]->p0), yb = y_of(gd[k]->p1);
        uint8_t c = k == guide ? C_BRIGHT : ramp(R_GRAY, 11);
        xen_line(xa, ya, xb, yb, c); gfx_ring(xa, ya, 3, c); gfx_ring(xb, yb, 3, c);
    }
    gfx_noclip();
}
static void cursor_band(int cx) {
    band(cx - 6, cx + 7);
    if (meta.playing || cx != page_px.x) {
        gfx_fill(cx, page_px.y, 1, page_px.h, ramp(R_GRAY, meta.playing ? 13 : 7));
        int t = (int)(meta.pos >> 16);
        const struct meta_string *s; int n = meta_strings(&s);
        for (int i = 0; i < n && meta.playing; i++)            /* where the cursor meets the strings sounding */
            if (t >= s[i].t0 && t <= s[i].t1)
                gfx_disc(cx, y_of(s[i].p0 + (s[i].p1 - s[i].p0) * (t - s[i].t0) / MAX(1, s[i].t1 - s[i].t0)), 2, ink(s[i].section, s[i].fam, 6));
    }
    gfx_dirty(cx - 6, page_px.y, 13, page_px.h);
}

/* ---- the pointer: a line drawn sets a guide; two fingers on the touchpad move it live ---- */
static void set_guide(int t0, int p0, int t1, int p1) {
    struct meta_line *g = guide ? &F()->b : &F()->a;
    *g = (struct meta_line){ (uint16_t)t0, (uint16_t)p0, (uint16_t)t1, (uint16_t)p1 };
    F()->on = true;
    meta_compile();
}
static bool live;
static void pointer(uint64_t now) {
    if (pad.f[0].on && pad.f[1].on) {                          /* two fingers: the guide's two ends */
        if (!live) { edit(now); live = true; }
        int t0 = pad.f[0].x * 65535 / 32767, t1 = pad.f[1].x * 65535 / 32767;
        int p0 = UPIC_LO + (32767 - pad.f[0].y) * (UPIC_HI - UPIC_LO) / 32767, p1 = UPIC_LO + (32767 - pad.f[1].y) * (UPIC_HI - UPIC_LO) / 32767;
        set_guide(t0, p0, t1, p1);
        return;
    }
    if (live && !pad.f[0].on && !pad.f[1].on) live = false;
    if (!page_px.w) return;
    bool in = ui_in(page_px, ptr.x, ptr.y);
    if (ptr.pressed && in) { drag_t0 = drag_t1 = t_of(ptr.x); drag_p0 = drag_p1 = p_of(ptr.y); }
    else if (drag_t0 >= 0 && ptr.down) { drag_t1 = t_of(ptr.x); drag_p1 = p_of(ptr.y); }
    else if (drag_t0 >= 0 && !ptr.down) {                      /* let go: the guide is that line (a click: a point) */
        edit(now);
        set_guide(drag_t0, drag_p0, drag_t1, drag_p1);
        guide ^= 1;                                            /* A, then B */
        drag_t0 = -1;
    }
}

/* ---- keys ---- */
static void turn(int k, int d, uint64_t now) {
    if (k != K_FAMILY && k != K_LENGTH) edit(now);
    switch (k) {
    case K_FAMILY:  fam = (fam + d + META_FAMILIES) % META_FAMILIES; break;
    case K_ON:      F()->on = !F()->on; break;
    case K_STRINGS: F()->n = (uint8_t)CLAMP(F()->n + d, 2, META_MAX); break;
    case K_CROSS:   F()->cross = !F()->cross; break;
    case K_MODULOR: F()->modulor = !F()->modulor; break;
    case K_SECTION: F()->section = (uint8_t)((F()->section + d + META_SECTIONS) % META_SECTIONS); break;
    case K_LEVEL:   F()->level = (uint8_t)CLAMP(F()->level + d * 5, 0, 100); break;
    case K_LENGTH:  set_length(length_index() + d); break;
    }
    meta_compile();
}
static void transpose(int d, uint64_t now) {
    edit(now);
    struct meta_line *g[2] = { &F()->a, &F()->b };
    for (int k = 0; k < 2; k++) {
        g[k]->p0 = (uint16_t)CLAMP(g[k]->p0 + d * 256, UPIC_LO, UPIC_HI); g[k]->p1 = (uint16_t)CLAMP(g[k]->p1 + d * 256, UPIC_LO, UPIC_HI);
    }
    meta_compile();
}
static bool key(uint8_t code, bool down, uint64_t now) {
    if (!down) return code == KEY_SPACE || code == KEY_HOME || code == KEY_TAB || code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT ||
                      code == KEY_ENTER || code == KEY_BACKSPACE || code == KEY_PGUP || code == KEY_PGDN || (code >= '1' && code <= '4') || code == 'c' || code == 'm' || code == 'o';
    switch (code) {
    case '1': case '2': case '3': case '4': fam = code - '1'; meta.changes++; return true;
    case KEY_SPACE: meta.playing = !meta.playing; return true;
    case KEY_HOME:  meta.pos = 0; return true;
    case KEY_TAB:   guide ^= 1; meta.changes++; return true;
    case KEY_UP:    knob = (knob + KNOBS - 1) % KNOBS; return true;
    case KEY_DOWN:  knob = (knob + 1) % KNOBS; return true;
    case KEY_LEFT:  turn(knob, -1, now); return true;
    case KEY_RIGHT: turn(knob, +1, now); return true;
    case KEY_PGUP:  transpose(+1, now); return true;
    case KEY_PGDN:  transpose(-1, now); return true;
    case 'c': turn(K_CROSS, 1, now); return true;
    case 'm': turn(K_MODULOR, 1, now); return true;
    case 'o': turn(K_ON, 1, now); return true;
    case KEY_ENTER: {                                          /* the family onto UPIC's page, as arcs */
        undo_one(U_UPIC, 0, "METASTASEIS onto UPIC", now);
        int k = meta_write_upic(fam);
        char m[48]; if (k < 0) snfmt(m, sizeof m, "no room on UPIC's page"); else snfmt(m, sizeof m, "%d strings onto UPIC's page", k);
        ui_notice(m, now);
        return true; }
    case KEY_BACKSPACE:                                        /* twice: the opening again */
        if (now - clear_ms < 2000) { edit(now); meta_defaults(); clear_ms = 0; ui_notice("the opening of Metastaseis again", now); }
        else { clear_ms = now; ui_notice("Backspace again: the families back to the opening", now); }
        return true;
    }
    return false;
}

static void draw(uint64_t now) {
    (void)now;
    if (harmony_on != harmony_seen || (harmony_on && omni_chord_word != chord_seen)) {   /* keys follow the chord: the ends land on it */
        harmony_seen = harmony_on; chord_seen = omni_chord_word; meta_compile();
    }
    int cols = text_cols(), rows = text_rows();
    int sw = 30, x = 2, y = XEN_TOP, lh = ui_lesson_rows(), ly = rows - 1 - lh, pw = cols - 4 - sw - 1, ph = ly - y;
    char t[96], len[16];
    if (meta.bars) snfmt(len, sizeof len, "%d bar%s", meta.bars, meta.bars > 1 ? "s" : ""); else snfmt(len, sizeof len, "%d s", meta.seconds);
    snfmt(t, sizeof t, "METASTASEIS · family %d · %d strings · %s", fam + 1, F()->on ? F()->n : 0, len);
    ui_panel(x, y, pw, ph, t, C_AMBER);
    for (int o = 0; o <= 7; o++) {                             /* the octaves, down the left */
        char lab[4]; snfmt(lab, sizeof lab, "C%d", 1 + o);
        text_str(x + 1, y + 1 + (ph - 3) - o * (ph - 3) / 7, lab, C_DIM, C_PANEL);
    }
    struct rect r;
    uint32_t key = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, cols << 16 | rows), (int32_t)meta.changes), fam | guide << 4);
    bool fresh = ui_canvas_keyed(&r, x + 4, y + 1, pw - 5, ph - 2, C_BG, key);
    page_px = r;
    int cx = x_of((int)(meta.pos >> 16));
    if (fresh) { band(r.x, r.x + r.w); shown_cursor = -1; }
    if (cx != shown_cursor || meta.playing) {
        if (shown_cursor >= 0 && shown_cursor != cx) band(shown_cursor - 6, shown_cursor + 7);
        cursor_band(cx);
        shown_cursor = cx;
    }
    if (drag_x0 >= 0) { band(drag_x0 - 4, drag_x1 + 5); drag_x0 = -1; }      /* the last preview gone, then the new one */
    if (drag_t0 >= 0) {
        int xa = x_of(drag_t0), xb = x_of(drag_t1);
        xen_line(xa, y_of(drag_p0), xb, y_of(drag_p1), C_BRIGHT);
        drag_x0 = MIN(xa, xb); drag_x1 = MAX(xa, xb);
    }
    ptr.shape = ui_in(r, ptr.x, ptr.y) ? PTR_CROSS : PTR_ARROW;
    /* the knobs */
    int kx = x + pw + 1, ry = y + 2, lim = y + ph - 1;
    ui_panel(kx, y, sw, ph, meta.playing ? "METASTASEIS · playing" : "METASTASEIS", C_CYAN);
    for (int i = 0; i < KNOBS && ry < lim; i++, ry++) {
        char v[32];
        switch (i) {
        case K_FAMILY:  snfmt(v, sizeof v, "%d of %d", fam + 1, META_FAMILIES); break;
        case K_ON:      snfmt(v, sizeof v, "%s", F()->on ? "on" : "off"); break;
        case K_STRINGS: snfmt(v, sizeof v, "%d", F()->n); break;
        case K_CROSS:   snfmt(v, sizeof v, "%s", F()->cross ? "crossed" : "straight"); break;
        case K_MODULOR: snfmt(v, sizeof v, "%s", F()->modulor ? "golden section" : "even"); break;
        case K_SECTION: snfmt(v, sizeof v, "%s", meta_section_names[F()->section % META_SECTIONS]); break;
        case K_LEVEL:   snfmt(v, sizeof v, "%d", F()->level); break;
        default:        snfmt(v, sizeof v, "%s", len); break;
        }
        bool on = i == knob;
        text_put(kx + 1, ry, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(kx + 3, ry, knob_names[i], on ? C_BRIGHT : C_TEXT, C_PANEL);
        text_str_n(kx + 12, ry, v, sw - 14, on ? C_AMBER : C_DIM, C_PANEL);
    }
    ry++;
    if (ry < lim) { text_str(kx + 3, ry, "guide", C_TEXT, C_PANEL); text_str(kx + 12, ry, guide ? "B" : "A", C_BLACK, C_AMBER); text_str(kx + 14, ry++, "the next line", C_DIM, C_PANEL); }
    if (harmony_on && ry < lim) text_str_n(kx + 3, ry++, "ends land on the chord", sw - 5, C_GREEN, C_PANEL);
    ry++;
    static const char *const tries[] = { "a point, a line: a fan", "two lines: a surface", "crossed: a curve appears", "two fingers: bend it live",
                                         "Enter: onto UPIC's page" };
    for (int i = 0; i < (int)ARRAY_LEN(tries) && ry < lim; i++, ry++) {
        if (i == 0) { text_str(kx + 3, ry++, "try", C_GREEN, C_PANEL); if (ry >= lim) break; }
        text_str_n(kx + 3, ry, tries[i], sw - 5, C_DIM, C_PANEL);
    }
    ui_lesson(x, ly, cols - 4, lh, &lesson_meta);
    FOOTER("SPACE", "play", "HOME", "start", "1-4", "family", "TAB", "guide A / B", "↑ ↓ ← →", "knobs", "C", "crossed", "M", "modulor", "O", "on",
           "PGUP PGDN", "transpose", "ENTER", "onto UPIC", "BKSP BKSP", "the opening", "", "drag a line: a guide; two fingers move it");
}

const struct view xen_meta = { "METASTASEIS", key, 0, pointer, draw, 0, "1954" };
