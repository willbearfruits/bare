/* UPIC (XENAKIS, a view): the page of core/upic.c. Draw arcs with every finger on the touchpad or the mouse (PEN: a
   line becomes an arc when it is let go; a click, a short note), straight glissandi with LINE, rub arcs out with
   ERASE (or the right button), and play the page: Space runs the cursor, SCRUB lets the hand hold it — moving slowly,
   still, or backwards. Arcs take the sound and level chosen when drawn; SNAP puts them on semitones or a sieve's steps
   (a sloping line then climbs as a run). PgUp/PgDn transpose the page, R plays it backwards (retrograde), I turns it
   upside down (inversion); CLOUDS can write a cloud onto it (Enter there). The picture is redrawn where the cursor was and
   is, and whole only when the page changes. */
#include "xen.h"
#include "upic.h"
#include "sieve.h"
#include "synth.h"
#include "gfx.h"
#include "keys.h"
#include "undo.h"

enum { T_PEN, T_LINE, T_ERASE, T_SCRUB, TOOLS };
static const char *const tool_names[TOOLS] = { "PEN", "LINE", "ERASE", "SCRUB" };
enum { K_LENGTH, K_SOUND, K_LEVEL, K_SNAP, KNOBS };
static const char *const knob_names[KNOBS] = { "length", "sound", "level", "snap" };
static const uint8_t lengths[] = { 1, 2, 4, 8, 16, 32 };                 /* bars; then seconds */
static const uint8_t secs[] = { 2, 4, 8, 16, 30, 60, 120 };
static int tool, knob = K_SOUND, sound = P_GD1, level = 100, snap;       /* snap: 0 free, 1 semitones, 2.. sieve S1.. */
static struct rect page_px;
static uint64_t clear_ms;
static int shown_cursor = -1;
#define STROKE 255
static struct stroke { int n; struct upic_pt p[STROKE]; } strokes[FINGERS + 1];   /* a finger's (or the mouse's) line being drawn */
static int line_t0 = -1, line_p0, line_t1, line_p1, line_x0 = -1, line_x1;   /* the line tool; the preview's span on screen */
static bool erasing;

static int length_index(void) {
    if (upic.bars) { for (int i = 0; i < 6; i++) if (lengths[i] >= upic.bars) return i; return 5; }
    for (int i = 0; i < 7; i++) if (secs[i] >= upic.seconds) return 6 + i;
    return 12;
}
static void set_length(int i) {
    i = CLAMP(i, 0, 12);
    if (i < 6) upic.bars = lengths[i]; else { upic.bars = 0; upic.seconds = secs[i - 6]; }
}
static int32_t snapped(int32_t p) {
    if (snap == 1) return (p + 128) & ~255;
    if (snap >= 2) return sieve_snap_pitch(&sieves[snap - 2], p);
    return p;
}

/* ---- the page's geometry ---- */
static int t_of(int x) { return CLAMP((x - page_px.x) * 65535 / MAX(1, page_px.w - 1), 0, 65535); }
static int p_of(int y) { return CLAMP(UPIC_LO + (page_px.y + page_px.h - 1 - y) * (UPIC_HI - UPIC_LO) / MAX(1, page_px.h - 1), UPIC_LO, UPIC_HI); }
static int x_of(int t) { return page_px.x + (int)((uint32_t)t * (uint32_t)(page_px.w - 1) / 65535); }
static int y_of(int p) { return page_px.y + page_px.h - 1 - (CLAMP(p, UPIC_LO, UPIC_HI) - UPIC_LO) * (page_px.h - 1) / (UPIC_HI - UPIC_LO); }
static uint8_t arc_colour(int sound, int lvl) {
    static const uint8_t r[6] = { R_AMBER, R_CYAN, R_GREEN, R_PINK, R_BLUE, R_RED };
    return ramp(r[sound % 6], 7 + CLAMP(lvl, 1, 127) * 8 / 127);
}

/* ---- strokes ---- */
static void stroke_point(struct stroke *s, int t, int p) {
    p = snapped(p);
    if (s->n && t <= s->p[s->n - 1].t + 96) {                 /* time only runs forward; points no closer than 1/680 page */
        if (!snap && t > s->p[s->n - 1].t - 8) s->p[s->n - 1].p = (uint16_t)p;
        return;
    }
    if (snap && s->n && s->p[s->n - 1].p != p && s->n < STROKE - 1) {   /* a step: held to just before, then the new note */
        uint16_t held = s->p[s->n - 1].p;
        s->p[s->n] = (struct upic_pt){ (uint16_t)(t - 1), held }; s->n++;
    }
    if (s->n < STROKE) s->p[s->n++] = (struct upic_pt){ (uint16_t)t, (uint16_t)p };
}
static void stroke_commit(struct stroke *s, uint64_t now) {
    if (s->n == 1) s->p[s->n++] = (struct upic_pt){ (uint16_t)MIN(65535, s->p[0].t + 512), s->p[0].p };   /* a click: a short note */
    if (s->n >= 2) {
        undo_one(U_UPIC, 0, "an arc", now);
        if (upic_add(s->p, s->n, (uint8_t)sound, (uint8_t)level) < 0) ui_notice("the page is full", now);
    }
    s->n = 0;
}
static void stroke_draw(const struct stroke *s) {             /* its newest piece, over the page */
    if (s->n >= 2) xen_line(x_of(s->p[s->n - 2].t), y_of(s->p[s->n - 2].p), x_of(s->p[s->n - 1].t), y_of(s->p[s->n - 1].p), ramp(R_GRAY, 15));
    else if (s->n == 1) gfx_fill(x_of(s->p[0].t) - 1, y_of(s->p[0].p) - 1, 3, 3, ramp(R_GRAY, 15));
}

/* the arc passing within a few pixels of (x, y), or -1 */
static int arc_near(int x, int y) {
    for (int a = upic.narcs - 1; a >= 0; a--) {
        const struct upic_arc *arc = &upic.d.arc[a];
        const struct upic_pt *p = upic.d.pt + arc->first;
        for (int i = 0; i + 1 < arc->n; i++) {
            int x0 = x_of(p[i].t), x1 = x_of(p[i + 1].t), y0 = y_of(p[i].p), y1 = y_of(p[i + 1].p);
            if (x < MIN(x0, x1) - 5 || x > MAX(x0, x1) + 5 || y < MIN(y0, y1) - 5 || y > MAX(y0, y1) + 5) continue;
            int yl = x1 == x0 ? y : y0 + (y1 - y0) * (x - x0) / (x1 - x0);
            if ((yl > y ? yl - y : y - yl) <= 5 || x1 - x0 <= 2) return a;
        }
    }
    return -1;
}

static bool key(uint8_t code, bool down, uint64_t now) {
    if (!down) return code == KEY_SPACE || code == KEY_HOME || code == KEY_TAB || code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT ||
                      code == KEY_RIGHT || code == KEY_PGUP || code == KEY_PGDN || code == KEY_BACKSPACE || code == 'r' || code == 'i';
    switch (code) {
    case KEY_SPACE: upic.playing = !upic.playing; return true;
    case KEY_HOME:  upic.seek = 1; return true;
    case KEY_TAB:   tool = (tool + 1) % TOOLS; line_t0 = -1; return true;
    case KEY_UP:    knob = (knob + KNOBS - 1) % KNOBS; return true;
    case KEY_DOWN:  knob = (knob + 1) % KNOBS; return true;
    case KEY_LEFT: case KEY_RIGHT: {
        int d = code == KEY_RIGHT ? 1 : -1;
        switch (knob) {
        case K_LENGTH: set_length(length_index() + d); break;
        case K_SOUND:  sound = synth_preset_next(sound, d); break;
        case K_LEVEL:  level = CLAMP(level + d * 8, 8, 127); break;
        case K_SNAP:   snap = (snap + d + 6) % 6; break;
        }
        return true; }
    case KEY_PGUP: case KEY_PGDN:
        undo_one(U_UPIC, 0, "transposing the page", now); upic_transpose(code == KEY_PGUP ? 256 : -256); return true;
    case 'r': undo_one(U_UPIC, 0, "the page backwards", now); upic_mirror_time(); return true;
    case 'i': undo_one(U_UPIC, 0, "the page upside down", now); upic_mirror_pitch(); return true;
    case KEY_BACKSPACE:                                        /* twice to clear */
        if (now - clear_ms < 2000) { undo_one(U_UPIC, 0, "clearing the page", now); upic_clear(); clear_ms = 0; ui_notice("page cleared", now); }
        else { clear_ms = now; ui_notice("Backspace again to clear the page", now); }
        return true;
    }
    return false;
}

static void pointer(uint64_t now) {
    if (!page_px.w) return;
    /* the touchpad: every finger a pen, the pad standing for the page */
    for (int k = 0; k < FINGERS; k++) {
        const struct finger *f = &pad.f[k];
        if (f->on) stroke_point(&strokes[k], f->x * 65535 / 32767, UPIC_HI - (UPIC_HI - UPIC_LO) * f->y / 32767);
        else if (strokes[k].n) stroke_commit(&strokes[k], now);
    }
    bool in = ui_in(page_px, ptr.x, ptr.y);
    struct stroke *m = &strokes[FINGERS];
    if ((ptr.buttons & 2) || (tool == T_ERASE && ptr.down)) {  /* rubbing out */
        if (!erasing) undo_one(U_UPIC, 0, "rubbing out arcs", now);
        erasing = true;
        int a = in ? arc_near(ptr.x, ptr.y) : -1;
        if (a >= 0) upic_delete(a);
        return;
    }
    erasing = false;
    switch (tool) {
    case T_PEN:
        if (ptr.down && (in || m->n)) stroke_point(m, t_of(ptr.x), p_of(ptr.y));
        else if (!ptr.down && m->n) stroke_commit(m, now);
        break;
    case T_LINE:
        if (ptr.pressed && in) { line_t0 = line_t1 = t_of(ptr.x); line_p0 = line_p1 = snapped(p_of(ptr.y)); }
        else if (line_t0 >= 0 && ptr.down) { line_t1 = t_of(ptr.x); line_p1 = snapped(p_of(ptr.y)); }
        else if (line_t0 >= 0) {                               /* let go: a straight glissando */
            struct upic_pt p[2] = { { (uint16_t)MIN(line_t0, line_t1), (uint16_t)(line_t0 <= line_t1 ? line_p0 : line_p1) },
                                    { (uint16_t)MAX(line_t0, line_t1), (uint16_t)(line_t0 <= line_t1 ? line_p1 : line_p0) } };
            if (p[1].t - p[0].t < 256) p[1].t = (uint16_t)MIN(65535, p[0].t + 256);
            undo_one(U_UPIC, 0, "a line", now);
            if (upic_add(p, 2, (uint8_t)sound, (uint8_t)level) < 0) ui_notice("the page is full", now);
            line_t0 = -1;
        }
        break;
    case T_SCRUB:
        upic.scrub = ptr.down && (in || upic.scrub >= 0) ? t_of(ptr.x) : -1;
        upic.scrub_ms = now;
        break;
    }
}

/* ---- the picture ---- */
static void grid_and_arcs(int x0, int x1) {                  /* a band of the page drawn again: clear, grid, arcs through it */
    struct rect r = page_px;
    x0 = MAX(x0, r.x); x1 = MIN(x1, r.x + r.w);
    if (x1 <= x0) return;
    gfx_clip(x0, r.y, x1 - x0, r.h);
    gfx_fill(x0, r.y, x1 - x0, r.h, C_BG);
    for (int n = UPIC_LO; n <= UPIC_HI; n += 12 * 256) { int y = y_of(n); for (int x = x0 + ((4 - x0 % 4) % 4); x < x1; x += 4) gfx_fill(x, y, 1, 1, ramp(R_PANEL, 6)); }
    if (upic.bars) {                                           /* the bars */
        for (int b = 1; b < upic.bars; b++) { int x = page_px.x + b * (page_px.w - 1) / upic.bars; if (x >= x0 && x < x1) for (int y = r.y; y < r.y + r.h; y += 3) gfx_fill(x, y, 1, 1, ramp(R_PANEL, 5)); }
    }
    for (int a = 0; a < upic.narcs; a++) {
        const struct upic_arc *arc = &upic.d.arc[a];
        const struct upic_pt *p = upic.d.pt + arc->first;
        if (arc->n < 2 || x_of(p[arc->n - 1].t) < x0 - 1 || x_of(p[0].t) > x1) continue;
        uint8_t c = arc_colour(arc->sound, arc->level);
        for (int i = 0; i + 1 < arc->n; i++) {
            int xa = x_of(p[i].t), xb = x_of(p[i + 1].t);
            if (xb < x0 - 1 || xa > x1) continue;
            xen_line(xa, y_of(p[i].p), xb, y_of(p[i + 1].p), c);
        }
    }
    gfx_noclip();
}
static void cursor_band(int cx) {
    grid_and_arcs(cx - 6, cx + 7);
    if (upic.playing || upic.scrub >= 0 || cx != page_px.x) {
        gfx_fill(cx, page_px.y, 1, page_px.h, ramp(R_GRAY, upic.playing || upic.scrub >= 0 ? 13 : 7));
        uint16_t t = (uint16_t)(upic.pos >> 16);
        for (int a = 0; a < upic.narcs && (upic.playing || upic.scrub >= 0); a++) {   /* where the cursor meets the arcs sounding */
            const struct upic_arc *arc = &upic.d.arc[a];
            const struct upic_pt *p = upic.d.pt + arc->first;
            if (arc->n < 2 || t < p[0].t || t > p[arc->n - 1].t) continue;
            int k = 0; while (k < arc->n - 2 && t >= p[k + 1].t) k++;
            int dt = MAX(1, p[k + 1].t - p[k].t), pitch = p[k].p + (p[k + 1].p - p[k].p) * (t - p[k].t) / dt;
            gfx_disc(cx, y_of(pitch), 3, ramp(R_GRAY, 15));
        }
    }
    gfx_dirty(cx - 6, page_px.y, 13, page_px.h);
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    int sw = 30, x = 2, y = 2, pw = cols - 4 - sw - 1, ph = rows - y - 3;
    char t[80];
    if (upic.bars) snfmt(t, sizeof t, "UPIC · %d arc%s · %d bar%s", upic.narcs, upic.narcs == 1 ? "" : "s", upic.bars, upic.bars > 1 ? "s" : "");
    else snfmt(t, sizeof t, "UPIC · %d arc%s · %d s", upic.narcs, upic.narcs == 1 ? "" : "s", upic.seconds);
    ui_panel(x, y, pw, ph, t, C_AMBER);
    for (int o = 0; o <= 7; o++) {                             /* the octaves, down the left */
        char lab[4]; snfmt(lab, sizeof lab, "C%d", 1 + o);
        int ry = y + 1 + (ph - 3) - o * (ph - 3) / 7;
        text_str(x + 1, ry, lab, C_DIM, C_PANEL);
    }
    struct rect r;
    uint32_t key = ui_hash_int(ui_hash_int(UI_HASH0, cols << 16 | rows), (int32_t)upic.changes);
    key = ui_hash_int(key, upic.bars);
    bool fresh = ui_canvas_keyed(&r, x + 4, y + 1, pw - 5, ph - 2, C_BG, key);
    page_px = r;
    int cx = x_of((int)(upic.pos >> 16));
    if (fresh) { grid_and_arcs(r.x, r.x + r.w); shown_cursor = -1; }
    if (cx != shown_cursor || upic.playing || upic.scrub >= 0) {
        if (shown_cursor >= 0 && shown_cursor != cx) grid_and_arcs(shown_cursor - 6, shown_cursor + 7);
        cursor_band(cx);
        shown_cursor = cx;
    }
    for (int k = 0; k <= FINGERS; k++) stroke_draw(&strokes[k]);    /* lines being drawn, over the page */
    if (line_x0 >= 0) { grid_and_arcs(line_x0 - 1, line_x1 + 2); line_x0 = -1; }     /* the last preview gone, then the new one */
    if (line_t0 >= 0) {
        int xa = x_of(line_t0), xb = x_of(line_t1);
        xen_line(xa, y_of(line_p0), xb, y_of(line_p1), ramp(R_GRAY, 15));
        line_x0 = MIN(xa, xb); line_x1 = MAX(xa, xb);
    }
    ptr.shape = ui_in(r, ptr.x, ptr.y) ? PTR_CROSS : PTR_ARROW;

    int kx = x + pw + 1, ry = y + 2, lim = y + ph - 1;
    ui_panel(kx, y, sw, ph, upic.playing ? "UPIC · playing" : upic.scrub >= 0 ? "UPIC · by hand" : "UPIC", C_CYAN);
    for (int i = 0; i < KNOBS && ry < lim; i++, ry++) {
        char v[32];
        switch (i) {
        case K_LENGTH: if (upic.bars) snfmt(v, sizeof v, "%d bar%s", upic.bars, upic.bars > 1 ? "s" : ""); else snfmt(v, sizeof v, "%d s", upic.seconds); break;
        case K_SOUND:  snfmt(v, sizeof v, "%s", synth_preset_name(sound)); break;
        case K_LEVEL:  snfmt(v, sizeof v, "%d", level); break;
        default:       snfmt(v, sizeof v, "%s", snap == 0 ? "free" : snap == 1 ? "semitones" : (const char *[]){ "S1", "S2", "S3", "S4" }[snap - 2]); break;
        }
        bool on = i == knob;
        text_put(kx + 1, ry, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(kx + 3, ry, knob_names[i], on ? C_BRIGHT : C_TEXT, C_PANEL);
        text_str_n(kx + 11, ry, v, sw - 13, on ? C_AMBER : C_DIM, C_PANEL);
    }
    ry++;
    if (ry < lim) {
        text_str(kx + 3, ry, "tool", C_TEXT, C_PANEL);
        for (int i = 0, tx = kx + 11; i < TOOLS; i++) {
            if (tx + ui_cells(tool_names[i]) > kx + sw - 1) { ry++; tx = kx + 11; if (ry >= lim) break; }
            text_str(tx, ry, tool_names[i], i == tool ? C_BLACK : C_DIM, i == tool ? C_AMBER : C_PANEL); tx += ui_cells(tool_names[i]) + 1;
        }
        ry += 2;
    }
    char info[40]; snfmt(info, sizeof info, "%d of %d points", upic.npts, UPIC_POINTS);
    if (ry < lim) text_str_n(kx + 3, ry++, info, sw - 5, C_DIM, C_PANEL);
    ry++;
    static const char *const tries[] = { "a line up: a glissando", "a click: a short note", "every finger at once", "snap to a sieve: a run",
                                         "R backwards, I inverted", "SCRUB: the hand plays" };
    for (int i = 0; i < (int)ARRAY_LEN(tries) && ry < lim; i++, ry++) {
        if (i == 0) { text_str(kx + 3, ry++, "try", C_GREEN, C_PANEL); if (ry >= lim) break; }
        text_str_n(kx + 3, ry, tries[i], sw - 5, C_DIM, C_PANEL);
    }
    FOOTER("SPACE", "play", "HOME", "start", "TAB", "pen / line / erase / scrub", "↑ ↓ ← →", "knobs", "PGUP PGDN", "transpose",
           "R", "backwards", "I", "upside down", "BKSP BKSP", "clear", "", "every finger draws; the right button rubs out");
}

const struct xen_view xen_upic = { "UPIC", key, 0, pointer, draw, 0 };
