/* CLOUDS (XENAKIS, a view): the four clouds of core/cloud.c. The picture is a score written as it sounds: a head sweeps
   across eight seconds, each note a line from its pitch along its glissando in its cloud's colour, the page cleared
   just ahead of the head (only what changes is drawn, the way a slow screen likes it). 1-4 switch the clouds on and off;
   the knobs set the one chosen (Tab). Played by hand: each finger on the touchpad plays a cloud while it stays down
   (up and down, where its band is; across, how dense), the mouse the chosen one; a key of the letter rows or a MIDI
   note centres the chosen cloud's band on its note while held. Enter writes the chosen cloud onto UPIC's page. */
#include "xen.h"
#include "harmony.h"
#include "cloud.h"
#include "synth.h"
#include "gfx.h"
#include "keys.h"
#include "undo.h"
#include "upic.h"
#include "sieve.h"
#include "seq.h"

enum { K_SOUND, K_DENSITY, K_LOW, K_HIGH, K_SHAPE, K_LENGTH, K_SPREAD, K_GLIDE, K_LEVEL, K_DYN, K_WIDTH, K_PITCH, K_RHYTHM, KN };
static const char *const knob_names[KN] = { "sound", "density", "low", "high", "shape", "length", "spread", "glide", "level",
                                            "dynamics", "width", "pitch", "rhythm" };
static const uint8_t cramp[CLOUDS] = { R_AMBER, R_CYAN, R_PINK, R_GREEN };
static int sel, knob = K_DENSITY, octave = 3;
static int held_note = -1; static uint8_t held_keys[128];
static bool pad_on[CLOUDS];                                /* a finger switched this cloud on */
static struct rect score_px;
static uint32_t drawn_to;                                  /* the clouds' clock the picture reaches */
#define WINDOW_S 8
#define LO_NOTE 24
#define HI_NOTE 108

static const char *sieve_name(int s) { static const char *const n[5] = { "free", "S1", "S2", "S3", "S4" }; return n[s + 1]; }
static void note_text(int n, char *out, int cap) {
    static const char *const nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    snfmt(out, cap, "%s%d", nm[n % 12], n / 12 - 1);
}
static void knob_text(const struct cloud *c, int k, char *out, int cap) {
    switch (k) {
    case K_SOUND:   snfmt(out, cap, "%s", synth_preset_name(c->sound)); break;
    case K_DENSITY: { uint32_t x = cloud_per_second_x100(c); if (x >= 1000) snfmt(out, cap, "%u a second", (x + 50) / 100); else snfmt(out, cap, "%u.%u a second", x / 100, x % 100 / 10); break; }
    case K_LOW:     note_text(c->low, out, cap); break;
    case K_HIGH:    note_text(c->high, out, cap); break;
    case K_SHAPE:   snfmt(out, cap, "%s", c->shape ? "bunched" : "even"); break;
    case K_LENGTH:  { uint32_t ms = cloud_length_ms(c); if (ms >= 1000) snfmt(out, cap, "%u.%u s", ms / 1000, ms % 1000 / 100); else snfmt(out, cap, "%u ms", ms); break; }
    case K_SPREAD:  snfmt(out, cap, "%d", c->spread); break;
    case K_GLIDE:   if (c->glide) snfmt(out, cap, "%d", c->glide); else snfmt(out, cap, "none"); break;
    case K_LEVEL:   snfmt(out, cap, "%d", c->level); break;
    case K_DYN:     snfmt(out, cap, "%d", c->dyn); break;
    case K_WIDTH:   snfmt(out, cap, "%d", c->width); break;
    case K_PITCH:   snfmt(out, cap, "%s", c->pitch_sieve < 0 ? "free" : sieve_name(c->pitch_sieve)); break;
    default:        snfmt(out, cap, "%s", c->rhythm_sieve < 0 ? "free" : sieve_name(c->rhythm_sieve)); break;
    }
}
static void turn(struct cloud *c, int k, int d, bool big) {
    int s = big ? 10 : 1;
    switch (k) {
    case K_SOUND:   c->sound = (uint8_t)synth_preset_next(c->sound, d); break;
    case K_DENSITY: c->density = (uint8_t)CLAMP(c->density + d * (big ? 10 : 2), 0, 100); break;
    case K_LOW:     c->low = (uint8_t)CLAMP(c->low + d * (big ? 12 : 1), 12, c->high); break;
    case K_HIGH:    c->high = (uint8_t)CLAMP(c->high + d * (big ? 12 : 1), c->low, 120); break;
    case K_SHAPE:   c->shape ^= 1; break;
    case K_LENGTH:  c->length = (uint8_t)CLAMP(c->length + d * (big ? 10 : 2), 0, 100); break;
    case K_SPREAD:  c->spread = (uint8_t)CLAMP(c->spread + d * s * 2, 0, 100); break;
    case K_GLIDE:   c->glide = (uint8_t)CLAMP(c->glide + d * s * 2, 0, 100); break;
    case K_LEVEL:   c->level = (uint8_t)CLAMP(c->level + d * s * 2, 1, 127); break;
    case K_DYN:     c->dyn = (uint8_t)CLAMP(c->dyn + d * s * 2, 0, 100); break;
    case K_WIDTH:   c->width = (uint8_t)CLAMP(c->width + d * s * 2, 0, 100); break;
    case K_PITCH:   c->pitch_sieve = (int8_t)((c->pitch_sieve + 1 + d + 5) % 5 - 1); break;
    default:        c->rhythm_sieve = (int8_t)((c->rhythm_sieve + 1 + d + 5) % 5 - 1); break;
    }
}
static void edit(uint64_t now) { char w[16]; snfmt(w, sizeof w, "cloud %c", 'A' + sel); undo_one(U_CLOUD, sel, w, now); }
static void set_on(int k, bool on) { clouds[k].on = on; if (!on) cloud_stop(k); }
static void centre(struct cloud *c, int note) {           /* the band moved to be centred on a note, its width kept */
    int w = c->high - c->low, lo = CLAMP(note - w / 2, 12, 120 - w);
    c->low = (uint8_t)lo; c->high = (uint8_t)(lo + w);
}

/* the chosen cloud written onto UPIC's page: a page's worth of its notes, each an arc (sloping where it glides); a
   cloud on a rhythm sieve lands on the page's sixteenths the sieve keeps */
static int write_upic(int k) {
    static uint32_t rng = 0x57524954u;
    const struct cloud *c = &clouds[k];
    uint32_t fpp = upic_frames_per_page(), rate = synth_rate(), steps = 0;
    int arcs = 0;
    if (c->rhythm_sieve >= 0) {                              /* sixteenths of the tempo across the page */
        uint32_t bpm = seq.bpm ? seq.bpm : 120;
        steps = upic.bars ? upic.bars * 16u : (uint32_t)upic.seconds * bpm * 4 / 60;
    }
    uint32_t f = steps ? 0 : cloud_wait(k, &rng, rate);
    for (uint32_t s = 0; steps ? s < steps : f < fpp; s++) {
        int count = 1;
        if (steps) {                                         /* on the sieve's steps: as many as the density gives */
            if (!sieve_has(&sieves[c->rhythm_sieve], (int32_t)s)) continue;
            uint32_t x100 = cloud_per_second_x100(c) * (fpp / steps) / rate;
            count = (int)(x100 / 100) + ((rng = rng * 1664525u + 1013904223u) % 100 < x100 % 100);
            f = (uint32_t)((uint64_t)s * fpp / steps);
        }
        for (int i = 0; i < count; i++) {
            struct cloud_note d; cloud_draw(k, &rng, &d);
            uint32_t len = d.ms * (rate / 1000), t0 = (uint32_t)((uint64_t)f * 65535 / fpp), t1 = (uint32_t)(MIN((uint64_t)f + len, fpp) * 65535 / fpp);
            int32_t p1 = CLAMP(d.pitch + d.glide * (int32_t)d.ms / 1000, 0, 127 * 256);
            struct upic_pt pt[2] = { { (uint16_t)t0, (uint16_t)d.pitch }, { (uint16_t)MAX(t1, t0 + 1), (uint16_t)p1 } };
            if (upic_add(pt, 2, c->sound, (uint8_t)d.vel) < 0) return -arcs - 1;
            arcs++;
        }
        if (!steps) f += cloud_wait(k, &rng, rate);
    }
    return arcs;
}

static bool key(uint8_t code, bool down, uint64_t now) {
    int n = code >= '0' && code <= '4' ? -1 : xen_key_note(code, octave);   /* here 1-4 and 0 are the clouds' */
    if (n >= 0) {                                          /* the chosen cloud around this note, while held */
        if (down == (held_keys[n & 127] != 0)) return true;
        held_keys[n & 127] = down;
        if (down) { centre(&clouds[sel], harmony_note(n)); if (!clouds[sel].on) { set_on(sel, true); held_note = n; } }
        else if (held_note == n) { set_on(sel, false); held_note = -1; }
        return true;
    }
    if (!down) return (code >= '0' && code <= '4') || code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT ||
                      code == '[' || code == ']' || code == KEY_TAB || code == KEY_SPACE || code == KEY_PGUP || code == KEY_PGDN || code == KEY_ENTER;
    struct cloud *c = &clouds[sel];
    switch (code) {
    case '1': case '2': case '3': case '4': set_on(code - '1', !clouds[code - '1'].on); return true;
    case '0': for (int k = 0; k < CLOUDS; k++) set_on(k, false); return true;
    case KEY_SPACE: set_on(sel, !c->on); return true;
    case KEY_TAB:   sel = (sel + 1) % CLOUDS; return true;
    case KEY_UP:    knob = (knob + KN - 1) % KN; return true;
    case KEY_DOWN:  knob = (knob + 1) % KN; return true;
    case KEY_LEFT: case KEY_RIGHT: case '[': case ']':
        edit(now); turn(c, knob, code == KEY_LEFT || code == '[' ? -1 : 1, code == '[' || code == ']'); return true;
    case KEY_PGUP:  octave = MIN(octave + 1, 7); return true;
    case KEY_PGDN:  octave = MAX(octave - 1, 0); return true;
    case KEY_ENTER: {
        undo_one(U_UPIC, 0, "a cloud written into UPIC", now);
        int n = write_upic(sel);
        char m[48];
        if (n < 0) snfmt(m, sizeof m, "UPIC's page is full (%d arcs written)", -n - 1);
        else snfmt(m, sizeof m, "cloud %c: %d arcs on UPIC's page", 'A' + sel, n);
        ui_notice(m, now);
        return true; }
    }
    return false;
}
static bool midi(uint8_t note, uint8_t vel, uint64_t now) {
    (void)now;
    if (vel) { centre(&clouds[sel], note); if (!clouds[sel].on) { set_on(sel, true); held_note = note; } }
    else if (held_note == note) { set_on(sel, false); held_note = -1; }
    return true;
}

/* played by hand: y the band's place, x the density; a cloud a finger switched on goes off when the finger lifts */
static void steer(int k, int x, int y) {
    centre(&clouds[k], HI_NOTE - (HI_NOTE - LO_NOTE) * y / 32767);
    clouds[k].density = (uint8_t)CLAMP(x * 100 / 32767, 0, 100);
}
static void pointer(uint64_t now) {
    (void)now;
    for (int k = 0; k < CLOUDS && k < FINGERS; k++) {
        if (pad.f[k].on) { steer(k, pad.f[k].x, pad.f[k].y); if (!clouds[k].on) { set_on(k, true); pad_on[k] = true; } }
        else if (pad_on[k]) { set_on(k, false); pad_on[k] = false; }
    }
    static bool dragging; static int mouse_cloud = -1;      /* the mouse on the score: the chosen cloud */
    if (ptr.pressed && score_px.w && ui_in(score_px, ptr.x, ptr.y)) dragging = true;
    if (!ptr.down) dragging = false;
    if (dragging) {
        steer(sel, CLAMP((ptr.x - score_px.x) * 32767 / MAX(1, score_px.w), 0, 32767), CLAMP((ptr.y - score_px.y) * 32767 / MAX(1, score_px.h), 0, 32767));
        if (!clouds[sel].on) { set_on(sel, true); mouse_cloud = sel; }
    } else if (mouse_cloud >= 0) { set_on(mouse_cloud, false); mouse_cloud = -1; }
}

/* ---- the score ---- */
static int px_x(uint32_t t) { uint32_t win = WINDOW_S * synth_rate(); return score_px.x + (int)((t % win) * (uint32_t)score_px.w / win); }
static int px_y(int32_t q8) {
    int32_t lo = LO_NOTE * 256, hi = HI_NOTE * 256;
    return score_px.y + score_px.h - 1 - (CLAMP(q8, lo, hi) - lo) * (score_px.h - 1) / (hi - lo);
}
static void grid_column(int x0, int x1) {                  /* cleared, the octaves' C dotted back in */
    struct rect r = score_px;
    x0 = MAX(x0, r.x); x1 = MIN(x1, r.x + r.w);
    if (x1 <= x0) return;
    gfx_fill(x0, r.y, x1 - x0, r.h, C_BG);
    for (int n = LO_NOTE; n <= HI_NOTE; n += 12) { int y = px_y(n * 256); for (int x = x0 + ((4 - x0 % 4) % 4); x < x1; x += 4) gfx_row(y)[x] = ramp(R_PANEL, 6); }
    gfx_dirty(x0, r.y, x1 - x0, r.h);
}
static void clear_span(uint32_t t0, uint32_t t1) {         /* the page's columns for [t0, t1), wrapping */
    int a = px_x(t0), b = px_x(t1);
    if (b >= a) grid_column(a, b + 1); else { grid_column(a, score_px.x + score_px.w); grid_column(score_px.x, b + 1); }
}
static void mark_piece(const struct cloud_mark *m, uint32_t t0, uint32_t t1) {   /* a note's line within [t0, t1] */
    uint32_t s = MAX(m->at, t0), e = MIN(m->at + m->len, t1), per_ms = synth_rate() / 1000;
    if (e <= s) return;
    int32_t p0 = m->pitch + m->glide * (int32_t)((s - m->at) / per_ms) / 1000, p1 = m->pitch + m->glide * (int32_t)((e - m->at) / per_ms) / 1000;
    uint8_t col = ramp(cramp[m->cloud & 3], 6 + m->vel * 9 / 127);
    int xa = px_x(s), xb = px_x(e), ya = px_y(p0), yb = px_y(p1);
    if (xb >= xa) xen_line(xa, ya, xb, yb, col);
    else {                                                  /* across the wrap: two pieces */
        int xw = score_px.x + score_px.w - 1, yw = ya + (yb - ya) * (xw - xa) / MAX(1, xw - xa + xb - score_px.x);
        xen_line(xa, ya, xw, yw, col); xen_line(score_px.x, yw, xb, yb, col);
    }
}
static void score(struct rect r, bool fresh) {
    uint32_t now = cloud_frames, win = WINDOW_S * synth_rate(), gap = synth_rate() / 4;
    if (fresh || now - drawn_to > win / 2) {                /* the whole page again: the last seconds */
        drawn_to = now > win - gap ? now - (win - gap) : 0;
        clear_span(0, win - 1);
    }
    if (now == drawn_to) return;
    clear_span(drawn_to, now + gap);                        /* where the head was, to ahead of where it is */
    uint32_t n = cloud_mark_n;
    for (uint32_t i = n > CLOUD_MARKS ? n - CLOUD_MARKS : 0; i < n; i++) mark_piece(&cloud_marks[i % CLOUD_MARKS], drawn_to, now);
    int hx = px_x(now) + 1;                                 /* the head: the bands of the clouds that sound */
    for (int k = 0; k < CLOUDS; k++) {
        const struct cloud *c = &clouds[k];
        if (!c->on || hx + 3 + k * 3 >= r.x + r.w) continue;
        int ya = px_y(c->high * 256), yb = px_y(c->low * 256);
        gfx_fill(hx + 2 + k * 3, ya, 2, MAX(1, yb - ya), ramp(cramp[k], 12));
    }
    gfx_fill(hx, r.y, 1, r.h, ramp(R_GRAY, 6));
    gfx_dirty(hx, r.y, 16, r.h);
    drawn_to = now;
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    int sw = 30, x = 2, y = XEN_TOP, pw = cols - 4 - sw - 1, ph = rows - y - 3;
    int n_on = 0; for (int k = 0; k < CLOUDS; k++) n_on += clouds[k].on;
    char t[48]; snfmt(t, sizeof t, "CLOUDS · %d sounding · %d seconds", n_on, WINDOW_S);
    ui_panel(x, y, pw, ph, t, C_AMBER);
    for (int o = 0; o <= (HI_NOTE - LO_NOTE) / 12; o++) {      /* the octaves, down the left */
        char lab[4]; snfmt(lab, sizeof lab, "C%d", LO_NOTE / 12 - 1 + o);
        int ry = y + 1 + (ph - 3) - o * (ph - 3) * 12 / (HI_NOTE - LO_NOTE);
        if (ry > y && ry < y + ph - 1) text_str(x + 1, ry, lab, C_DIM, C_PANEL);
    }
    struct rect r;
    bool fresh = ui_canvas_keyed(&r, x + 4, y + 1, pw - 5, ph - 2, C_BG, ui_hash_int(UI_HASH0, cols << 16 | rows));
    score_px = r;
    score(r, fresh);

    int kx = x + pw + 1, ry = y + 1, lim = y + ph - 1;
    ui_panel(kx, y, sw, ph, "CLOUDS", C_CYAN);
    for (int k = 0; k < CLOUDS && ry < lim; k++, ry++) {
        char nm[32]; snfmt(nm, sizeof nm, "%c %s", 'A' + k, synth_preset_name(clouds[k].sound));
        text_put(kx + 1, ry, k == sel ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        ui_led(kx + 3, ry, clouds[k].on, cramp[k] == R_AMBER ? C_AMBER : cramp[k] == R_CYAN ? C_CYAN : cramp[k] == R_PINK ? C_PINK : C_GREEN, "", C_PANEL);
        text_str_n(kx + 6, ry, nm, sw - 8, k == sel ? C_BRIGHT : C_TEXT, C_PANEL);
    }
    ry++;
    const struct cloud *c = &clouds[sel];
    char head[24]; snfmt(head, sizeof head, "cloud %c", 'A' + sel);
    if (ry < lim) text_str(kx + 3, ry++, head, C_GREEN, C_PANEL);
    for (int k = 0; k < KN && ry < lim; k++, ry++) {
        char v[24]; knob_text(c, k, v, sizeof v);
        bool on = k == knob;
        text_put(kx + 1, ry, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(kx + 3, ry, knob_names[k], on ? C_BRIGHT : C_TEXT, C_PANEL);
        text_str_n(kx + 12, ry, v, sw - 14, on ? C_AMBER : C_DIM, C_PANEL);
    }
    FOOTER("1-4", "clouds on/off", "TAB", "choose", "↑ ↓ ← →", "knobs", "SPACE", "this one", "Z-/ Q-P", "around a note", "0", "all off",
           "ENTER", "onto UPIC's page", "", "the touchpad: a cloud a finger");
}

const struct view xen_cloud = { "CLOUDS", key, 0, pointer, draw, midi, "1956" };
