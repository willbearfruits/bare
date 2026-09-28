/* SIEVES (XENAKIS, a view): the four sieves of core/sieve.c. Each is a formula and a row of dots — the whole numbers it
   keeps, from 0 — read as sixteenths: the rhythm section's SIEVE pattern plays S1 on the kick, S2 the snare, S3 the hat
   and S4 the bass (Space starts it here), counted from the start, so a sieve longer than a bar keeps turning across the
   bars. Read as pitches (the clouds and UPIC snap to them), the letter rows play the chosen sieve as a scale: each key
   the next member up from the keyboard's C. Enter edits a formula; Tab puts an example in. */
#include "xen.h"
#include "sieve.h"
#include "rhythm.h"
#include "synth.h"
#include "gfx.h"
#include "keys.h"
#include "undo.h"

#define TAG_SCALE 0x5C0                                   /* | key: the scale keyboard */
static const char *const lanes[SIEVES] = { "kick", "snare", "hat", "bass" };
static const uint8_t lane_ramp[SIEVES] = { R_AMBER, R_PINK, R_CYAN, R_GREEN };
static const char kb_low[] = "zsxdcvgbhnjm,l.;/", kb_high[] = "q2w3er5t6y7ui9o0p";
static int sel, octave = 3, sound = P_GD1, example[SIEVES] = { -1, -1, -1, -1 };
static bool editing; static char buf[SIEVE_TEXT]; static int blen; static char err[48];
static int8_t playing_key[34 + 1];

static int key_index(uint8_t code) {
    for (int i = 0; kb_low[i]; i++) if (code == (uint8_t)kb_low[i]) return i;
    for (int i = 0; kb_high[i]; i++) if (code == (uint8_t)kb_high[i]) return 17 + i;
    return -1;
}
/* the k-th member at or above the keyboard's C, as a pitch in 1/256 semitones (or -1) */
static int32_t scale_pitch(const struct sieve *s, int k) {
    int u = s->unit ? s->unit : 1;
    int32_t x = 12 * (octave + 1) * u;
    for (int i = 0; i <= k; i++) {
        int32_t m = sieve_next(s, x, 128 * u);
        if (m == SIEVE_NONE || m >= 128 * u) return -1;                   /* past the top of MIDI */
        if (i == k) return m * 256 / u;
        x = m + 1;
    }
    return -1;
}
static void scale_key(int k, bool down) {
    uint16_t tag = (uint16_t)(TAG_SCALE | k);
    if (!down) { if (playing_key[k]) { synth_note_off_tag(tag); playing_key[k] = 0; } return; }
    int32_t p = scale_pitch(&sieves[sel], k);
    if (p < 0 || p >= 128 * 256) return;
    synth_note_on((uint8_t)(p >> 8), 105, (uint8_t)sound, tag);
    if (p & 255) synth_tag_bend(tag, p & 255);
    playing_key[k] = 1;
}

static bool typing(void) { return editing; }
static char typed(uint8_t code) {                          /* the formula's characters, shifted as on a US keyboard */
    if (ui_shift) switch (code) { case '2': return '@'; case '\\': return '|'; case '7': return '&'; case '9': return '('; case '0': return ')'; default: return 0; }
    if (code >= '0' && code <= '9') return (char)code;
    switch (code) { case '-': case ' ': return (char)code; case '.': return '@'; case '/': return '|'; default: return 0; }
}
static bool key(uint8_t code, bool down, uint64_t now) {
    if (editing) {
        if (!down) return true;
        if (code == KEY_ESC) { editing = false; err[0] = 0; return true; }
        if (code == KEY_BACKSPACE) { if (blen) buf[--blen] = 0; return true; }
        if (code == KEY_ENTER) {
            struct sieve t = sieves[sel];
            snfmt(t.text, SIEVE_TEXT, "%s", buf);
            if (!sieve_compile(&t)) { snfmt(err, sizeof err, "%s", t.err); return true; }
            undo_one(U_SIEVE, sel, "a sieve", now);
            snfmt(sieves[sel].text, SIEVE_TEXT, "%s", buf); sieve_compile(&sieves[sel]);
            editing = false; err[0] = 0; example[sel] = -1;
            return true;
        }
        char c = typed(code);
        if (c && blen < SIEVE_TEXT - 1) { buf[blen++] = c; buf[blen] = 0; }
        return true;
    }
    int k = key_index(code);
    if (k >= 0) { scale_key(k, down); return true; }
    if (!down) return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_TAB || code == KEY_ENTER ||
                      code == KEY_SPACE || code == KEY_HOME || code == KEY_END || code == KEY_PGUP || code == KEY_PGDN;
    struct sieve *s = &sieves[sel];
    switch (code) {
    case KEY_UP:    sel = (sel + SIEVES - 1) % SIEVES; return true;
    case KEY_DOWN:  sel = (sel + 1) % SIEVES; return true;
    case KEY_LEFT: case KEY_RIGHT: {                           /* the unit, read as pitches: 1 2 3 6 steps a semitone */
        static const uint8_t units[4] = { 1, 2, 3, 6 };
        int i = 0; while (i < 3 && units[i] != s->unit) i++;
        undo_one(U_SIEVE, sel, "a sieve", now);
        s->unit = units[(i + (code == KEY_RIGHT ? 1 : 3)) % 4]; sieve_changes++;
        return true; }
    case KEY_TAB: {
        int e = (example[sel] + 1) % sieve_example_count;
        undo_one(U_SIEVE, sel, "a sieve", now);
        snfmt(s->text, SIEVE_TEXT, "%s", sieve_examples[e].text);
        sieve_compile(s); example[sel] = e;
        return true; }
    case KEY_ENTER: editing = true; snfmt(buf, sizeof buf, "%s", s->text); blen = (int)strlen(buf); err[0] = 0; return true;
    case KEY_SPACE:
        if (rhythm.playing && rhythm.pattern == RHYTHM_SIEVE) rhythm_play(false);
        else { rhythm.pattern = RHYTHM_SIEVE; rhythm_play(true); }
        return true;
    case KEY_HOME:  sound = synth_preset_next(sound, -1); return true;
    case KEY_END:   sound = synth_preset_next(sound, +1); return true;
    case KEY_PGUP:  octave = MIN(octave + 1, 7); return true;
    case KEY_PGDN:  octave = MAX(octave - 1, 0); return true;
    }
    return false;
}

static bool midi(uint8_t note, uint8_t vel, uint64_t now) {  /* MIDI notes play the scale too: C of the octave and up */
    (void)now;
    int k = note - 12 * (octave + 1);
    if (k < 0 || k > 34) return true;
    scale_key(k, vel > 0);
    return true;
}

/* a sieve as sixteenths: a row a bar (two on wide screens), the rows stacked, so a sieve that doesn't divide the bar
   shows as a pattern that drifts; while the SIEVE rhythm plays, the page of bars it is in, the step it is on lit */
static void grid(int i, int x, int y, int w, int h) {
    const struct sieve *s = &sieves[i];
    bool live = rhythm.playing && rhythm.pattern == RHYTHM_SIEVE;
    const struct font *f = text_font();
    int64_t now_step = live ? (int64_t)(rhythm_steps_q16() >> 16) : -1;
    int per = w * f->width >= 700 ? 32 : 16, rh = MAX(6, f->height / 2), nr = MAX(1, h * f->height / rh), page = per * nr;
    int base = now_step >= 0 ? (int)(now_step / page * page) : 0;
    uint32_t key = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, (int32_t)sieve_changes), base), (int32_t)now_step);
    key = ui_hash_int(ui_hash_int(key, per << 8 | nr), i == sel);
    struct rect r;
    if (!ui_canvas_keyed(&r, x, y, w, h, C_BG, key)) return;
    int cw = r.w / per;
    for (int row = 0; row < nr; row++)
        for (int k = 0; k < per; k++) {
            int v = base + row * per + k, cx = r.x + k * cw, cy = r.y + row * rh;
            if (cy + rh > r.y + r.h) break;
            if (k && k % 16 == 0) gfx_fill(cx - 1, cy, 1, rh, ramp(R_PANEL, 7));           /* the bar line */
            bool in = sieve_has(s, v), at = v == now_step;
            if (in) gfx_fill(cx + 1, cy + 1, cw - 2, rh - 2, at ? ramp(lane_ramp[i], 15) : ramp(lane_ramp[i], (i == sel ? 11 : 8) + (v % 4 ? 0 : 2)));
            else gfx_fill(cx + cw / 2 - (v % 4 ? 0 : 1), cy + rh / 2 - 1, v % 4 ? 1 : 3, v % 4 ? 1 : 2, at ? ramp(R_GRAY, 14) : ramp(R_GRAY, v % 4 ? 4 : 6));
            if (at && !in) gfx_fill(cx + 1, cy + rh - 2, cw - 2, 1, ramp(R_GRAY, 14));
        }
}

static void note_name(int32_t q8, char *out, int cap) {
    static const char *const nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int n = q8 >> 8, fr = q8 & 255;
    if (fr) snfmt(out, cap, "%s%d+%d", nm[n % 12], n / 12 - 1, fr * 100 / 256);
    else snfmt(out, cap, "%s%d", nm[n % 12], n / 12 - 1);
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    int sw = 34, x = 2, y = XEN_TOP, pw = cols - 4 - sw - 1, ph = rows - y - 3;
    ui_panel(x, y, pw, ph, "SIEVES · read as sixteenths", C_AMBER);
    int bh = MAX(4, (ph - 2) / SIEVES);
    for (int i = 0; i < SIEVES; i++) {
        const struct sieve *s = &sieves[i];
        int by = y + 1 + i * bh;
        if (by + 3 >= y + ph) break;
        bool on = i == sel;
        char lab[16]; snfmt(lab, sizeof lab, "S%d %s", i + 1, lanes[i]);
        text_put(x + 1, by, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(x + 3, by, lab, on ? C_BRIGHT : C_TEXT, C_PANEL);
        if (on && editing) {
            char e[SIEVE_TEXT + 2]; snfmt(e, sizeof e, "%s_", buf);
            text_str_n(x + 14, by, e, pw - 16, C_AMBER, C_PANEL);
        } else text_str_n(x + 14, by, s->text, pw - 16, on ? C_BRIGHT : C_TEXT, C_PANEL);
        grid(i, x + 3, by + 1, pw - 5, MAX(1, bh - 2));
        char info[64];
        if (on && editing && err[0]) { text_str_n(x + 14, by + bh - 1, err, pw - 16, C_RED, C_PANEL); continue; }
        if (s->period) snfmt(info, sizeof info, "%d of every %u", s->members, s->period);
        else snfmt(info, sizeof info, "%d of the first 1000", s->members);
        if (bh >= 4) text_str_n(x + 14, by + bh - 1, info, pw - 16, C_DIM, C_PANEL);
    }

    int kx = x + pw + 1, ry = y + 1;
    const struct sieve *s = &sieves[sel];
    char t[40]; snfmt(t, sizeof t, "S%d · plays the %s", sel + 1, lanes[sel]);
    ui_panel(kx, y, sw, ph, t, C_CYAN);
    int lim = y + ph - 1;
    if (example[sel] >= 0 && ry < lim) { text_str_n(kx + 2, ry++, sieve_examples[example[sel]].what, sw - 3, C_GREEN, C_PANEL); }
    if (ry < lim) ry++;
    if (ry < lim) text_str(kx + 2, ry++, "as pitches", C_GREEN, C_PANEL);
    static const char *const unit_names[7] = { "", "semitones", "quarter tones", "sixth tones", "", "", "72 an octave" };
    if (ry < lim) { text_str(kx + 2, ry, "unit", C_TEXT, C_PANEL); text_str(kx + 8, ry++, unit_names[s->unit < 7 ? s->unit : 1], C_AMBER, C_PANEL); }
    char line[64]; int lx = 0; line[0] = 0;
    for (int k = 0; k < 24 && ry < lim; k++) {                /* the scale from the keyboard's C */
        int32_t p = scale_pitch(s, k);
        if (p < 0) break;
        char nn[12]; note_name(p, nn, sizeof nn);
        int l = (int)strlen(nn);
        if (lx + l + 1 > sw - 4) { text_str(kx + 2, ry++, line, C_DIM, C_PANEL); lx = 0; line[0] = 0; if (ry >= lim || ry > y + 9) break; }
        snfmt(line + lx, (int)sizeof line - lx, "%s%s", lx ? " " : "", nn); lx += l + (lx ? 1 : 0);
    }
    if (lx && ry < lim) text_str(kx + 2, ry++, line, C_DIM, C_PANEL);
    if (ry < lim) ry++;
    char sn[40]; snfmt(sn, sizeof sn, "%s", synth_preset_name(sound));
    if (ry < lim) { text_str(kx + 2, ry, "sound", C_TEXT, C_PANEL); text_str_n(kx + 8, ry++, sn, sw - 10, C_AMBER, C_PANEL); }
    char kc[16]; snfmt(kc, sizeof kc, "C%d up", octave);
    if (ry < lim) { text_str(kx + 2, ry, "keys", C_TEXT, C_PANEL); text_str(kx + 8, ry++, kc, C_AMBER, C_PANEL); }
    if (ry < lim) ry++;
    if (ry < lim) text_str(kx + 2, ry++, "write", C_GREEN, C_PANEL);
    static const char *const help[] = { "3@1   leaves 1 after 3s", "|  or (also /)", "&  and", "-  not", "( )", "3.1 is 3@1 too" };
    for (int i = 0; i < (int)ARRAY_LEN(help) && ry < lim; i++) text_str_n(kx + 2, ry++, help[i], sw - 3, C_DIM, C_PANEL);
    if (editing) FOOTER("ENTER", "keep", "ESC", "leave it", "BKSP", "rub out", "⇧2 @  ⇧\\ |  ⇧7 &  ⇧9 ⇧0 ( )", "");
    else FOOTER("SPACE", "the SIEVE rhythm", "ENTER", "write", "TAB", "an example", "↑ ↓", "sieve", "← →", "unit", "Z-/ Q-P", "the scale",
                "HOME END", "sound", "PGUP PGDN", "octave");
}

const struct view xen_sieve = { "SIEVES", key, typing, 0, draw, midi, "1966" };
