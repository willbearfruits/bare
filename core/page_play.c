/* PLAY: the omnichord, drawn in lines and laid out like the keyboard under it. Chord buttons on top (the number row;
   Q W E pick major, minor, 7th), the sonic strings below (the letter rows, or a finger across the touchpad), a display
   with the chord and the scope, and the rhythm section: drum patterns, a bass that follows the chord, level knobs.
   F1 again steps to the instruments from files (core/inst.c): their pages (core/page_inst.c) take PLAY's place, and
   the tab takes their name. */
#include "ui.h"
#include "gfx.h"
#include "omni.h"
#include "synth.h"
#include "rhythm.h"
#include "sieve.h"
#include "seq.h"
#include "tables.h"
#include "keys.h"
#include "inst.h"

#define STRINGS (2 * OMNI_ZONES - 1)                   /* the low row's 10 zones, then the high row's 11 */
#define RING_MS 700                                    /* how long a plucked string shows its vibration */

/* geometry shared by drawing and the pointer, in pixels */
static struct rect chord_px[3][OMNI_ROOTS], pattern_px[RHYTHM_PATTERNS], knob_px[3], start_px, bass_px, plate_px;
static int chord_held = -1, knob_drag = -1, drag_y0, drag_v0;
/* the string under each finger on the touchpad and, last, under the mouse; a string lets go when its last one leaves */
static int8_t string_hit[FINGERS + 1] = { -1, -1, -1, -1, -1, -1 };
static uint8_t string_holders[STRINGS];

/* which one shows: 0 the omnichord, then the instruments */
static int showing;
static char tab[12] = "PLAY";
static int shown_inst(void) { if (showing > inst_count) showing = 0; return showing - 1; }
void play_show(int k) {
    showing = CLAMP(k, 0, inst_count);
    int i = shown_inst();
    inst_select(i);
    snfmt(tab, sizeof tab, "%s", i >= 0 ? insts[i].name : "PLAY");
}
static void again(uint64_t now) { (void)now; if (inst_count) play_show((showing + 1) % (inst_count + 1)); }
static bool midi(uint8_t n, uint8_t vel, uint64_t now) { int i = shown_inst(); return i >= 0 && inst_page_midi(i, n, vel, now); }
static int strum_sound(void) { int i = shown_inst(); return i >= 0 ? P_INST1 + i : -1; }

static void string_zone(int s, int *row, int *zone) { *row = s < OMNI_ZONES - 1 ? 0 : 1; *zone = *row ? s - (OMNI_ZONES - 1) : s; }
static uint64_t string_age(int s, uint64_t now) {
    int r, z; string_zone(s, &r, &z);
    return omni.zone_hit_ms[r][z] ? now - omni.zone_hit_ms[r][z] : 100000;
}
static uint8_t *knob_val(int i) { return i == 0 ? &omni.pad_level : i == 1 ? &omni.strum_level : &rhythm.level; }
static const char *const knob_names[3] = { "chord", "strings", "rhythm" };

/* ---- layout, in cells: the body fills the page; buttons and display on top, strings, then the rhythm section ---- */
static struct { int x, y, w, h, bw, bh, gx, gy, dx, dy, dw, dh, px, py, pw, ph, ry, pbw, kx; } L;

static void layout(void) {
    int cols = text_cols(), rows = text_rows();
    L.x = 1; L.y = 1; L.w = cols - 2; L.h = rows - 3;
    L.bh = rows >= 60 ? 3 : rows >= 44 ? 2 : 1;
    L.bw = CLAMP((L.w - 6 - 3 - 30) / OMNI_ROOTS, 5, 9);
    L.gx = L.x + 6; L.gy = L.y + 3;
    L.dx = L.gx + OMNI_ROOTS * L.bw + 2; L.dy = L.y + 1; L.dw = L.x + L.w - 2 - L.dx; L.dh = 2 + 3 * L.bh;
    L.ry = L.y + L.h - 5;                                       /* three rows of rhythm section above the bottom edge */
    L.px = L.x + 2; L.pw = L.w - 4;
    L.py = L.gy + 3 * L.bh + 2;
    L.ph = CLAMP(L.ry - 3 - L.py, 4, 24);
    L.py += (L.ry - 3 - L.py - L.ph) / 2;                        /* spare rows go around the plate */
    L.pbw = CLAMP((L.w - 12 - 27) / RHYTHM_PATTERNS, 6, 10);
    L.kx = L.x + L.w - 2 - 27;
}

/* ---- chord buttons ---- */
static void chord_buttons(struct rect c, bool flash) {
    const struct font *f = text_font();
    for (int q = 0; q < 3; q++) for (int r = 0; r < OMNI_ROOTS; r++) {
        struct rect b = chord_px[q][r];
        bool sel = r == omni.root && q == omni.quality, hover = ui_in(b, ptr.x, ptr.y);
        uint8_t fill, fg; int outline;
        if (sel && omni.chord_on) { fill = ramp(R_AMBER, flash ? 15 : 12); fg = C_BLACK; outline = ramp(R_AMBER, 15); }
        else if (sel) { fill = C_AMBER_D; fg = C_BRIGHT; outline = ramp(R_AMBER, 9); }
        else { fill = ramp(R_PANEL, r == omni.root ? 5 : 3); fg = r == omni.root ? C_AMBER : q == omni.quality ? C_TEXT : C_DIM;
               outline = hover ? ramp(R_AMBER, 7) : ramp(R_GRAY, q == omni.quality ? 6 : 4); }
        gfx_round(b.x, b.y, b.w, b.h, MIN(6, b.h / 2), fill, outline);
        const char *name = omni_root_names[r];
        gfx_text(b.x + (b.w - gfx_text_width(name, f, 1)) / 2, b.y + (b.h - f->height) / 2, name, f, fg, -1, 1);
    }
    (void)c;
}

static void chord_section(uint64_t now) {
    const struct font *f = text_font();
    for (int r = 0; r < OMNI_ROOTS; r++) {                                  /* the number row above its buttons */
        char k[2] = { omni_root_keys[r], 0 };
        text_str(L.gx + r * L.bw + (L.bw - 1) / 2, L.gy - 1, k, r == omni.root ? C_AMBER : C_DIM, C_BG);
    }
    for (int q = 0; q < 3; q++)
        text_str(L.x + 2, L.gy + q * L.bh + (L.bh - 1) / 2, omni_quality_names[q], q == omni.quality ? C_AMBER : C_DIM, C_BG);
    struct rect c = text_rect(L.gx, L.gy, OMNI_ROOTS * L.bw, 3 * L.bh);
    for (int q = 0; q < 3; q++) for (int r = 0; r < OMNI_ROOTS; r++)
        chord_px[q][r] = (struct rect){ c.x + r * L.bw * f->width + 2, c.y + q * L.bh * f->height + 2, L.bw * f->width - 4, L.bh * f->height - 4 };
    bool flash = omni.chord_on && now - omni.root_hit_ms < 90;
    int hover = -1;
    for (int q = 0; q < 3; q++) for (int r = 0; r < OMNI_ROOTS; r++) if (ui_in(chord_px[q][r], ptr.x, ptr.y)) hover = q * OMNI_ROOTS + r;
    uint32_t key = ui_hash_int(ui_hash_int(UI_HASH0, omni.root | omni.quality << 8 | omni.chord_on << 16 | flash << 17), hover);
    if (ui_canvas_keyed(&c, L.gx, L.gy, OMNI_ROOTS * L.bw, 3 * L.bh, C_BG, key)) chord_buttons(c, flash);
    char oct[16]; snfmt(oct, sizeof oct, "octave %+d", omni.octave);
    text_str(L.gx, L.gy + 3 * L.bh, oct, C_DIM, C_BG);
    ui_led(L.gx + 12, L.gy + 3 * L.bh, omni.hold, C_GREEN, "hold", C_BG);
}

/* ---- the display: the chord, its notes, the two sounds; the scope where there is room ---- */
static void display(uint64_t now) {
    if (L.dw < 16) return;
    const struct font *f = text_font(), *big = &font_t12x24;
    int scope_h = L.dh >= 8 ? L.dh - 5 : 0, info_h = L.dh - scope_h;
    uint32_t key = ui_hash_int(ui_hash_int(UI_HASH0, omni.root | omni.quality << 8 | omni.chord_on << 16 | omni.hold << 17),
                               omni.pad_preset | omni.strum_preset << 8 | omni.octave << 16);
    struct rect c;
    if (ui_canvas_keyed(&c, L.dx, L.dy, L.dw, info_h, C_BG, key)) {
        gfx_round(c.x, c.y, c.w, c.h, 8, ramp(R_PANEL, 1), ramp(R_AMBER, 5));
        int k = MAX(1, f->height / 24), scale = (info_h >= 5 ? 2 : 1) * k;
        uint8_t col = omni.chord_on || omni.hold ? C_AMBER : C_DIM;
        const char *root = omni_root_names[omni.root];
        /* the chord, big, on the left; its notes and the two sounds in a column beside it */
        int tx = c.x + f->width * 2, ty = c.y + (c.h - big->height * scale) / 2;
        gfx_text(tx + k, ty, root, big, col, -1, scale);
        int wx = gfx_text(tx, ty, root, big, col, -1, scale) + k;
        gfx_text(tx + wx + f->width / 2, ty + big->height * scale - f->height, omni_quality_names[omni.quality], f, col, -1, 1);
        int ix = tx + wx + f->width * 5, iy = c.y + (c.h - 3 * f->height) / 2;
        char notes[32] = ""; int n = 0;
        for (int i = 0; i < omni.n_notes; i++) { char nn[8]; omni_note_name(omni.chord_notes[i], nn); n += snfmt(notes + n, sizeof notes - (size_t)n, "%s%s", i ? " " : "", nn); }
        char line[48];
        if (ix + gfx_text_width(notes, f, 1) < c.x + c.w - 8) gfx_text(ix, iy, notes, f, C_TEXT, -1, 1);
        snfmt(line, sizeof line, "chord   %s", synth_preset_name(omni.pad_preset));
        if (ix + gfx_text_width(line, f, 1) < c.x + c.w - 8) gfx_text(ix, iy + f->height, line, f, C_AMBER, -1, 1);
        snfmt(line, sizeof line, "strings %s", synth_preset_name(omni.strum_preset));
        if (ix + gfx_text_width(line, f, 1) < c.x + c.w - 8) gfx_text(ix, iy + 2 * f->height, line, f, C_GREEN, -1, 1);
    }
    if (scope_h) ui_scope(L.dx, L.dy + info_h, L.dw, scope_h);
    (void)now;
}

/* ---- the sonic strings: 21 strings between two bridges; a plucked one swings and fades ---- */
static void plate(struct rect c, uint64_t now) {
    const struct font *f = text_font();
    gfx_round(c.x, c.y, c.w, c.h, 8, ramp(R_PANEL, 2), ramp(R_AMBER, 5));
    int top = c.y + f->height / 2, bot = c.y + c.h - f->height / 2, len = bot - top;
    gfx_hline(c.x + 6, top, c.w - 12, ramp(R_AMBER, 7)); gfx_hline(c.x + 6, bot, c.w - 12, ramp(R_AMBER, 7));
    int sp = c.w / STRINGS;
    int swing = (int)(sine_q15[((now * 9 * 256) / 1000 + 64) & 255]);   /* cos, 9 Hz: the swing of every ringing string */
    for (int s = 0; s < STRINGS; s++) {
        int x = c.x + sp * s + sp / 2;
        uint64_t age = string_age(s, now);
        if (age >= RING_MS) { gfx_vline(x, top + 1, len - 1, ramp(R_GRAY, s < OMNI_ZONES - 1 ? 5 : 7)); continue; }
        int amp = (int)((uint64_t)MIN(sp / 3, 18) * (RING_MS - age) / RING_MS);
        uint8_t col = ramp(R_GREEN, 6 + (int)(9 * (RING_MS - age) / RING_MS));
        int px = x, py = top;
        for (int k = 1; k <= 16; k++) {                             /* the string's fundamental: a bow across its length */
            int yy = top + len * k / 16;
            int xx = x + amp * sine_q15[(k * 128 / 16) & 255] / 32768 * swing / 32768;
            gfx_line(px, py, xx, yy, col); gfx_line(px + 1, py, xx + 1, yy, col);
            px = xx; py = yy;
        }
    }
    for (int k = 0; k < FINGERS; k++) {                            /* where the fingers are on the touchpad */
        if (!pad.f[k].on) continue;
        int x = c.x + (int)((int64_t)pad.f[k].x * (sp * STRINGS) / 32768);
        gfx_disc(x, c.y + c.h / 2, MAX(3, f->width / 2), ramp(R_CYAN, 14));
    }
}

static void strings_section(uint64_t now) {
    const struct font *f = text_font();
    text_str(L.px, L.py - 1, "SONIC STRINGS", C_GREEN, C_BG);
    text_str_n(L.px + 15, L.py - 1, "a finger across the touchpad, or the letter rows", L.pw - 15, C_DIM, C_BG);
    bool ringing = false;
    for (int s = 0; s < STRINGS; s++) if (string_age(s, now) < RING_MS) ringing = true;
    uint32_t key = ui_hash_int(UI_HASH0, ringing ? (int)(now / 16) : -1);
    for (int k = 0; k < FINGERS; k++) if (pad.f[k].on) key = ui_hash_int(key, k << 8 | pad.f[k].x >> 8);
    struct rect c;
    plate_px = text_rect(L.px, L.py, L.pw, L.ph);
    if (ui_canvas_keyed(&c, L.px, L.py, L.pw, L.ph, C_BG, key)) plate(c, now);
    int sp_px = plate_px.w / STRINGS;
    for (int s = 0; s < STRINGS; s++) {                              /* the note and its key under each string */
        int r, z; string_zone(s, &r, &z);
        int cx = (plate_px.x + sp_px * s + sp_px / 2 - text_rect(0, 0, 1, 1).x) / f->width;
        char name[8]; omni_note_name(omni_zone_note(r, z), name);
        bool hot = string_age(s, now) < RING_MS;
        text_str(cx - (ui_cells(name) - 1) / 2, L.py + L.ph, name, hot ? C_GREEN : C_TEXT, C_BG);
        char k[2] = { omni_zone_keys[r][z], 0 }; if (k[0] >= 'a' && k[0] <= 'z') k[0] -= 32;
        text_str(cx, L.py + L.ph + 1, k, r ? C_DIM : C_BORDER, C_BG);
    }
}

/* ---- the rhythm section: patterns, the steps of the bar, tempo, start, auto bass, and the level knobs ---- */
static void knobs(struct rect c) {
    const struct font *f = text_font();
    int kw = c.w / 3;
    for (int i = 0; i < 3; i++) {
        int cx = c.x + kw * i + kw / 2, rad = MIN(kw / 3, (c.h - f->height) / 2 - 2), cy = c.y + rad + 3;
        int v = *knob_val(i);
        for (int d = 0; d <= 12; d++) {                              /* the scale: 7 o'clock to 5 o'clock */
            int a = (160 - d * 192 / 12) & 255;                      /* 256 steps a turn; 0 = right, counterclockwise */
            int lit = d * 127 / 12 <= v;
            int x = cx + (rad + 3) * sine_q15[(a + 64) & 255] / 32768, y = cy - (rad + 3) * sine_q15[a] / 32768;
            gfx_pixel(x, y, lit ? ramp(R_AMBER, 12) : ramp(R_GRAY, 5));
        }
        gfx_disc(cx, cy, rad, ramp(R_PANEL, knob_drag == i ? 7 : 5)); gfx_ring(cx, cy, rad, ramp(R_AMBER, 8));
        int a = (160 - v * 192 / 127) & 255;
        gfx_line(cx, cy, cx + (rad - 2) * sine_q15[(a + 64) & 255] / 32768, cy - (rad - 2) * sine_q15[a] / 32768, C_BRIGHT);
        gfx_text(cx - gfx_text_width(knob_names[i], f, 1) / 2, c.y + c.h - f->height, knob_names[i], f, C_DIM, -1, 1);
    }
}

static void rhythm_section(uint64_t now) {
    const struct font *f = text_font();
    int x = L.x + 2, y = L.ry;
    text_str(x, y, "RHYTHM", C_CYAN, C_BG);
    struct rect c;
    uint32_t key = ui_hash_int(UI_HASH0, rhythm.pattern | rhythm.playing << 8);
    if (ui_canvas_keyed(&c, x + 8, y, RHYTHM_PATTERNS * L.pbw, 1, C_BG, key))
        for (int i = 0; i < RHYTHM_PATTERNS; i++) {
            struct rect b = { c.x + i * L.pbw * f->width + 1, c.y + 1, L.pbw * f->width - 3, f->height - 2 };
            bool sel = i == rhythm.pattern;
            gfx_round(b.x, b.y, b.w, b.h, 4, sel ? ramp(R_CYAN, rhythm.playing ? 12 : 7) : ramp(R_PANEL, 3), sel ? -1 : ramp(R_GRAY, 5));
            static const char *const tight[RHYTHM_PATTERNS] = { "ROCK", "POP", "DISCO", "16B", "SWING", "WALTZ", "BOSSA", "REGG", "SIEVE" };
            const char *nm = gfx_text_width(rhythm_names[i], f, 1) > b.w - 2 ? tight[i] : rhythm_names[i];   /* narrow screens */
            gfx_text(b.x + (b.w - gfx_text_width(nm, f, 1)) / 2, b.y + (b.h - f->height) / 2 + 1, nm, f, sel ? C_BLACK : C_DIM, -1, 1);
        }
    for (int i = 0; i < RHYTHM_PATTERNS; i++) pattern_px[i] = text_rect(x + 8 + i * L.pbw, y, L.pbw, 1);
    /* the bar: a light per step, the beats larger; the current step lit while it plays; then tempo and switches */
    int steps = rhythm_steps(), per = rhythm_beat_steps();
    key = ui_hash_int(ui_hash_int(UI_HASH0, (rhythm.playing ? rhythm.pos : -1) | rhythm.pattern << 8), (int32_t)sieve_changes);   /* SIEVE: its sieves */
    if (ui_canvas_keyed(&c, x + 8, y + 1, 2 * 16 + 2, 1, C_BG, key))
        for (int s = 0; s < steps; s++) {
            int cx = c.x + s * 2 * f->width + f->width, cy = c.y + f->height / 2;
            bool on = rhythm.playing && s == rhythm.pos, beat = s % per == 0;
            int kick = rhythm_hit(0, s), snare = rhythm_hit(1, s);
            uint8_t col = on ? ramp(R_CYAN, 15) : kick ? ramp(R_AMBER, 8) : snare ? ramp(R_PINK, 8) : ramp(R_GRAY, beat ? 7 : 4);
            gfx_disc(cx, cy, beat ? MAX(3, f->width / 2) : MAX(2, f->width / 3), col);
        }
    char t[24]; snfmt(t, sizeof t, "tempo %u", seq.bpm);
    text_str(x + 8 + 2 * 16 + 2, y + 1, t, C_TEXT, C_BG);
    start_px = text_rect(x + 8, y + 2, 10, 1); bass_px = text_rect(x + 20, y + 2, 12, 1);
    ui_led(x + 8, y + 2, rhythm.playing, C_CYAN, "playing", C_BG);
    ui_led(x + 20, y + 2, rhythm.bass, C_AMBER, "auto bass", C_BG);
    if (L.kx >= MAX(x + 8 + RHYTHM_PATTERNS * L.pbw + 1, x + 8 + 2 * 16 + 12)) {   /* the knobs, where they fit */
        struct rect kr = text_rect(L.kx, y, 27, 3);
        for (int i = 0; i < 3; i++) knob_px[i] = (struct rect){ kr.x + kr.w / 3 * i, kr.y, kr.w / 3, kr.h };
        key = ui_hash_int(UI_HASH0, omni.pad_level | omni.strum_level << 8 | rhythm.level << 16 | (knob_drag + 1) << 24);
        if (ui_canvas_keyed(&c, L.kx, y, 27, 3, C_BG, key)) knobs(c);
    } else for (int i = 0; i < 3; i++) knob_px[i] = (struct rect){ 0, 0, 0, 0 };
    (void)now;
}

static void draw(uint64_t now) {
    if (shown_inst() >= 0) { inst_page_draw(shown_inst(), now); return; }
    if (tab[0] != 'P') snfmt(tab, sizeof tab, "PLAY");   /* the instruments were read again */
    layout();
    text_box(L.x, L.y, L.w, L.h, ramp(R_AMBER, 5), C_BG, false);
    text_put(L.x, L.y, G_RTL, ramp(R_AMBER, 5), C_BG); text_put(L.x + L.w - 1, L.y, G_RTR, ramp(R_AMBER, 5), C_BG);
    text_put(L.x, L.y + L.h - 1, G_RBL, ramp(R_AMBER, 5), C_BG); text_put(L.x + L.w - 1, L.y + L.h - 1, G_RBR, ramp(R_AMBER, 5), C_BG);
    text_str(L.x + 2, L.y + 1, "◆ BARE!", C_AMBER, C_BG);
    text_str(L.x + 11, L.y + 1, "chord · strings · rhythm", C_DIM, C_BG);
    chord_section(now);
    display(now);
    strings_section(now);
    rhythm_section(now);
    FOOTER("1-=", "chord", "Q W E", "maj min 7th", "A-' Z-/", "strings", "SPACE", "hold", "↑↓", "octave",
           "R", "rhythm", "[ ]", "pattern", "PGUP PGDN", "tempo", "TAB", "bass", "⇧?", "keys");
}

/* The pointer plays it too: chord buttons, dragging across the strings, the rhythm buttons, and the knobs (drag up
   or down). The touchpad, when the page has it, is the strings. */
/* a finger (or the mouse, who == FINGERS) moves onto string s (-1: off the strings): each arrival plucks the string,
   even one already ringing under another finger */
static void to_string(int who, int s, uint64_t now) {
    if (s >= STRINGS) s = STRINGS - 1;
    int old = string_hit[who], r, z;
    if (s == old) return;
    if (old >= 0 && string_holders[old] && --string_holders[old] == 0) { string_zone(old, &r, &z); omni_strum(r, z, false, now); }
    if (s >= 0) { string_holders[s]++; string_zone(s, &r, &z); omni_strum(r, z, true, now); }
    string_hit[who] = (int8_t)s;
}

static void pointer(uint64_t now) {
    if (shown_inst() >= 0) { inst_page_pointer(shown_inst(), now); return; }
    if (ptr.pressed)
        for (int i = 0; i < 3; i++) if (ui_in(knob_px[i], ptr.x, ptr.y)) { knob_drag = i; drag_y0 = ptr.y; drag_v0 = *knob_val(i); }
    if (knob_drag >= 0) {
        if (ptr.down) *knob_val(knob_drag) = (uint8_t)CLAMP(drag_v0 + (drag_y0 - ptr.y) * 127 / 160, 0, 127);
        else knob_drag = -1;
        return;
    }
    if (ptr.pressed) {
        for (int q = 0; q < 3; q++) for (int r = 0; r < OMNI_ROOTS; r++)
            if (ui_in(chord_px[q][r], ptr.x, ptr.y)) { omni_chord(r, q, true, now); chord_held = r; }
        for (int i = 0; i < RHYTHM_PATTERNS; i++) if (ui_in(pattern_px[i], ptr.x, ptr.y)) rhythm.pattern = (uint8_t)i;
        if (ui_in(start_px, ptr.x, ptr.y)) rhythm_play(!rhythm.playing);
        if (ui_in(bass_px, ptr.x, ptr.y)) rhythm.bass = !rhythm.bass;
    }
    if (ptr.released && chord_held >= 0) { omni_chord(0, 0, false, now); chord_held = -1; }
    int s = -1;
    if (ptr.down && ui_in(plate_px, ptr.x, ptr.y)) s = (ptr.x - plate_px.x) * STRINGS / MAX(1, plate_px.w);
    to_string(FINGERS, s, now);
    for (int k = 0; k < FINGERS; k++) to_string(k, pad.f[k].on ? pad.f[k].x * STRINGS / 32768 : -1, now);
}

static bool key(uint8_t code, bool down, uint64_t now) {
    if (shown_inst() >= 0) return inst_page_key(shown_inst(), code, down, now);
    if (!down) return code == 'r' || code == '[' || code == ']' || code == KEY_TAB || code == KEY_PGUP || code == KEY_PGDN;
    switch (code) {
    case 'r': rhythm_play(!rhythm.playing); return true;
    case '[': rhythm.pattern = (uint8_t)((rhythm.pattern + RHYTHM_PATTERNS - 1) % RHYTHM_PATTERNS); return true;
    case ']': rhythm.pattern = (uint8_t)((rhythm.pattern + 1) % RHYTHM_PATTERNS); return true;
    case KEY_TAB: rhythm.bass = !rhythm.bass; return true;
    case KEY_PGUP: if (seq.bpm < 300) seq.bpm += 2; return true;
    case KEY_PGDN: if (seq.bpm > 40) seq.bpm -= 2; return true;
    }
    return false;
}

const struct page page_play = { tab, "F1", KEY_F1, true, key, 0, pointer, strum_sound, draw, true, midi, 0, again };
