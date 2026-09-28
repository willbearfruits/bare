/* TOUCH: the circuit board of core/touch.c, played with the touchpad (every finger), the mouse, the keys A S D F /
   Z X C V (a finger on each pad; the longer a key is held, the harder it presses) or MIDI notes. Under it, the short
   history of the Crackle Box (core/lessons.c), as the LINEAGE views have theirs. */
#include "ui.h"
#include "lessons.h"
#include "gfx.h"
#include "keys.h"
#include "touch.h"

enum { F_PAD = 0, F_MOUSE = 5, F_KEY = 6 };                         /* finger slots: touchpad, mouse, a key per pad */
static const char pad_keys[TOUCH_PADS] = { 'a', 's', 'd', 'f', 'z', 'x', 'c', 'v' };
static uint64_t key_since[TOUCH_PADS];                               /* when each key finger went down */
static uint8_t key_firm[TOUCH_PADS];                                 /* pressed with Shift or by MIDI: this firm, at once */
static bool hold;                                                    /* Space: key fingers stay after the key is let go */
static int sel;                                                      /* the knob ←→ turns */
static uint64_t mouse_since;
static struct rect board_px, knob_px[TOUCH_KNOBS];
static int knob_drag = -1;

static void key_finger(int pad, bool down, uint8_t firm, uint64_t now) {
    struct touch_finger *f = &touch.f[F_KEY + pad];
    if (down) {
        int x0, y0, x1, y1; touch_pad_rect(pad, &x0, &y0, &x1, &y1);
        key_since[pad] = now; key_firm[pad] = firm;
        *f = (struct touch_finger){ true, (uint16_t)((x0 + x1) / 2), (uint16_t)((y0 + y1) / 2), (uint8_t)(firm ? firm : 60), 0 };
    } else if (!hold) f->on = false;
}

static bool key(uint8_t code, bool down, uint64_t now) {
    for (int p = 0; p < TOUCH_PADS; p++)
        if (code == (uint8_t)pad_keys[p]) {
            if (down && hold && touch.f[F_KEY + p].on) { touch.f[F_KEY + p].on = false; return true; }   /* held: let go */
            if (down && touch.f[F_KEY + p].on) return true;          /* key repeat */
            key_finger(p, down, down && ui_shift ? 230 : 0, now);
            return true;
        }
    if (!down) return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_TAB || code == KEY_SPACE;
    switch (code) {
    case KEY_UP:    sel = (sel + TOUCH_KNOBS - 1) % TOUCH_KNOBS; return true;
    case KEY_DOWN:  sel = (sel + 1) % TOUCH_KNOBS; return true;
    case KEY_LEFT:  touch.knob[sel] = (uint8_t)MAX(touch.knob[sel] - 2, 0); return true;
    case KEY_RIGHT: touch.knob[sel] = (uint8_t)MIN(touch.knob[sel] + 2, 100); return true;
    case KEY_TAB:   touch.hum60 = !touch.hum60; return true;
    case KEY_SPACE:
        hold = !hold;
        if (!hold) for (int p = 0; p < TOUCH_PADS; p++) touch.f[F_KEY + p].on = false;   /* hold off: the held fingers lift */
        return true;
    }
    return false;
}

/* MIDI: note n puts a finger on pad n mod 8, as firm as the velocity; note off lifts it */
static bool midi(uint8_t note, uint8_t vel, uint64_t now) {
    int pad = note % TOUCH_PADS;
    if (vel) key_finger(pad, true, (uint8_t)(40 + vel * 215 / 127), now);
    else key_finger(pad, false, 0, now);
    return true;
}

/* touchpad pressure: a Synaptics pad's z runs about 25 (a breath) to 130 (pressed); pads that can't tell send 60 */
static uint8_t pad_press(const struct finger *f) {
    if (f->size) return (uint8_t)CLAMP(f->size * 2, 30, 255);
    return (uint8_t)CLAMP((f->z - 25) * 255 / 100, 0, 255);
}

static void pointer(uint64_t now) {
    for (int k = 0; k < FINGERS; k++) {                              /* every finger on the pad, the pad as the board */
        struct touch_finger *t = &touch.f[F_PAD + k];
        const struct finger *f = &pad.f[k];
        t->on = f->on;
        if (f->on) { t->x = (uint16_t)f->x; t->y = (uint16_t)f->y; t->press = pad_press(f); t->size = (uint8_t)f->size; }
    }
    for (int p = 0; p < TOUCH_PADS; p++) {                          /* a held key presses harder over a second */
        struct touch_finger *t = &touch.f[F_KEY + p];
        if (t->on && !key_firm[p]) t->press = (uint8_t)MIN(60 + (now - key_since[p]) * 170 / 1200, 230u);
    }
    struct touch_finger *m = &touch.f[F_MOUSE];                      /* the mouse: a finger where it is clicked */
    if (ptr.pressed) {
        for (int i = 0; i < TOUCH_KNOBS; i++) if (ui_in(knob_px[i], ptr.x, ptr.y)) { knob_drag = i; sel = i; }
        if (knob_drag < 0 && ui_in(board_px, ptr.x, ptr.y)) mouse_since = now;
    }
    if (knob_drag >= 0) {
        struct rect k = knob_px[knob_drag];
        if (ptr.down) touch.knob[knob_drag] = (uint8_t)CLAMP((ptr.x - k.x) * 100 / MAX(k.w - 1, 1), 0, 100);
        else knob_drag = -1;
        m->on = false;
        return;
    }
    bool in = ui_in(board_px, ptr.x, ptr.y) && board_px.w > 0;
    if ((ptr.down || (ptr.buttons & 2)) && in) {
        m->on = true;
        m->x = (uint16_t)CLAMP((ptr.x - board_px.x) * 32768 / board_px.w, 0, 32767);
        m->y = (uint16_t)CLAMP((ptr.y - board_px.y) * 32768 / board_px.h, 0, 32767);
        m->press = (ptr.buttons & 2) ? 230 : (uint8_t)MIN(60 + (now - mouse_since) * 170 / 1200, 230u);
        m->size = 0;
    } else m->on = false;
}

/* ---- drawing ---- */
static int bx(struct rect r, int x) { return r.x + (int)((int64_t)x * r.w / 32768); }
static int by(struct rect r, int y) { return r.y + (int)((int64_t)y * r.h / 32768); }
static int finger_r(const struct touch_finger *f) { return f->size ? 1200 + f->size * 2300 / 255 : 1400 + f->press * 1600 / 255; }

static int glow_step(int p) { return touch.sounding ? MIN(touch.glow[p] / 2048, 3) : 0; }   /* 0 dark, 1-3 lit */

static void board(struct rect r) {
    const struct font *fn = text_font();
    /* the op-amp between the rows: a chip with its legs, the traces to the pads faint behind it */
    int gy = by(r, 16384), cw = MIN(r.w / 6, 12 * fn->width), ch = MAX(r.h / 20, fn->height + 4);
    for (int p = 0; p < TOUCH_PADS; p++) {
        int x0, y0, x1, y1; touch_pad_rect(p, &x0, &y0, &x1, &y1);
        int px = bx(r, (x0 + x1) / 2), py = p < 4 ? by(r, y1) : by(r, y0);
        gfx_line(px, py, px, gy, ramp(R_AMBER, 3)); gfx_line(px, gy, r.x + r.w / 2, gy, ramp(R_AMBER, 3));
    }
    gfx_round(r.x + r.w / 2 - cw / 2, gy - ch / 2, cw, ch, 2, C_BLACK, ramp(R_GRAY, 6));
    gfx_text(r.x + r.w / 2 - gfx_text_width("709", fn, 1) / 2, gy - fn->height / 2, "709", fn, ramp(R_GRAY, 9), -1, 1);
    for (int p = 0; p < TOUCH_PADS; p++) {                          /* the pads: copper, their edge lit by the current through them */
        int x0, y0, x1, y1; touch_pad_rect(p, &x0, &y0, &x1, &y1);
        int g = glow_step(p), X0 = bx(r, x0), Y0 = by(r, y0), W = bx(r, x1) - X0, H = by(r, y1) - Y0, t = MAX(2, MIN(W, H) / 24);
        gfx_round(X0, Y0, W, H, MIN(W, H) / 8, g ? ramp(R_AMBER, 8 + g * 2) : ramp(R_AMBER, 6), -1);
        gfx_round(X0 + t, Y0 + t, W - 2 * t, H - 2 * t, MAX(1, MIN(W, H) / 8 - t), ramp(R_AMBER, 4), ramp(R_AMBER, 7));
        const char *nm = touch_pad_names[p];
        gfx_text(X0 + (W - gfx_text_width(nm, fn, 1)) / 2, Y0 + (H - fn->height) / 2, nm, fn, ramp(R_AMBER, 13), -1, 1);
    }
    int last_x = -1, last_y = -1;                                    /* the fingers, and the body between them */
    for (int k = 0; k < TOUCH_FINGERS; k++) {
        const struct touch_finger *f = &touch.f[k];
        if (!f->on) continue;
        int fx = bx(r, f->x), fy = by(r, f->y);
        if (last_x >= 0) gfx_line(last_x, last_y, fx, fy, ramp(R_PINK, 6));
        last_x = fx; last_y = fy;
    }
    for (int k = 0; k < TOUCH_FINGERS; k++) {
        const struct touch_finger *f = &touch.f[k];
        if (!f->on) continue;
        int fx = bx(r, f->x), fy = by(r, f->y), rad = MAX(3, (int)((int64_t)finger_r(f) * r.w / 32768));
        uint8_t col = ramp(R_CYAN, 8 + f->press * 7 / 255);            /* a ring, so the pad's name shows through */
        for (int t = 0; t < MAX(2, rad / 10); t++) gfx_ring(fx, fy, rad - t, col);
    }
}

static void knobs(int x, int y, int w) {
    for (int i = 0; i < TOUCH_KNOBS; i++) {
        int ry = y + i * 2;
        bool on = i == sel;
        char v[24];
        if (i == TK_HUM) snfmt(v, sizeof v, "%3d  %s", touch.knob[i], touch.hum60 ? "60 Hz" : "50 Hz");
        else snfmt(v, sizeof v, "%3d", touch.knob[i]);
        text_put(x, ry, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(x + 2, ry, touch_knob_names[i], on ? C_BRIGHT : C_TEXT, C_PANEL);
        text_str(x + 11, ry, v, on ? C_AMBER : C_DIM, C_PANEL);
        ui_bar(x + 2, ry + 1, w - 4, touch.knob[i], 100, on ? C_AMBER : C_GREEN, C_PANEL);
        knob_px[i] = text_rect(x + 2, ry + 1, w - 4, 1);
    }
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    int hh = ui_lesson_rows(), hy = rows - 1 - hh;                     /* the history band, just above the footer */
    int sw = 30, x = 2, y = 2, w = cols - 4 - sw - 1, sh = rows >= 45 ? 6 : 4, h = hy - y - sh - 1;
    ui_panel(x, y, w, h, "BOARD · two pads at once: your body closes the circuit", C_AMBER);
    uint32_t key = ui_hash_int(UI_HASH0, cols << 16 | rows);          /* the picture: redrawn when something in it moves */
    for (int k = 0; k < TOUCH_FINGERS; k++) {
        const struct touch_finger *f = &touch.f[k];
        if (f->on) key = ui_hash_int(key, k << 24 | (f->x >> 7) << 16 | (f->y >> 7) << 8 | f->press >> 4);
    }
    for (int p = 0; p < TOUCH_PADS; p++) key = ui_hash_int(key, glow_step(p));
    struct rect c;
    if (ui_canvas_keyed(&c, x + 1, y + 1, w - 2, h - 2, C_BG, key)) board(c);
    board_px = c;
    int kx = x + w + 1;
    ui_panel(kx, y, sw, h, "CIRCUIT", C_GREEN);
    knobs(kx + 1, y + 2, sw - 2);
    int ly = y + 2 + TOUCH_KNOBS * 2 + 1;
    if (ly < y + h - 2) ui_led(kx + 3, ly, hold, C_GREEN, "HOLD", C_PANEL);
    if (ly + 2 < y + h - 1) {
        int n = 0; for (int k = 0; k < TOUCH_FINGERS; k++) n += touch.f[k].on;
        char s[32]; snfmt(s, sizeof s, n == 1 ? "1 finger" : "%d fingers", n);
        text_str(kx + 3, ly + 2, s, n ? C_CYAN : C_DIM, C_PANEL);
    }
    static const char *const tries[] = { "OUT + IN-: a tone", "press harder: higher", "one finger on a gap", "IN- alone, hum up",
                                         "IN+ with +9V: it sticks", "add COMP: softer, higher", "add 0V: lower, rougher",
                                         "OUT + C1 + C2: it wobbles" };
    for (int i = 0, ty = ly + 4; i < (int)ARRAY_LEN(tries) && ty < y + h - 1; i++, ty++) {
        if (i == 0) { text_str(kx + 3, ty++, "try", C_GREEN, C_PANEL); if (ty >= y + h - 1) break; }
        text_str_n(kx + 3, ty, tries[i], sw - 5, C_DIM, C_PANEL);
    }
    int sy = y + h + 1;
    ui_panel(x, sy, cols - 4, sh, "OUT", C_SCOPE);
    ui_scope(x + 1, sy + 1, cols - 6, sh - 2);
    ui_lesson(x, hy, cols - 4, hh, &lesson_touch);
    FOOTER("A S D F", "top pads", "Z X C V", "bottom pads", "↑ ↓", "knob", "← →", "turn it", "TAB", "50/60 Hz", "SPACE", "hold",
           "", "a key pressed longer presses harder; ⇧ presses hard at once");
}

const struct page page_touch = { "TOUCH", false, key, 0, pointer, 0, draw, true, midi };
