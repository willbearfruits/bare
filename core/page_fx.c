/* FX: effects played live (core/perf.c), on everything heard or on the input alone. A pad for the selected effect's
   two settings — the touchpad (a second finger plays the filter), the mouse, or ←→ and PgUp PgDn — and the keys 1-8
   to hold an effect, Shift to latch it. Above, the ring of the last seconds the effects read from. */
#include "ui.h"
#include "gfx.h"
#include "keys.h"
#include "perf.h"
#include "seq.h"
#include "mix.h"
#include "platform.h"

static bool key_down[PERF_FX];
static int8_t touched[2] = { -1, -1 };                   /* what each finger (0: the pad's first or the mouse, 1: a second) plays */
static struct rect pad_px, list_px[PERF_FX];
#define TRAIL 24
static int16_t trail_x[TRAIL], trail_y[TRAIL]; static int trail_n;
static const uint8_t fx_ramp[PERF_FX] = { R_CYAN, R_AMBER, R_PINK, R_RED, R_GREEN, R_BLUE, R_AMBER, R_GRAY };

static int second(void) { return perf.sel == PF_FILTER ? PF_CRUSH : PF_FILTER; }
static void let_go(int k) { if (k >= 0 && !perf.latched[k] && !key_down[k] && touched[0] != k && touched[1] != k) perf.on[k] = false; }

static bool key(uint8_t code, bool down, uint64_t now) {
    (void)now;
    if (code >= '1' && code <= '8') {
        int k = code - '1';
        if (down && ui_shift) { perf.latched[k] = !perf.latched[k]; perf.on[k] = perf.latched[k] || key_down[k]; perf.sel = (int8_t)k; return true; }
        if (down && !key_down[k]) { key_down[k] = true; perf.on[k] = true; perf.sel = (int8_t)k; }
        if (!down) { key_down[k] = false; let_go(k); }
        return true;
    }
    if (!down) return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_PGUP || code == KEY_PGDN ||
                      code == KEY_SPACE || code == KEY_TAB || code == '0' || code == KEY_BACKSPACE;
    int s = perf.sel;
    switch (code) {
    case KEY_UP:    perf.sel = (int8_t)((s + PERF_FX - 1) % PERF_FX); return true;
    case KEY_DOWN:  perf.sel = (int8_t)((s + 1) % PERF_FX); return true;
    case KEY_LEFT:  perf.x[s] = (int16_t)MAX(0, perf.x[s] - 25); return true;
    case KEY_RIGHT: perf.x[s] = (int16_t)MIN(1000, perf.x[s] + 25); return true;
    case KEY_PGUP:  perf.y[s] = (int16_t)MIN(1000, perf.y[s] + 50); return true;
    case KEY_PGDN:  perf.y[s] = (int16_t)MAX(0, perf.y[s] - 50); return true;
    case KEY_SPACE: {                                     /* hold: latch what sounds; again: let all latches go */
        bool any = false; for (int k = 0; k < PERF_FX; k++) any |= perf.latched[k];
        for (int k = 0; k < PERF_FX; k++) { if (any) { perf.latched[k] = false; let_go(k); } else if (perf.on[k]) perf.latched[k] = true; }
        return true; }
    case '0': case KEY_BACKSPACE:                         /* everything off */
        for (int k = 0; k < PERF_FX; k++) { perf.latched[k] = false; perf.on[k] = false; }
        return true;
    case KEY_TAB: perf.source = (int8_t)(perf.source == PERF_ALL ? PERF_INPUT : PERF_ALL); return true;
    }
    return false;
}

/* MIDI: notes 36-43 (C2-G2, a drum pad's first row) hold the effects 1-8 */
static bool midi(uint8_t note, uint8_t vel, uint64_t now) {
    (void)now;
    if (note < 36 || note > 43) return false;
    int k = note - 36;
    if (vel) { key_down[k] = true; perf.on[k] = true; perf.sel = (int8_t)k; }
    else { key_down[k] = false; let_go(k); }
    return true;
}

static void finger(int who, int k, bool on, int x, int y) {   /* x, y: 0..32767 across the pad, 0,0 top left */
    if (!on) { int was = touched[who]; touched[who] = -1; let_go(was); return; }
    if (touched[who] != k) { int was = touched[who]; touched[who] = (int8_t)k; let_go(was); }
    perf.x[k] = (int16_t)CLAMP(x * 1000 / 32767, 0, 1000); perf.y[k] = (int16_t)CLAMP(1000 - y * 1000 / 32767, 0, 1000);
    perf.on[k] = true;
    if (who == 0) {
        if (trail_n < TRAIL) trail_n++;
        memmove(trail_x + 1, trail_x, sizeof trail_x[0] * (TRAIL - 1)); memmove(trail_y + 1, trail_y, sizeof trail_y[0] * (TRAIL - 1));
        trail_x[0] = perf.x[k]; trail_y[0] = perf.y[k];
    }
}

static void pointer(uint64_t now) {
    (void)now;
    if (ptr.pressed) for (int k = 0; k < PERF_FX; k++) if (ui_in(list_px[k], ptr.x, ptr.y)) perf.sel = (int8_t)k;
    bool mouse = ptr.down && pad_px.w > 0 && ui_in(pad_px, ptr.x, ptr.y);
    if (pad.f[0].on) finger(0, perf.sel, true, pad.f[0].x, pad.f[0].y);
    else if (mouse) finger(0, perf.sel, true, (ptr.x - pad_px.x) * 32767 / MAX(1, pad_px.w - 1), (ptr.y - pad_px.y) * 32767 / MAX(1, pad_px.h - 1));
    else { finger(0, perf.sel, false, 0, 0); trail_n = 0; }
    finger(1, second(), pad.f[1].on, pad.f[1].x, pad.f[1].y);
}

/* ---- drawing ---- */
/* the ring: its peaks laid round from left to right, the write head moving along; what the effects read, marked */
static void strip(struct rect r, const struct perf_view *v) {
    int mid = r.y + r.h / 2, amp = r.h / 2 - 1;
    uint32_t bl = MAX(1u, v->blocks);
    for (int c = 0; c < r.w; c++) {
        uint32_t b0 = (uint32_t)c * bl / (uint32_t)r.w, b1 = MAX(b0 + 1, (uint32_t)(c + 1) * bl / (uint32_t)r.w);
        int pk = 0; for (uint32_t b = b0; b < b1 && b < bl; b++) pk = MAX(pk, v->peaks[b]);
        uint32_t back = (v->w_block + bl - b0) % bl;                         /* blocks behind the head */
        uint8_t col = ramp(R_GREEN, back < bl / 16 ? 12 : back < bl / 4 ? 9 : 6);
        uint32_t fb = back * 256;                                            /* frames behind the head */
        if (v->rep_len && fb <= v->rep_from && fb + 256 > v->rep_from - v->rep_len) col = ramp(R_AMBER, 13);
        if (v->rev_len && fb <= v->rev_from && fb + 256 > v->rev_from - v->rev_len) col = ramp(R_PINK, 13);
        int h = pk * amp / 255;
        gfx_vline(r.x + c, mid - h, 2 * h + 1, col);
    }
    int hx = r.x + (int)(v->w_block * (uint32_t)r.w / bl);
    gfx_vline(hx, r.y, r.h, v->frozen ? ramp(R_AMBER, 15) : C_BRIGHT);
    if (v->tape_at) { int tx = r.x + (int)(((v->w_block + bl) * 256 - v->tape_at) / 256 % bl * (uint32_t)r.w / bl); gfx_vline(tx, r.y, r.h, ramp(R_RED, 14)); }
}

/* the pad: a grid, the selected effect's zones (the filter's middle is off; lengths are steps), its point and trail */
static void pad_picture(struct rect r, const struct perf_view *v) {
    const struct font *f = text_font();
    int k = perf.sel, rp = fx_ramp[k];
    bool lit = perf.on[k];
    for (int i = 1; i < 8; i++) { gfx_vline(r.x + r.w * i / 8, r.y, r.h, ramp(R_PANEL, 3)); gfx_hline(r.x, r.y + r.h * i / 8, r.w, ramp(R_PANEL, 3)); }
    static const int zones[PERF_FX] = { 0, 5, 4, 0, 6, 0, 5, 3 };
    static const char *const zone_names[PERF_FX][6] = { { 0 }, { "1/2", "1/4", "1/8", "1/16", "1/32" }, { "1/4", "1/2", "1", "2" }, { 0 },
        { "1/4", "1/8", "1/12", "1/16", "1/24", "1/32" }, { 0 }, { "1/16", "1/8", "1/8.", "1/4", "1/4." }, { "dark", "warm", "bright" } };
    if (k == PF_FILTER) {
        int a = r.x + r.w * 45 / 100, b = r.x + r.w * 55 / 100;
        gfx_fill(a, r.y, b - a, r.h, ramp(R_PANEL, 2));
        gfx_text(r.x + f->width, r.y + r.h - f->height - 2, "low-pass", f, ramp(rp, 8), -1, 1);
        gfx_text((a + b - gfx_text_width("off", f, 1)) / 2, r.y + r.h - f->height - 2, "off", f, ramp(R_GRAY, 6), -1, 1);
        gfx_text(r.x + r.w - f->width - gfx_text_width("high-pass", f, 1), r.y + r.h - f->height - 2, "high-pass", f, ramp(rp, 8), -1, 1);
    } else if (zones[k]) {
        for (int z = 0; z < zones[k]; z++) {
            int zx = r.x + r.w * z / zones[k];
            if (z) for (int yy = r.y; yy < r.y + r.h; yy += 4) gfx_vline(zx, yy, 2, ramp(rp, 5));
            const char *nm = zone_names[k][z];
            gfx_text(zx + (r.w / zones[k] - gfx_text_width(nm, f, 1)) / 2, r.y + r.h - f->height - 2, nm, f, ramp(rp, 9), -1, 1);
        }
    }
    gfx_text(r.x + f->width, r.y + f->height / 2, perf_names[k], f, ramp(rp, lit ? 14 : 8), -1, 2);
    char yl[40]; snfmt(yl, sizeof yl, "↑ %s", perf_y_names[k]);
    gfx_text(r.x + f->width, r.y + f->height * 5 / 2 + f->height / 2, yl, f, ramp(R_GRAY, 7), -1, 1);
    /* the trail, then the point: a ring, filled while it sounds */
    for (int t = trail_n - 1; t >= 1; t--) {
        int tx = r.x + trail_x[t] * (r.w - 1) / 1000, ty = r.y + (1000 - trail_y[t]) * (r.h - 1) / 1000;
        gfx_disc(tx, ty, MAX(1, 4 - t / 6), ramp(rp, MAX(2, 11 - t / 3)));
    }
    int px = r.x + perf.x[k] * (r.w - 1) / 1000, py = r.y + (1000 - perf.y[k]) * (r.h - 1) / 1000, rad = MAX(6, r.w / 45);
    if (lit) gfx_disc(px, py, rad, ramp(rp, 10 + MIN(5, v->level / 3000)));
    for (int t = 0; t < MAX(2, rad / 5); t++) gfx_ring(px, py, rad + 2 + t, ramp(rp, lit ? 15 : 9));
    int k2 = second();
    if (touched[1] >= 0) {                                                 /* the second finger's filter (or crush) */
        int qx = r.x + perf.x[k2] * (r.w - 1) / 1000, qy = r.y + (1000 - perf.y[k2]) * (r.h - 1) / 1000;
        for (int t = 0; t < 3; t++) gfx_ring(qx, qy, rad - 2 + t, ramp(fx_ramp[k2], 14));
    }
    bool any = false; for (int j = 0; j < PERF_FX; j++) any |= perf.on[j];
    if (any) for (int t = 0; t < 3; t++) gfx_frame(r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t, ramp(rp, MIN(15, 6 + v->level / 2500 - t * 2)));
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    struct perf_view v; perf_view(&v);
    int x = 2, y = 2, sh = 6;
    uint32_t bpm_x10 = seq_link_q16 ? (uint32_t)(((uint64_t)seq_link_q16 * 10 + 32768) >> 16) : (uint32_t)(seq.bpm ? seq.bpm : 120) * 10;
    char t[96];
    uint32_t ms = perf_ring_frames() / 48;                                         /* at 48 kHz */
    snfmt(t, sizeof t, "RING · the last %u.%u s · %u.%u BPM%s", ms / 1000, ms / 100 % 10, bpm_x10 / 10, bpm_x10 % 10, v.frozen ? " · held by the repeat" : "");
    ui_panel(x, y, cols - 4, sh, t, C_GREEN);
    uint32_t key = ui_hash_int(UI_HASH0, (int32_t)v.w_block);
    key = ui_hash_int(key, (int32_t)(v.rep_len ^ v.rev_len << 1 ^ v.tape_at >> 8 ^ (uint32_t)v.frozen << 31));
    struct rect s;
    if (perf_ring_frames() && ui_canvas_keyed(&s, x + 1, y + 1, cols - 6, sh - 2, C_BG, key)) { gfx_clip(s.x, s.y, s.w, s.h); strip(s, &v); gfx_noclip(); }
    int sw = MIN(38, cols / 3), py = y + sh + 1, ph = rows - py - 2, pw = cols - 4 - sw - 1;
    snfmt(t, sizeof t, "PAD · → %s", perf_x_names[perf.sel]);
    ui_panel(x, py, pw, ph, t, (uint8_t)(perf.on[perf.sel] ? C_AMBER : C_CYAN));
    key = ui_hash_int(UI_HASH0, perf.sel << 16 | perf.x[perf.sel]);
    key = ui_hash_int(key, perf.y[perf.sel] << 8 | trail_n);
    for (int k = 0; k < PERF_FX; k++) key = ui_hash_int(key, perf.on[k] << 4 | (touched[1] == k));
    key = ui_hash_int(key, perf.x[second()] << 16 | perf.y[second()]);
    key = ui_hash_int(key, trail_x[0] << 16 | trail_y[0]);
    key = ui_hash_int(key, v.level / 2500);
    struct rect c;
    if (ui_canvas_keyed(&c, x + 1, py + 1, pw - 2, ph - 2, C_BG, key)) { gfx_clip(c.x, c.y, c.w, c.h); pad_picture(c, &v); gfx_noclip(); }   /* a point on the edge stays inside */
    pad_px = c;
    /* the effects */
    int lx = x + pw + 1;
    ui_panel(lx, py, sw, ph, perf.source == PERF_ALL ? "EFFECTS · on everything" : "EFFECTS · on the input", C_PINK);
    for (int k = 0; k < PERF_FX; k++) {
        int ry = py + 2 + k * 2;
        if (ry >= py + ph - 2) break;
        bool sel = k == perf.sel;
        char kn[4] = { (char)('1' + k), 0 };
        text_put(lx + 1, ry, sel ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(lx + 3, ry, kn, sel ? C_AMBER : C_DIM, C_PANEL);
        text_str(lx + 5, ry, perf_names[k], sel ? C_BRIGHT : C_TEXT, C_PANEL);
        text_put(lx + 16, ry, perf.on[k] ? G_DISC : G_CIRCLE, perf.on[k] ? ramp(fx_ramp[k], 14) : C_DIM, C_PANEL);
        if (perf.latched[k]) text_str(lx + 18, ry, "LATCH", C_GREEN, C_PANEL);
        int bw = MAX(4, (sw - 7) / 2 - 1);
        ui_bar(lx + 5, ry + 1, bw, perf.x[k], 1000, sel ? ramp(fx_ramp[k], 12) : ramp(R_GRAY, 6), C_PANEL);
        ui_bar(lx + 6 + bw, ry + 1, bw, perf.y[k], 1000, sel ? ramp(fx_ramp[k], 9) : ramp(R_GRAY, 5), C_PANEL);
        list_px[k] = text_rect(lx + 1, ry, sw - 2, 2);
    }
    int iy = py + 2 + PERF_FX * 2;
    if (iy < py + ph - 2) { perf_value(perf.sel, t, sizeof t); text_str_n(lx + 3, iy, t, sw - 5, ramp(fx_ramp[perf.sel], 13), C_PANEL); }
    if (iy + 2 < py + ph - 1) {
        const char *in = "none chosen";
        const char *names[8]; int n = plat_audio_inputs(names, 8);
        if (mix.input >= 0 && mix.input < n) in = names[mix.input];
        text_str_n(lx + 3, iy + 2, perf.source == PERF_ALL ? "TAB: on the input only" : "TAB: on everything", sw - 5, C_DIM, C_PANEL);
        snfmt(t, sizeof t, mix.input >= 0 ? "input: %s" : "input: none (MIX picks it)", in);
        if (iy + 3 < py + ph - 1) text_str_n(lx + 3, iy + 3, t, sw - 5, perf.source == PERF_INPUT ? C_TEXT : C_DIM, C_PANEL);
    }
    static const char *const tries[] = { "hold 2, slide right: faster", "2, then a 2nd finger: filter it", "7 at a phrase's end: a throw",
                                         "4 as the song stops", "8 and play over the frozen room" };
    for (int i = 0, ty = iy + 5; i < (int)ARRAY_LEN(tries) && ty < py + ph - 1; i++, ty++) {
        if (i == 0) { text_str(lx + 3, ty++, "try", C_GREEN, C_PANEL); if (ty >= py + ph - 1) break; }
        text_str_n(lx + 3, ty, tries[i], sw - 5, C_DIM, C_PANEL);
    }
    FOOTER("1-8", "hold an effect", "⇧1-8", "latch", "↑ ↓", "pick", "← →", "its X", "PGUP PGDN", "its Y", "SPACE", "hold",
           "0", "all off", "TAB", "everything / the input", "", "the pad plays the one picked; a 2nd finger, the filter");
}

const struct page page_fx = { "FX", "F10", KEY_F10, false, key, 0, pointer, 0, draw, true, midi };
