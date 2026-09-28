/* MERZBOW, a LINEAGE view (after Merzbow, Masami Akita): noise from junk (core/junk.c). Fingers on the touchpad (or the
   mouse) scrape; the letter rows strike junk; Space feeds it back into itself; Enter plays this program's own bytes.
   The picture: a Merzbau — Schwitters' room built of found things — as a collage of shards that light with each
   source, and under it the wall, the noise's last seconds as a jagged strip. */
#include "lineage.h"
#include "junk.h"
#include "lessons.h"
#include "gfx.h"
#include "keys.h"

enum { K_DRIVE, K_BITS, K_CHOP, K_FEEDBACK, K_GRAIN, K_BYTES, K_LEVEL, KNOBS };
static const char *const knob_names[KNOBS] = { "drive", "bits", "chop", "feedback", "grain", "bytes", "level" };
static const char rows_[3][13] = { "zxcvbnm,./", "asdfghjkl;'", "qwertyuiop[]" };
static int knob;
static bool held[JUNK_OBJECTS], latched;
static struct rect pic_px;

static int object_of(uint8_t code) {
    for (int r = 0, k = 0; r < 3; r++) for (int i = 0; rows_[r][i]; i++, k++) if (code == (uint8_t)rows_[r][i]) return k;
    return -1;
}
static uint8_t *knob_val(int k) {
    switch (k) { case K_DRIVE: return &junk.drive; case K_BITS: return &junk.bits; case K_CHOP: return &junk.chop; case K_FEEDBACK: return &junk.feedback;
                 case K_GRAIN: return &junk.grain; case K_BYTES: return &junk.bytes_rate; }
    return &junk.level;
}
static bool key(uint8_t code, bool down, uint64_t now) {
    (void)now;
    int k = object_of(code);
    if (k >= 0) { if (down && !held[k]) junk_strike(k, 110); held[k] = down; return true; }
    if (code == KEY_SPACE) { if (!latched || down) junk.feedback_on = down; if (down && ui_shift) latched = true; else if (down) latched = false; return true; }   /* held; ⇧ latches */
    if (!down) return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_ENTER || code == KEY_BACKSPACE;
    switch (code) {
    case KEY_UP:    knob = (knob + KNOBS - 1) % KNOBS; return true;
    case KEY_DOWN:  knob = (knob + 1) % KNOBS; return true;
    case KEY_LEFT: case KEY_RIGHT: {
        int d = code == KEY_RIGHT ? 1 : -1; uint8_t *v = knob_val(knob);
        *v = (uint8_t)(knob == K_BITS ? CLAMP(*v + d, 1, 16) : CLAMP(*v + d * 5, 0, 100));
        return true; }
    case KEY_ENTER: junk.bytes_on = !junk.bytes_on; return true;
    case KEY_BACKSPACE: junk_all_off(); latched = false; return true;
    }
    return false;
}
/* every finger scrapes, and the mouse */
static void scratch_add(int who, int x, int y, bool joined);
static bool was_on[JUNK_SCRAPERS];
static void pointer(uint64_t now) {
    (void)now;
    for (int k = 0; k <= FINGERS; k++) {
        bool on; int x, y, z;
        if (k < FINGERS) { on = pad.f[k].on; x = pad.f[k].x; y = pad.f[k].y; z = pad.f[k].z; }
        else {                                                        /* the mouse, dragged over the picture */
            on = ptr.down && ui_in(pic_px, ptr.x, ptr.y); z = 90;
            x = CLAMP((ptr.x - pic_px.x) * 32767 / MAX(1, pic_px.w - 1), 0, 32767); y = CLAMP((ptr.y - pic_px.y) * 32767 / MAX(1, pic_px.h - 1), 0, 32767);
        }
        junk_scrape(k, CLAMP(x, 0, 32767), CLAMP(y, 0, 32767), z, on);
        if (on) scratch_add(k, CLAMP(x, 0, 32767), CLAMP(y, 0, 32767), was_on[k]);
        was_on[k] = on;
    }
}

/* ---- the picture ---- */
/* the Merzbau: shards in fixed places (the same room every time), each belonging to a source; their fills stay and
   their edges light with it, so a change writes edges only (a slow framebuffer's pixels are what a picture costs) */
static const uint8_t inks[JUNK_SOURCES] = { R_AMBER, R_GRAY, R_RED, R_GREEN };
static int lit[JUNK_SOURCES], lit_scratches; static uint64_t lit_at;
/* the scratches: where fingers (and the mouse) scraped lately, in the picture's pixels; a stroke per finger */
#define SCRATCHES 96
static struct { int16_t x, y; uint8_t who, joined; } scratch[SCRATCHES]; static int scratch_n;
static void scratch_add(int who, int x, int y, bool joined) {
    if (!pic_px.w) return;
    int i = scratch_n++ % SCRATCHES;
    scratch[i].x = (int16_t)(pic_px.x + x * (pic_px.w - 1) / 32767); scratch[i].y = (int16_t)(pic_px.y + y * (pic_px.h - 1) / 32767);
    scratch[i].who = (uint8_t)who; scratch[i].joined = joined;
}
static void collage(struct rect r) {
    uint32_t s = 0x4D455242u;
#define RND() (s ^= s << 13, s ^= s >> 17, s ^= s << 5, s)
    gfx_clip(r.x, r.y, r.w, r.h);
    for (int i = 0; i < 52; i++) {
        int src = (int)(RND() % JUNK_SOURCES), w = r.w / 14 + (int)(RND() % (uint32_t)MAX(1, r.w / 4)), h = r.h / 12 + (int)(RND() % (uint32_t)MAX(1, r.h / 3));
        int x = r.x - w / 4 + (int)(RND() % (uint32_t)MAX(1, r.w)), y = r.y - h / 4 + (int)(RND() % (uint32_t)MAX(1, r.h));
        uint8_t edge = ramp(inks[src], 3 + lit[src]);
        gfx_fill(x, y, w, h, ramp(inks[src], 1 + (int)(RND() % 3)));
        gfx_frame(x, y, w, h, edge);
        if (RND() % 3 == 0) gfx_line(x, y + h - 1, x + w - 1, y, edge);                     /* the Merzbau's angles */
        else if (RND() % 2) gfx_line(x, y, x + w - 1, y + h - 1, edge);
    }
    for (int i = 0; i < 7; i++) {                                                          /* wire strung across it all */
        int y0 = r.y + (int)(RND() % (uint32_t)r.h), y1 = r.y + (int)(RND() % (uint32_t)r.h);
        gfx_line(r.x, y0, r.x + r.w - 1, y1, ramp(R_GRAY, 5 + lit[JS_SCRAPE] / 2));
    }
    int n = MIN(scratch_n, SCRATCHES);                                                     /* the scratches, the newest brightest */
    for (int k = 1; k < n; k++) {
        int i = (scratch_n - n + k) % SCRATCHES, j = (i + SCRATCHES - 1) % SCRATCHES;
        if (!scratch[i].joined || scratch[i].who != scratch[j].who) continue;
        uint8_t c = ramp(R_AMBER, 6 + k * 9 / n);
        gfx_line(scratch[j].x, scratch[j].y, scratch[i].x, scratch[i].y, c); gfx_line(scratch[j].x, scratch[j].y + 1, scratch[i].x, scratch[i].y + 1, c);
    }
    gfx_noclip();
#undef RND
}
/* the wall: the noise's peaks, a column a frame, written at a sweeping head (only the head's columns change); each
   column a speckle as rough as what sounded */
static int wall_x; static uint32_t wall_seed = 0x57414C4Cu;
static void wall(struct rect r, bool fresh) {
    if (fresh) wall_x = 0;
    int v = junk.peak; junk.peak = 0;
    int h = MIN(v, 22000) * (r.h - 2) / 22000, x = r.x + wall_x, y0 = r.y + (r.h - h) / 2;
    gfx_fill(x, r.y, MIN(6, r.x + r.w - x), r.h, C_BG);                                  /* where it goes next */
    uint8_t ink = v > 16000 ? R_RED : v > 8000 ? R_AMBER : R_GRAY;
    for (int y = 0; y < h; y++) {
        wall_seed ^= wall_seed << 13; wall_seed ^= wall_seed >> 17; wall_seed ^= wall_seed << 5;
        int edge = MIN(y, h - 1 - y) * 16 / MAX(1, h);                                 /* brighter towards the middle */
        gfx_pixel(x, y0 + y, ramp(ink, CLAMP(4 + edge + (int)(wall_seed >> 29), 1, 15)));
    }
    wall_x = (wall_x + 1) % MAX(1, r.w);
    gfx_vline(r.x + wall_x, r.y, r.h, ramp(R_GRAY, 5));                                  /* the head */
}

static void draw(uint64_t now) {
    struct lin_layout L; lineage_layout(&L, 30);
    char t[64]; snfmt(t, sizeof t, "MERZBOW · junk%s%s", junk.feedback_on ? " · feedback" : "", junk.bytes_on ? " · bytes" : "");
    ui_panel(L.x, L.y, L.pw, L.ph, t, C_RED);
    int wh = MAX(2, (L.ph - 2) / 4), ch = L.ph - 2 - wh;
    if (now - lit_at >= 50) {                                        /* the shards' light: 20 times a second at most */
        lit_at = now;
        lit_scratches = scratch_n;
        for (int i = 0; i < JUNK_SOURCES; i++) { int a = junk.activity[i]; lit[i] = a > 16384 ? 12 : a > 4096 ? 8 + (a - 4096) / 3072 : a > 256 ? 3 + (a - 256) / 768 : a > 16; }
    }
    struct rect r;
    uint32_t hk = ui_hash_int(UI_HASH0, text_cols() << 16 | text_rows());
    for (int i = 0; i < JUNK_SOURCES; i++) hk = ui_hash_int(hk, lit[i]);
    hk = ui_hash_int(hk, lit_scratches);
    if (ui_canvas_keyed(&r, L.x + 1, L.y + 1, L.pw - 2, ch, C_BG, hk)) collage(r);
    pic_px = r;
    bool fresh = ui_canvas_keyed(&r, L.x + 1, L.y + 1 + ch, L.pw - 2, wh, C_BG, ui_hash_int(UI_HASH0, text_cols() << 16 | text_rows()));
    wall(r, fresh);
    ptr.shape = ui_in(pic_px, ptr.x, ptr.y) ? PTR_CROSS : PTR_ARROW;
    ui_panel(L.kx, L.y, L.kw, L.ph, "JUNK", C_CYAN);
    int ry = L.y + 2, lim = L.y + L.ph - 1;
    for (int i = 0; i < KNOBS && ry < lim; i++, ry++) {
        char v[16]; snfmt(v, sizeof v, "%d", *knob_val(i));
        lineage_knob(L.kx + 1, ry, L.kw - 2, knob_names[i], v, i == knob);
    }
    ry++;
    static const char *const src[JUNK_SOURCES] = { "scrape: fingers, mouse", "metal: the letter keys", "feedback: SPACE", "bytes: ENTER" };
    static const uint8_t led[JUNK_SOURCES] = { C_AMBER, C_TEXT, C_RED, C_GREEN };
    for (int i = 0; i < JUNK_SOURCES && ry < lim; i++, ry++)
        ui_led(L.kx + 2, ry, lit[i] > 2 || (i == JS_FEEDBACK && junk.feedback_on) || (i == JS_BYTES && junk.bytes_on), led[i], src[i], C_PANEL);
    if (++ry < lim) text_str_n(L.kx + 2, ry, "a cap holds it near -12 dB", L.kw - 4, C_DIM, C_PANEL);
    ui_lesson(L.x, L.ly, text_cols() - 4, L.lh, &lesson_merzbow);
    FOOTER("Z-/ A-' Q-]", "strike junk", "SPACE", "feedback (⇧ holds)", "ENTER", "the bytes", "↑ ↓ ← →", "knobs", "BKSP", "all off",
           "", "fingers on the touchpad scrape");
}

const struct view lin_merzbow = { "MERZBOW", key, 0, pointer, draw, 0, "1979" };
