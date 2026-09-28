/* PLAY: the omnichord (core/omni.c), laid out like the Suzuki OM-108 and the keyboard under it. The chord buttons on
   top, three rows over twelve roots (the PC keyboard's three letter rows); the display with the chord and the voice;
   the REAL TIME CONTROL switches; the strumplate's 13 strings and the INSTANT OFF plate (a finger across the touchpad,
   the mouse, or Z … /); and the VOICE and PATTERN buttons in their sets, the rhythm's steps and the knobs. Its key again
   (F1 by default) steps to the instruments from files (core/inst.c): their pages (core/page_inst.c) take PLAY's place,
   and the tab takes their name. */
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
#include "font.h"

#define STRINGS OMNI_ZONES
#define RING_MS 700                                    /* how long a plucked string shows its vibration */
#define KNOBS 6
enum { K_MAIN, K_SUB, K_SUSTAIN, K_CHORD, K_RHYTHM, K_TEMPO };
static const char *const knob_names[KNOBS] = { "main", "sub", "sustain", "chord", "rhythm", "tempo" };
static const char *const knob_short[KNOBS] = { "main", "sub", "sust", "chrd", "rhy", "bpm" };
enum { LED_AUTO, LED_HOLD, LED_SYNC, LED_KEYBOARD, LED_CLASSIC, LEDS };

/* geometry shared by drawing and the pointer, in pixels */
static struct rect chord_px[OMNI_ROWS][OMNI_ROOTS], knob_px[KNOBS], plate_px, off_px, led_px[LEDS], start_px;
static struct rect voice_px[5], vset_px, pat_px[5], pset_px;
static int knob_drag = -1, drag_y0, drag_v0, voice_set, pat_set;
/* the buttons the pointer holds down (⇧-click holds more than one: the combinations) */
static uint16_t ptr_held[OMNI_ROWS];
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
int play_showing(void) { return showing; }
static void again(uint64_t now) { (void)now; if (inst_count) play_show((showing + 1) % (inst_count + 1)); }
static bool midi(uint8_t n, uint8_t vel, uint64_t now) { int i = shown_inst(); return i >= 0 && inst_page_midi(i, n, vel, now); }
static int strum_sound(void) { int i = shown_inst(); return i >= 0 ? P_INST1 + i : -1; }

static uint64_t string_age(int s, uint64_t now) { return omni.zone_hit_ms[s] ? now - omni.zone_hit_ms[s] : 100000; }
static int knob_get(int i) {
    switch (i) {
    case K_MAIN: return omni.main_level; case K_SUB: return omni.sub_level; case K_SUSTAIN: return omni.sustain;
    case K_CHORD: return omni.pad_level; case K_RHYTHM: return rhythm.level;
    }
    return (CLAMP(seq.bpm, 40, 300) - 40) * 127 / 260;
}
static void knob_set(int i, int v) {
    v = CLAMP(v, 0, 127);
    switch (i) {
    case K_MAIN: omni.main_level = (uint8_t)v; break; case K_SUB: omni.sub_level = (uint8_t)v; break;
    case K_SUSTAIN: omni.sustain = (uint8_t)v; break; case K_CHORD: omni.pad_level = (uint8_t)v; break;
    case K_RHYTHM: rhythm.level = (uint8_t)v; break;
    case K_TEMPO: seq.bpm = (uint16_t)(40 + v * 260 / 127); break;
    }
}
static bool led_on(int i) {
    return i == LED_AUTO ? omni.autoplay : i == LED_HOLD ? omni.hold : i == LED_SYNC ? omni.sync : i == LED_KEYBOARD ? omni.keyboard : rhythm.classic;
}
static void led_toggle(int i, uint64_t now) {
    switch (i) {
    case LED_AUTO:     omni_set_auto(!omni.autoplay, now); break;
    case LED_HOLD:     omni_set_hold(!omni.hold, now); break;
    case LED_SYNC:     omni.sync = !omni.sync; break;
    case LED_KEYBOARD: omni_off(now); omni.keyboard = !omni.keyboard; break;
    case LED_CLASSIC:  rhythm.classic = !rhythm.classic; break;
    }
}
static const char *const led_names[LEDS] = { "AUTO", "HOLD", "SYNC", "KEYBOARD", "CLASSIC" };

/* ---- layout, in cells ---- */
static struct { int x, y, w, h, bh, bw, gx, gy, dx, dy, dw, dh, cy, px, py, pw, ph, ry, kx, kw, bx, bbw; } L;
static void layout(void) {
    int cols = text_cols(), rows = text_rows();
    L.x = 1; L.y = 1; L.w = cols - 2; L.h = rows - 3;
    L.bh = rows >= 60 ? 3 : rows >= 44 ? 2 : 1;
    L.gx = L.x + 9; L.gy = L.y + 3;
    L.bw = CLAMP((L.w - 9 - 3 - 26) / OMNI_ROOTS, 5, 9);
    L.dx = L.gx + OMNI_ROOTS * L.bw + 2; L.dy = L.y + 1; L.dw = L.x + L.w - 2 - L.dx; L.dh = L.gy + 3 * L.bh - L.dy;
    L.cy = L.gy + 3 * L.bh;                                      /* the switches, under the buttons */
    L.ry = L.y + L.h - 5;                                        /* three rows of VOICE, PATTERN and the steps */
    L.px = L.x + 2; L.pw = L.w - 4 - 8;                           /* the strings, and INSTANT OFF beside them */
    L.py = L.cy + 3;
    L.ph = CLAMP(L.ry - 3 - L.py, 3, 24);
    L.py += (L.ry - 3 - L.py - L.ph) / 2;
    L.kw = CLAMP((L.w - 4) / 14, 6, 9);
    L.kx = L.x + L.w - 2 - KNOBS * L.kw;
    L.bx = L.x + 2 + 9 + 4;                                      /* VOICE / PATTERN buttons, after the label and the set */
    L.bbw = CLAMP((L.kx - 2 - L.bx) / 5, 5, 13);
}

/* ---- chord buttons (in keyboard mode: T P ↓ ↑ and eight drums, the black keys, the white keys) ---- */
static const char *button_label(int row, int col, char *buf) {
    if (!omni.keyboard) return omni_root_names[col];
    if (row == ROW_MAJ) return omni_kb_top[col];
    int n = omni_kb_note(row, col);
    if (n < 0) return "";
    omni_note_name(n, buf); buf[strlen(buf) - 1] = 0;           /* the name without the octave */
    return buf;
}
static bool button_down(int row, int col) { return omni.held[row] >> col & 1; }
static void chord_buttons(bool flash) {
    const struct font *f = text_font();
    int suf = omni.suffix;
    for (int w = 0; w < OMNI_ROWS; w++) for (int r = 0; r < OMNI_ROOTS; r++) {
        struct rect b = chord_px[w][r];
        char buf[8]; const char *name = button_label(w, r, buf);
        bool down = button_down(w, r), hover = ui_in(b, ptr.x, ptr.y);
        bool part = !omni.keyboard && (omni.chord_on || omni.hold) && r == omni.root &&
                    ((w == ROW_MAJ && suf != SUF_MIN && suf != SUF_7 && suf != SUF_MIN7) || (w == ROW_MIN && (suf == SUF_MIN || suf == SUF_MIN7 || suf == SUF_DIM || suf == SUF_AUG)) ||
                     (w == ROW_7 && (suf == SUF_7 || suf == SUF_MAJ7 || suf == SUF_MIN7 || suf == SUF_AUG)));
        uint8_t fill, fg; int outline;
        if (!*name) { fill = ramp(R_PANEL, 1); fg = C_DIM; outline = ramp(R_GRAY, 3); }
        else if (down) { fill = ramp(R_AMBER, flash ? 15 : 12); fg = C_BLACK; outline = ramp(R_AMBER, 15); }
        else if (part) { fill = C_AMBER_D; fg = C_BRIGHT; outline = ramp(R_AMBER, 9); }
        else { fill = ramp(R_PANEL, omni.keyboard && w == ROW_7 ? 6 : 3); fg = r == omni.root && !omni.keyboard ? C_AMBER : C_TEXT;
               outline = hover ? ramp(R_AMBER, 7) : ramp(R_GRAY, 5); }
        gfx_round(b.x, b.y, b.w, b.h, MIN(6, b.h / 2), fill, outline);
        gfx_text(b.x + (b.w - gfx_text_width(name, f, 1)) / 2, b.y + (b.h - f->height) / 2, name, f, fg, -1, 1);
    }
}
static void chord_section(uint64_t now) {
    const struct font *f = text_font();
    for (int r = 0; r < OMNI_ROOTS; r++) {                                  /* the number row above its buttons */
        char k[2] = { omni_row_keys[0][r], 0 };
        text_str(L.gx + r * L.bw + (L.bw - 1) / 2, L.gy - 1, k, C_DIM, C_BG);
    }
    static const char *const kb_rows[OMNI_ROWS] = { "DRUMS", "", "KEYS" };
    for (int w = 0; w < OMNI_ROWS; w++) {
        const char *nm = omni.keyboard ? kb_rows[w] : omni_row_names[w];
        text_str(L.x + 2, L.gy + w * L.bh + (L.bh - 1) / 2, nm, C_DIM, C_BG);
        char k[4] = { (char)(omni_row_keys[w][0] - (w ? 32 : 0)), 0 };
        text_str(L.x + 8, L.gy + w * L.bh + (L.bh - 1) / 2, k, C_BORDER, C_BG);
    }
    struct rect c = text_rect(L.gx, L.gy, OMNI_ROOTS * L.bw, 3 * L.bh);
    for (int w = 0; w < OMNI_ROWS; w++) for (int r = 0; r < OMNI_ROOTS; r++)
        chord_px[w][r] = (struct rect){ c.x + r * L.bw * f->width + 2, c.y + w * L.bh * f->height + 2, L.bw * f->width - 4, L.bh * f->height - 4 };
    bool flash = (omni.chord_on || omni.hold) && now - omni.root_hit_ms < 90;
    int hover = -1;
    for (int w = 0; w < OMNI_ROWS; w++) for (int r = 0; r < OMNI_ROOTS; r++) if (ui_in(chord_px[w][r], ptr.x, ptr.y)) hover = w * OMNI_ROOTS + r;
    uint32_t key = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, omni.root | omni.suffix << 4 | omni.chord_on << 8 | omni.hold << 9 | flash << 10 | omni.keyboard << 11 | (omni.kb_octave + 1) << 12), hover),
                               omni.held[0] | omni.held[1] << 12 | (uint32_t)omni.held[2] << 24);
    if (ui_canvas_keyed(&c, L.gx, L.gy, OMNI_ROOTS * L.bw, 3 * L.bh, C_BG, key)) chord_buttons(flash);
}

/* ---- the display: the chord, its notes, the voice; the scope where there is room ---- */
static void display(uint64_t now) {
    if (L.dw < 16) return;
    const struct font *f = text_font(), *big = &font_t12x24;
    int scope_h = L.dh >= 9 ? L.dh - 6 : 0, info_h = L.dh - scope_h;
    char name[12]; omni_chord_name(name);
    uint32_t key = ui_hash(ui_hash_int(ui_hash_int(UI_HASH0, omni.chord_on | omni.hold << 1 | omni.keyboard << 2 | omni.voice << 8 | omni.transpose << 16),
                                       omni.pad_preset | omni.strum_preset << 8 | (omni.tune + 8) << 16 | (omni.octave + 4) << 24), name, (int)strlen(name));
    struct rect c;
    if (ui_canvas_keyed(&c, L.dx, L.dy, L.dw, info_h, C_BG, key)) {
        gfx_round(c.x, c.y, c.w, c.h, 8, ramp(R_PANEL, 1), ramp(R_AMBER, 5));
        int k = MAX(1, f->height / 24), scale = (info_h >= 5 ? 2 : 1) * k;
        uint8_t col = omni.chord_on || omni.hold ? C_AMBER : C_DIM;
        const char *shown = omni.keyboard ? "KEYS" : name;
        int tx = c.x + f->width * 2, ty = c.y + f->height / 2;
        gfx_text(tx, ty, shown, big, col, -1, info_h >= 4 ? scale : k);
        int ly = ty + big->height * (info_h >= 4 ? scale : k) + 2;
        char line[48], notes[24] = "";
        int n = 0;
        for (int i = 0; i < omni.n_notes && !omni.keyboard; i++) { char nn[6]; omni_note_name(omni.chord_notes[i], nn); n += snfmt(notes + n, sizeof notes - (size_t)n, "%s%s", i ? " " : "", nn); }
        if (omni.transpose) n += snfmt(notes + n, sizeof notes - (size_t)n, "  %+d", omni.transpose);
        if (ly + f->height <= c.y + c.h - 2) { gfx_text(tx, ly, notes, f, C_TEXT, -1, 1); ly += f->height; }
        if (omni.voice < OMNI_VOICES) snfmt(line, sizeof line, "%s: %s + %s", omni_voices[omni.voice].name, synth_preset_name(omni_voices[omni.voice].main), synth_preset_name(omni_voices[omni.voice].sub));
        else snfmt(line, sizeof line, "custom: %s", synth_preset_name(omni.strum_preset));
        if (ly + f->height <= c.y + c.h - 2) { gfx_text(tx, ly, line, f, C_GREEN, -1, 1); ly += f->height; }
        snfmt(line, sizeof line, "chord: %s", synth_preset_name(omni.pad_preset));
        if (ly + f->height <= c.y + c.h - 2) gfx_text(tx, ly, line, f, C_AMBER, -1, 1);
    }
    if (scope_h) ui_scope(L.dx, L.dy + info_h, L.dw, scope_h);
    (void)now;
}

/* ---- REAL TIME CONTROL, octave, transpose, tune ---- */
static void switches(void) {
    char t[48];
    snfmt(t, sizeof t, "octave %+d  transpose %+d  A %d Hz", omni.keyboard ? omni.kb_octave : omni.octave, omni.transpose, 440 + omni.tune);
    text_str(L.gx, L.cy, t, C_DIM, C_BG);
    int x = L.gx + ui_cells(t) + 3;
    for (int i = 0; i < LEDS; i++) {
        int w = ui_cells(led_names[i]) + 2;
        if (x + w > L.x + L.w - 2) { led_px[i] = (struct rect){ 0, 0, 0, 0 }; continue; }
        ui_led(x, L.cy, led_on(i), i == LED_KEYBOARD ? C_CYAN : C_GREEN, led_names[i], C_BG);
        led_px[i] = text_rect(x, L.cy, w, 1);
        x += w + 2;
    }
}

/* ---- the strumplate: 13 strings between two bridges; a plucked one swings and fades ---- */
static void plate(struct rect c, uint64_t now) {
    const struct font *f = text_font();
    gfx_round(c.x, c.y, c.w, c.h, 8, ramp(R_PANEL, 2), ramp(R_AMBER, 5));
    int top = c.y + f->height / 2, bot = c.y + c.h - f->height / 2, len = bot - top;
    gfx_hline(c.x + 6, top, c.w - 12, ramp(R_AMBER, 7)); gfx_hline(c.x + 6, bot, c.w - 12, ramp(R_AMBER, 7));
    int sp = c.w / STRINGS;
    if (omni.keyboard) {                                             /* a drum set: seven zones, lit while struck */
        for (int d = 0; d < 7; d++) {
            int x0 = c.x + 6 + (c.w - 12) * d / 7, x1 = c.x + 6 + (c.w - 12) * (d + 1) / 7;
            bool hot = false;
            for (int s = 0; s < STRINGS; s++) if (s * 7 / STRINGS == d && string_age(s, now) < RING_MS) hot = true;
            if (hot) gfx_fill(x0 + 2, top + 2, x1 - x0 - 4, len - 3, ramp(R_CYAN, 5));
            if (d) gfx_vline(x0, top + 1, len - 1, ramp(R_GRAY, 6));
        }
        return;
    }
    int swing = (int)(sine_q15[((now * 9 * 256) / 1000 + 64) & 255]);   /* cos, 9 Hz: the swing of every ringing string */
    for (int s = 0; s < STRINGS; s++) {
        int x = c.x + sp * s + sp / 2;
        uint64_t age = string_age(s, now);
        bool root = !omni.keyboard && (s % 3 == 0);                  /* the roots: the manual's dots */
        if (age >= RING_MS) {
            gfx_vline(x, top + 1, len - 1, ramp(R_GRAY, root ? 8 : 5));
            if (root) gfx_disc(x, bot + f->height / 4, MAX(2, f->width / 4), ramp(R_AMBER, 8));
            continue;
        }
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
    text_str(L.px, L.py - 1, omni.keyboard ? "DRUM SET" : "STRUMPLATE", C_GREEN, C_BG);
    text_str_n(L.px + 12, L.py - 1, omni.keyboard ? "the strings are drums: on the rhythm's steps while it plays" : "a finger across the touchpad, the mouse, or Z to /", L.pw - 12, C_DIM, C_BG);
    bool ringing = false;
    for (int s = 0; s < STRINGS; s++) if (string_age(s, now) < RING_MS) ringing = true;
    uint32_t key = ui_hash_int(ui_hash_int(UI_HASH0, ringing ? (int)(now / 16) : -1), omni.keyboard);
    for (int k = 0; k < FINGERS; k++) if (pad.f[k].on) key = ui_hash_int(key, k << 8 | pad.f[k].x >> 8);
    struct rect c;
    plate_px = text_rect(L.px, L.py, L.pw, L.ph);
    if (ui_canvas_keyed(&c, L.px, L.py, L.pw, L.ph, C_BG, key)) plate(c, now);
    int sp_px = plate_px.w / STRINGS;
    for (int d = 0; omni.keyboard && d < 7; d++) {                   /* keyboard mode: each drum's zone, and its key */
        int cx = L.px + L.pw * (2 * d + 1) / 14;
        text_str(cx - 1, L.py + L.ph, omni_plate_drums[d], C_CYAN, C_BG);
        char k[2] = { (char)(omni_zone_keys[d] - 32), 0 };
        text_str(cx, L.py + L.ph + 1, k, C_DIM, C_BG);
    }
    for (int s = 0; s < STRINGS && !omni.keyboard; s++) {          /* the note and its key under each string */
        int cx = (plate_px.x + sp_px * s + sp_px / 2 - text_rect(0, 0, 1, 1).x) / f->width;
        char name[8];
        if (omni.keyboard) snfmt(name, sizeof name, "%s", omni_plate_drums[MIN(s * 7 / STRINGS, 6)]);
        else omni_note_name(omni_zone_note(s), name);
        bool hot = string_age(s, now) < RING_MS;
        text_str(cx - (ui_cells(name) - 1) / 2, L.py + L.ph, name, hot ? C_GREEN : s % 3 == 0 && !omni.keyboard ? C_AMBER : C_TEXT, C_BG);
        if (s < 10) { char k[2] = { (char)(omni_zone_keys[s] >= 'a' ? omni_zone_keys[s] - 32 : omni_zone_keys[s]), 0 }; text_str(cx, L.py + L.ph + 1, k, C_DIM, C_BG); }
    }
    /* INSTANT OFF: a plate of its own beside the strings (in keyboard mode, hand claps) */
    int ox = L.px + L.pw + 1;
    off_px = text_rect(ox, L.py + L.ph / 3, 6, MAX(2, L.ph / 3));
    ui_panel(ox, L.py + L.ph / 3, 7, MAX(3, L.ph / 3), "", C_RED);
    text_str(ox + 2, L.py + L.ph / 3 + 1, omni.keyboard ? "HC" : "OFF", C_RED, C_PANEL);
    text_str(ox, L.py + L.ph / 3 + MAX(3, L.ph / 3), "BKSP", C_DIM, C_BG);
}

/* ---- VOICE and PATTERN, in their sets; the rhythm's steps; the knobs ---- */
static void knobs(struct rect c) {
    const struct font *f = text_font();
    int kw = c.w / KNOBS;
    for (int i = 0; i < KNOBS; i++) {
        int cx = c.x + kw * i + kw / 2, rad = MIN(kw / 3, (c.h - f->height) / 2 - 2), cy = c.y + rad + 3;
        int v = knob_get(i);
        for (int d = 0; d <= 12; d++) {                              /* the scale: 7 o'clock to 5 o'clock */
            int a = (160 - d * 192 / 12) & 255;                      /* 256 steps a turn; 0 = right, counterclockwise */
            int lit = d * 127 / 12 <= v;
            int x = cx + (rad + 3) * sine_q15[(a + 64) & 255] / 32768, y = cy - (rad + 3) * sine_q15[a] / 32768;
            gfx_pixel(x, y, lit ? ramp(R_AMBER, 12) : ramp(R_GRAY, 5));
        }
        gfx_disc(cx, cy, rad, ramp(R_PANEL, knob_drag == i ? 7 : 5)); gfx_ring(cx, cy, rad, ramp(R_AMBER, 8));
        int a = (160 - v * 192 / 127) & 255;
        gfx_line(cx, cy, cx + (rad - 2) * sine_q15[(a + 64) & 255] / 32768, cy - (rad - 2) * sine_q15[a] / 32768, C_BRIGHT);
        const char *nm = gfx_text_width(knob_names[i], f, 1) >= kw - 2 ? knob_short[i] : knob_names[i];
        gfx_text(cx - gfx_text_width(nm, f, 1) / 2, c.y + c.h - f->height, nm, f, C_DIM, -1, 1);
    }
}
static void button_row(int y, const char *label, const char *set, struct rect *set_px, struct rect *px, const char *const *names, const int8_t *on, uint8_t accent) {
    text_str(L.x + 2, y, label, accent, C_BG);
    text_fill(L.bx - 4, y, 3, 1, ' ', C_TEXT, C_AMBER);             /* the set button: the OM-108's yellow one */
    text_str(L.bx - 3, y, set, C_BLACK, C_AMBER);
    *set_px = text_rect(L.bx - 4, y, 3, 1);
    for (int i = 0; i < 5; i++) {
        int x = L.bx + i * L.bbw;
        px[i] = text_rect(x, y, L.bbw - 1, 1);
        if (!names[i]) { text_fill(x, y, L.bbw - 1, 1, ' ', C_DIM, C_BG); px[i] = (struct rect){ 0, 0, 0, 0 }; continue; }
        bool sel = on[i] > 0, waits = on[i] < 0;
        uint8_t bg = sel ? accent : waits ? C_BORDER : C_PANEL;
        text_fill(x, y, L.bbw - 1, 1, ' ', C_TEXT, bg);
        text_str_n(x + 1, y, names[i], L.bbw - 2, sel ? C_BLACK : waits ? C_BRIGHT : C_TEXT, bg);
    }
}
static void bottom_section(uint64_t now) {
    const struct font *f = text_font();
    int y = L.ry;
    /* VOICE: two sets of five, as the OM-108's */
    const char *vn[5]; int8_t von[5];
    for (int i = 0; i < 5; i++) { int v = voice_set * 5 + i; vn[i] = omni_voices[v].name; von[i] = omni.voice == v; }
    char vs[4]; snfmt(vs, sizeof vs, "%d", voice_set + 1);
    button_row(y, "VOICE", vs, &vset_px, voice_px, vn, von, C_GREEN);
    /* PATTERN: its two sets, and BARE!'s */
    const char *pn[5]; int8_t pon[5];
    for (int i = 0; i < 5; i++) {
        int p = rhythm_sets[pat_set][i];
        pn[i] = p == 0xFF ? 0 : rhythm_names[p];
        pon[i] = p == 0xFF ? 0 : rhythm.next == p ? -1 : rhythm.pattern == p;
    }
    char ps[4]; snfmt(ps, sizeof ps, "%c", pat_set == 2 ? 'B' : '1' + pat_set);
    button_row(y + 1, "PATTERN", ps, &pset_px, pat_px, pn, pon, C_CYAN);
    /* the bar: a light per step, the beats larger; the current step lit while it plays; the tempo and START */
    int steps = rhythm_steps(), per = rhythm_beat_steps();
    uint32_t key = ui_hash_int(ui_hash_int(UI_HASH0, (rhythm.playing ? rhythm.pos : -1) | rhythm.pattern << 8), (int32_t)sieve_changes);   /* SIEVE: its sieves */
    struct rect c;
    if (ui_canvas_keyed(&c, L.bx, y + 2, 2 * 16 + 2, 1, C_BG, key))
        for (int s = 0; s < steps; s++) {
            int cx = c.x + s * 2 * f->width + f->width, cy = c.y + f->height / 2;
            bool on = rhythm.playing && s == rhythm.pos, beat = s % per == 0;
            int kick = rhythm_hit(0, s), snare = rhythm_hit(1, s);
            uint8_t col = on ? ramp(R_CYAN, 15) : kick ? ramp(R_AMBER, 8) : snare ? ramp(R_PINK, 8) : ramp(R_GRAY, beat ? 7 : 4);
            gfx_disc(cx, cy, beat ? MAX(3, f->width / 2) : MAX(2, f->width / 3), col);
        }
    text_str(L.x + 2, y + 2, "RHYTHM", C_DIM, C_BG);
    int tx = L.bx + 2 * 16 + 3;
    start_px = text_rect(tx, y + 2, 7, 1);
    ui_led(tx, y + 2, rhythm.playing, C_CYAN, rhythm.playing ? "START" : "STOP", C_BG);
    char t[16]; snfmt(t, sizeof t, "%u BPM", seq.bpm);
    if (tx + 9 + ui_cells(t) < L.kx) text_str(tx + 9, y + 2, t, C_TEXT, C_BG);
    /* the knobs */
    struct rect kr = text_rect(L.kx, y, KNOBS * L.kw, 3);
    for (int i = 0; i < KNOBS; i++) knob_px[i] = (struct rect){ kr.x + kr.w / KNOBS * i, kr.y, kr.w / KNOBS, kr.h };
    key = UI_HASH0;
    for (int i = 0; i < KNOBS; i++) key = ui_hash_int(key, knob_get(i));
    key = ui_hash_int(key, knob_drag);
    if (ui_canvas_keyed(&c, L.kx, y, KNOBS * L.kw, 3, C_BG, key)) knobs(c);
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
    text_str_n(L.x + 11, L.y + 1, "omnichord · after the Suzuki OM-108", L.dx - L.x - 12, C_DIM, C_BG);
    chord_section(now);
    display(now);
    switches();
    strings_section(now);
    bottom_section(now);
    if (omni.keyboard) FOOTER("A-' W-]", "keyboard", "1 2", "T P + 3 4", "3 4", "octave", "5-=", "drums", "Z-M", "drum strings", "CAPS", "chords", "⇧?", "keys");
    else FOOTER("1-= Q-] A-'", "chords (together: more)", "Z-/", "strings", "SPACE", "rhythm", "TAB", "auto", "`", "hold",
                "BKSP", "off", "← →", "pattern", "HOME END", "voice", "↑ ↓", "octave", "PGUP PGDN", "tempo", "CAPS", "keyboard", "⇧?", "keys");
}

/* The pointer plays it too: the buttons (⇧-click holds more than one), the strings, INSTANT OFF, the switches, the
   VOICE and PATTERN buttons, and the knobs (drag up or down). The touchpad, when the page has it, is the strings. */
/* a finger (or the mouse, who == FINGERS) moves onto string s (-1: off the strings): each arrival plucks the string,
   even one already ringing under another finger */
static void to_string(int who, int s, uint64_t now) {
    if (s >= STRINGS) s = STRINGS - 1;
    int old = string_hit[who];
    if (s == old) return;
    if (old >= 0 && string_holders[old] && --string_holders[old] == 0) omni_strum(old, false, now);
    if (s >= 0) { string_holders[s]++; omni_strum(s, true, now); }
    string_hit[who] = (int8_t)s;
}
static void ptr_buttons_up(uint64_t now) {
    for (int w = 0; w < OMNI_ROWS; w++) for (int r = 0; r < OMNI_ROOTS; r++)
        if (ptr_held[w] >> r & 1) omni_key((uint8_t)omni_row_keys[w][r], false, now);
    memset(ptr_held, 0, sizeof ptr_held);
}
static bool off_held;

static void pointer(uint64_t now) {
    if (shown_inst() >= 0) { inst_page_pointer(shown_inst(), now); return; }
    if (ptr.pressed)
        for (int i = 0; i < KNOBS; i++) if (ui_in(knob_px[i], ptr.x, ptr.y)) { knob_drag = i; drag_y0 = ptr.y; drag_v0 = knob_get(i); }
    if (knob_drag >= 0) {
        if (ptr.down) knob_set(knob_drag, drag_v0 + (drag_y0 - ptr.y) * 127 / 160);
        else knob_drag = -1;
        return;
    }
    if (ptr.pressed) {
        bool on_button = false;
        for (int w = 0; w < OMNI_ROWS; w++) for (int r = 0; r < OMNI_ROOTS; r++)
            if (ui_in(chord_px[w][r], ptr.x, ptr.y)) {
                if (!ui_shift) ptr_buttons_up(now);                 /* without ⇧, one button at a time */
                if (!(ptr_held[w] >> r & 1)) { ptr_held[w] |= (uint16_t)(1u << r); omni_key((uint8_t)omni_row_keys[w][r], true, now); }
                on_button = true;
            }
        if (!on_button && !ui_shift) ptr_buttons_up(now);
        for (int i = 0; i < LEDS; i++) if (ui_in(led_px[i], ptr.x, ptr.y)) led_toggle(i, now);
        if (ui_in(off_px, ptr.x, ptr.y)) { omni_key(KEY_BACKSPACE, true, now); off_held = true; }
        if (ui_in(start_px, ptr.x, ptr.y)) rhythm_play(!rhythm.playing);
        if (ui_in(vset_px, ptr.x, ptr.y)) voice_set ^= 1;
        if (ui_in(pset_px, ptr.x, ptr.y)) pat_set = (pat_set + 1) % RHYTHM_SETS;
        for (int i = 0; i < 5; i++) {
            if (ui_in(voice_px[i], ptr.x, ptr.y)) omni_set_voice(voice_set * 5 + i);
            if (ui_in(pat_px[i], ptr.x, ptr.y) && rhythm_sets[pat_set][i] != 0xFF) rhythm_select(rhythm_sets[pat_set][i]);
        }
    }
    if (ptr.released) { if (!ui_shift) ptr_buttons_up(now); if (off_held) { omni_key(KEY_BACKSPACE, false, now); off_held = false; } }
    if (!ui_shift && !ptr.down && (ptr_held[0] | ptr_held[1] | ptr_held[2])) ptr_buttons_up(now);   /* ⇧ let go: the combination too */
    int s = -1;
    if (ptr.down && ui_in(plate_px, ptr.x, ptr.y)) s = (ptr.x - plate_px.x) * STRINGS / MAX(1, plate_px.w);
    to_string(FINGERS, s, now);
    for (int k = 0; k < FINGERS; k++) to_string(k, pad.f[k].on ? pad.f[k].x * STRINGS / 32768 : -1, now);
}

/* the patterns in the buttons' order: the OM-108's two sets, then BARE!'s */
static int pattern_step(int d) {
    int order[15], n = 0, at = 0;
    for (int s = 0; s < RHYTHM_SETS; s++) for (int i = 0; i < 5; i++) if (rhythm_sets[s][i] != 0xFF) order[n++] = rhythm_sets[s][i];
    int cur = rhythm.next != 0xFF ? rhythm.next : rhythm.pattern;
    for (int i = 0; i < n; i++) if (order[i] == cur) at = i;
    int p = order[(at + d + n) % n];
    for (int s = 0; s < RHYTHM_SETS; s++) for (int i = 0; i < 5; i++) if (rhythm_sets[s][i] == p) pat_set = s;
    return p;
}

static bool key(uint8_t code, bool down, uint64_t now) {
    if (shown_inst() >= 0) return inst_page_key(shown_inst(), code, down, now);
    bool mine = code == KEY_SPACE || code == KEY_TAB || code == '`' || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_UP ||
                code == KEY_DOWN || code == KEY_PGUP || code == KEY_PGDN || code == KEY_HOME || code == KEY_END || code == KEY_CAPS || code == KEY_DELETE;
    if (!down || !mine) return mine;
    switch (code) {
    case KEY_SPACE: if (ui_shift) omni.sync = !omni.sync; else rhythm_play(!rhythm.playing); break;
    case KEY_TAB:   omni_set_auto(!omni.autoplay, now); break;
    case '`':       omni_set_hold(!omni.hold, now); break;
    case KEY_LEFT:  rhythm_select(pattern_step(-1)); break;
    case KEY_RIGHT: rhythm_select(pattern_step(+1)); break;
    case KEY_UP:    if (omni.keyboard) omni.kb_octave = (int8_t)MIN(omni.kb_octave + 1, 1); else omni.octave = (int8_t)MIN(omni.octave + 1, 2); break;
    case KEY_DOWN:  if (omni.keyboard) omni.kb_octave = (int8_t)MAX(omni.kb_octave - 1, -1); else omni.octave = (int8_t)MAX(omni.octave - 1, -2); break;
    case KEY_PGUP:  if (ui_shift) omni_set_transpose(omni.transpose + 1); else if (seq.bpm < 300) seq.bpm += 2; break;
    case KEY_PGDN:  if (ui_shift) omni_set_transpose(omni.transpose - 1); else if (seq.bpm > 40) seq.bpm -= 2; break;
    case KEY_HOME:  if (ui_shift) omni_set_tune(omni.tune - 1); else omni_set_voice((omni.voice + OMNI_VOICES) % (OMNI_VOICES + 1)); break;
    case KEY_END:   if (ui_shift) omni_set_tune(omni.tune + 1); else omni_set_voice((omni.voice + 1) % (OMNI_VOICES + 1)); break;
    case KEY_CAPS:  led_toggle(LED_KEYBOARD, now); break;
    case KEY_DELETE: rhythm.classic = !rhythm.classic; break;
    }
    if (omni.voice < OMNI_VOICES) voice_set = omni.voice / 5;
    return true;
}

const struct page page_play = { tab, true, key, 0, pointer, strum_sound, draw, true, midi, 0, again };
