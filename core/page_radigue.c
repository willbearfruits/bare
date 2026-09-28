/* RADIGUE, a LINEAGE view (after Éliane Radigue): a drone of eight partials (core/drone.c). The picture is a slow score:
   time runs left to right, the last minutes, a column every tenth of a second written at a sweeping head; each partial
   is a line at its pitch, as bright as it sounds with its twin — so the breathing, the beating and the base's glides
   draw themselves as they happen. Under it the partials: harmonic, detune, level, breath, and how fast each pair beats.
   The letter keys send the base to a note (over the sweep's time); a finger on the pad detunes the chosen partial. */
#include "lineage.h"
#include "drone.h"
#include "harmony.h"
#include "lessons.h"
#include "gfx.h"
#include "keys.h"

enum { K_BASE, K_SWEEP, K_FADE, K_DEPTH, K_LEVEL, K_HARM, K_DETUNE, K_PLEVEL, K_BREATH, KNOBS };
static const char *const knob_names[KNOBS] = { "base", "sweep", "fade", "breath", "level", "harmonic", "detune", "level", "breathes" };
static const char *const note_names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char kb[] = "zsxdcvgbhnjm,l.;/";          /* C to E, black keys on the row above, as on WAVE */
static const uint16_t sweeps[] = { 0, 10, 30, 60, 120, 300, 600, 1200, 1800 };
static const uint8_t fades[] = { 1, 2, 5, 10, 20, 30, 60 };
static const uint16_t breaths[] = { 30, 45, 60, 90, 120, 180, 240, 300, 420, 600 };
static int knob, sel, kb_octave = 2;
static struct rect score_px;
static uint64_t col_at; static int head;

static int step_in(const void *list, int n, int size, int v, int d) {   /* the next value of a list of steps */
    int i = 0;
    for (int k = 0; k < n; k++) { int x = size == 1 ? ((const uint8_t *)list)[k] : ((const uint16_t *)list)[k]; if (x <= v) i = k; }
    i = CLAMP(i + d, 0, n - 1);
    return size == 1 ? ((const uint8_t *)list)[i] : ((const uint16_t *)list)[i];
}
static void turn(int k, int d, bool fine) {
    struct drone_partial *p = &radigue.p[sel];
    switch (k) {
    case K_BASE:   drone_sweep_to(radigue.target + d * (fine ? 10 : 1000)); break;
    case K_SWEEP:  radigue.sweep_s = (uint16_t)step_in(sweeps, ARRAY_LEN(sweeps), 2, radigue.sweep_s, d); break;
    case K_FADE:   radigue.fade_s = (uint8_t)step_in(fades, ARRAY_LEN(fades), 1, radigue.fade_s, d); break;
    case K_DEPTH:  radigue.depth = (uint8_t)CLAMP(radigue.depth + d * 5, 0, 100); break;
    case K_LEVEL:  radigue.level = (uint8_t)CLAMP(radigue.level + d * 5, 0, 100); break;
    case K_HARM:   p->harmonic = (uint8_t)CLAMP(p->harmonic + d, 1, 16); break;
    case K_DETUNE: p->detune = (int16_t)CLAMP(p->detune + d * (fine ? 1 : 10), -200, 200); break;
    case K_PLEVEL: p->level = (uint8_t)CLAMP(p->level + d * 5, 0, 100); break;
    case K_BREATH: p->breath_s = (uint16_t)step_in(breaths, ARRAY_LEN(breaths), 2, p->breath_s, d); break;
    }
}
static void base_to_note(int note) { drone_sweep_to(harmony_note(CLAMP(note, 12, 96)) * 1000); }

static bool key(uint8_t code, bool down, uint64_t now) {
    (void)now;
    for (int i = 0; kb[i]; i++) if (code == (uint8_t)kb[i]) { if (down) base_to_note((kb_octave + 1) * 12 + i); return true; }
    if (code >= '1' && code <= '8') { if (down) sel = code - '1'; return true; }
    if (!down) return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_SPACE || code == KEY_PGUP || code == KEY_PGDN || code == KEY_HOME;
    switch (code) {
    case KEY_UP:    knob = (knob + KNOBS - 1) % KNOBS; return true;
    case KEY_DOWN:  knob = (knob + 1) % KNOBS; return true;
    case KEY_LEFT:  turn(knob, -1, ui_shift); return true;
    case KEY_RIGHT: turn(knob, +1, ui_shift); return true;
    case KEY_SPACE: drone_play(!radigue.playing); return true;
    case KEY_PGUP:  kb_octave = MIN(kb_octave + 1, 5); return true;
    case KEY_PGDN:  kb_octave = MAX(kb_octave - 1, 0); return true;
    case KEY_HOME:  drone_defaults(); return true;
    }
    return false;
}
static bool midi(uint8_t note, uint8_t vel, uint64_t now) { (void)now; if (vel) base_to_note(note); return true; }

/* a finger on the pad (or the mouse on the score): the chosen partial's detune across, its level up and down */
static void pointer(uint64_t now) {
    (void)now;
    int x, y;
    if (pad.f[0].on) { x = pad.f[0].x; y = pad.f[0].y; }
    else if (ptr.down && ui_in(score_px, ptr.x, ptr.y)) { x = (ptr.x - score_px.x) * 32767 / MAX(1, score_px.w - 1); y = (ptr.y - score_px.y) * 32767 / MAX(1, score_px.h - 1); }
    else return;
    radigue.p[sel].detune = (int16_t)CLAMP(-200 + CLAMP(x, 0, 32767) * 400 / 32767, -200, 200);
    radigue.p[sel].level = (uint8_t)CLAMP(100 - CLAMP(y, 0, 32767) * 100 / 32767, 0, 100);
}

/* ---- the picture ---- */
static int y_of(struct rect r, int32_t p) { return r.y + r.h - 1 - (int)((int64_t)(CLAMP(p, 24000, 96000) - 24000) * (r.h - 1) / 72000); }   /* C1 .. C7 */
static uint8_t ink_of(int h) { while (h > 1 && !(h & 1)) h >>= 1; return h == 1 ? R_AMBER : h % 3 == 0 ? R_GREEN : h == 5 || h == 15 ? R_PINK : h == 7 ? R_RED : R_BLUE; }   /* octaves share a colour */
static void column(struct rect r, int x) {
    const struct font *f = text_font();
    int th = MAX(3, f->height / 6);
    gfx_fill(x, r.y, MIN(6, r.x + r.w - x), r.h, C_BG);                                  /* where it goes next */
    for (int o = 1; o <= 7; o++) if (x % 4 == 0) gfx_pixel(x, y_of(r, o * 12000 + 12000), ramp(R_PANEL, 6));   /* the Cs */
    for (int i = 0; i < DRONE_PARTIALS; i++) {
        int32_t a = drone_pair_amp(i);
        if (a < 200) continue;
        int y = y_of(r, drone_partial_pitch(i));
        uint32_t v = (uint32_t)a * 32767, q = 0, bit = 1u << 30;                        /* its square root: quiet lines still show */
        while (bit > v) bit >>= 2;
        while (bit) { if (v >= q + bit) { v -= q + bit; q = (q >> 1) + bit; } else q >>= 1; bit >>= 2; }
        gfx_vline(x, y - th / 2, th, ramp(ink_of(radigue.p[i].harmonic), 3 + (int)(q * 12 / 32768)));
    }
    if (x + 1 < r.x + r.w) gfx_vline(x + 1, r.y, r.h, ramp(R_GRAY, 5));                  /* the head */
}
static void score(struct rect r, bool fresh, uint64_t now) {
    if (fresh) { head = 0; col_at = now; }
    for (int k = 0; k < 4 && now - col_at >= 100; k++) {                                 /* a column a tenth of a second */
        col_at += 100;
        column(r, r.x + head);
        head = (head + 1) % MAX(1, r.w);
    }
    if (now - col_at > 1000) col_at = now;
}

static void fmt_pitch(char *s, int n, int32_t p) {
    int note = (p + 500) / 1000, c = p - note * 1000;                                    /* the nearest note, and tenths of a cent */
    snfmt(s, n, "%s%d %c%d.%d¢", note_names[note % 12], note / 12 - 1, c < 0 ? '-' : '+', (c < 0 ? -c : c) / 10, (c < 0 ? -c : c) % 10);
}
static void fmt_hz(char *s, int n, uint32_t mhz) {
    if (mhz >= 100000) snfmt(s, n, "%d Hz", (int)((mhz + 500) / 1000));
    else snfmt(s, n, "%d.%02d Hz", (int)(mhz / 1000), (int)(mhz % 1000 / 10));
}
static void fmt_time(char *s, int n, uint32_t sec) { snfmt(s, n, "%d:%02d", (int)(sec / 60), (int)(sec % 60)); }

static void draw(uint64_t now) {
    struct lin_layout L; lineage_layout(&L, 30);
    char t[96], a[24], b[24], c[16];
    fmt_pitch(a, sizeof a, drone_base_now()); fmt_hz(b, sizeof b, drone_hz_milli(drone_base_now())); fmt_time(c, sizeof c, drone_seconds());
    snfmt(t, sizeof t, "RADIGUE · %s · %s%s%s", a, b, drone_sounding() ? " · " : "", drone_sounding() ? c : "");
    ui_panel(L.x, L.y, L.pw, L.ph, t, C_AMBER);
    int rows = DRONE_PARTIALS + 1, sh = MAX(4, L.ph - 3 - rows);
    struct rect r;
    bool fresh = ui_canvas_keyed(&r, L.x + 1, L.y + 1, L.pw - 2, sh, C_BG, ui_hash_int(UI_HASH0, text_cols() << 16 | text_rows()));
    score_px = r;
    score(r, fresh, now);
    ptr.shape = ui_in(r, ptr.x, ptr.y) ? PTR_CROSS : PTR_ARROW;
    /* the partials */
    int ty = L.y + 1 + sh, tx = L.x + 2, lim = L.y + L.ph - 1;
    if (ty < lim) text_str_n(tx, ty++, "   h   detune  level  breathes  now        beats", L.pw - 4, C_DIM, C_PANEL);
    for (int i = 0; i < DRONE_PARTIALS && ty < lim; i++, ty++) {
        const struct drone_partial *p = &radigue.p[i];
        char row[80], tm[8], beat[32] = "";
        fmt_time(tm, sizeof tm, p->breath_s);
        int32_t mhz = drone_beat_mhz(i);
        if (mhz >= 0) { char hz[16]; fmt_hz(hz, sizeof hz, (uint32_t)mhz); snfmt(beat, sizeof beat, "%s with %d", hz, drone_twin(i) + 1); }
        int dt = p->detune < 0 ? -p->detune : p->detune;
        snfmt(row, sizeof row, "%d %2d  %c%2d.%d¢   %3d    %5s", i + 1, p->harmonic, p->detune < 0 ? '-' : '+', dt / 10, dt % 10, p->level, tm);
        text_put(tx - 1, ty, i == sel ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str_n(tx, ty, row, L.pw - 4, i == sel ? C_BRIGHT : C_TEXT, C_PANEL);
        int mx = tx + 34;
        if (mx + 10 < L.x + L.pw - 1) ui_bar(mx, ty, 8, drone_partial_amp(i), 32767, ramp(ink_of(p->harmonic), 12), C_PANEL);
        if (mx + 11 < L.x + L.pw - 2) text_str_n(mx + 10, ty, beat, L.x + L.pw - 2 - (mx + 10), mhz >= 0 ? C_GREEN : C_DIM, C_PANEL);
    }
    /* the knobs */
    ui_panel(L.kx, L.y, L.kw, L.ph, radigue.playing ? "DRONE · on" : drone_sounding() ? "DRONE · fading" : "DRONE", C_CYAN);
    int ry = L.y + 2;
    for (int i = 0; i < KNOBS && ry < lim; i++, ry++) {
        char v[32];
        const struct drone_partial *p = &radigue.p[sel];
        switch (i) {
        case K_BASE:   fmt_pitch(v, sizeof v, radigue.target); break;
        case K_SWEEP:  if (radigue.sweep_s) fmt_time(v, sizeof v, radigue.sweep_s); else snfmt(v, sizeof v, "at once"); break;
        case K_FADE:   snfmt(v, sizeof v, "%d s", radigue.fade_s); break;
        case K_DEPTH:  snfmt(v, sizeof v, "%d%%", radigue.depth); break;
        case K_LEVEL:  snfmt(v, sizeof v, "%d", radigue.level); break;
        case K_HARM:   snfmt(v, sizeof v, "%d", p->harmonic); break;
        case K_DETUNE: { int dt = p->detune < 0 ? -p->detune : p->detune; snfmt(v, sizeof v, "%c%d.%d¢", p->detune < 0 ? '-' : '+', dt / 10, dt % 10); break; }
        case K_PLEVEL: snfmt(v, sizeof v, "%d", p->level); break;
        default:       { char tm[8]; fmt_time(tm, sizeof tm, p->breath_s); snfmt(v, sizeof v, "every %s", tm); break; }
        }
        if (i == K_HARM && ry < lim) { char h[24]; snfmt(h, sizeof h, "partial %d", sel + 1); text_str_n(L.kx + 2, ry++, h, L.kw - 4, C_GREEN, C_PANEL); }
        if (ry < lim) lineage_knob(L.kx + 1, ry, L.kw - 2, knob_names[i], v, i == knob);
    }
    ry++;
    uint32_t left = drone_sweep_left_s();
    if (left && ry < lim) { char tm[8], p[24], s[48]; fmt_time(tm, sizeof tm, left); fmt_pitch(p, sizeof p, radigue.target); snfmt(s, sizeof s, "→ %s in %s", p, tm); text_str_n(L.kx + 2, ry++, s, L.kw - 4, C_AMBER, C_PANEL); }
    if (ry < lim) { char s[32]; snfmt(s, sizeof s, "keys from C%d", kb_octave); text_str_n(L.kx + 2, ry++, s, L.kw - 4, C_DIM, C_PANEL); }
    if (!drone_sounding() && ry + 1 < lim) text_str_n(L.kx + 2, ++ry, "Space: it fades in", L.kw - 4, C_DIM, C_PANEL);
    ui_lesson(L.x, L.ly, text_cols() - 4, L.lh, &lesson_radigue);
    FOOTER("SPACE", radigue.playing ? "fade out" : "fade in", "Z-/", "the base goes there", "1-8", "partial", "↑ ↓ ← →", "knobs",
           "⇧← →", "finer", "PGUP PGDN", "octave", "HOME", "start again", "", "a finger on the pad: detune, level");
}

const struct view lin_radigue = { "RADIGUE", key, 0, pointer, draw, midi, "1969" };
