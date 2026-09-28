/* REICH, a LINEAGE view (after Steve Reich): players looping one pattern, and a process moving them apart and back
   (core/phase.c). The picture: a ring a player, the pattern's steps around it, turned by how far the player is ahead
   of the first — in a move the ring turns slowly by a step and stops, locked; the step each plays is lit, and a line
   joins the players' steps that sound together. Under it the pattern, a cell a step, written from the letter keys. */
#include "lineage.h"
#include "phase.h"
#include "harmony.h"
#include "seq.h"
#include "synth.h"
#include "sampler.h"
#include "tables.h"
#include "lessons.h"
#include "gfx.h"
#include "keys.h"

enum { K_MODE, K_PLAYERS, K_STEPS, K_PER_BEAT, K_TEMPO, K_HOLD, K_MOVE, K_SOUND, K_LEVEL, KNOBS };
static const char *const note_names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char kb[] = "zsxdcvgbhnjm,l.;/";          /* C to E, black keys on the row above, as on WAVE */
static const uint8_t inks[PHASE_PLAYERS] = { R_AMBER, R_CYAN, R_GREEN, R_PINK };
static int knob, cursor, kb_octave = 4;
static uint8_t preview;                                 /* the note a key sounds while the players rest */

static void turn(int k, int d) {
    switch (k) {
    case K_MODE:     reich.mode = (uint8_t)((reich.mode + d + PHASE_MODES) % PHASE_MODES); break;
    case K_PLAYERS:  reich.players = (uint8_t)CLAMP(reich.players + d, 2, PHASE_PLAYERS); break;
    case K_STEPS:    reich.len = (uint8_t)CLAMP(reich.len + d, 2, PHASE_STEPS); cursor = MIN(cursor, reich.len - 1); break;
    case K_PER_BEAT: reich.per_beat = (uint8_t)CLAMP(reich.per_beat + d, 2, 4); break;
    case K_TEMPO:    seq.bpm = (uint16_t)CLAMP(seq.bpm + d * 2, 40, 300); break;
    case K_HOLD:     reich.hold = (uint8_t)CLAMP(reich.hold + d, 1, 32); break;
    case K_MOVE:     if (reich.mode == PM_PHASE) reich.move = (uint8_t)CLAMP(reich.move + d, 1, 16);
                     else reich.drift = (uint8_t)CLAMP(reich.drift + d, 1, 50); break;
    case K_SOUND:    if (reich.mode == PM_LOOP) reich.slot = (uint8_t)((reich.slot + d + SAMPLE_SLOTS) % SAMPLE_SLOTS);
                     else reich.sound = (uint8_t)((reich.sound + d + PHASE_SOUNDS) % PHASE_SOUNDS); break;
    case K_LEVEL:    reich.level = (uint8_t)CLAMP(reich.level + d * 5, 0, 100); break;
    }
}

static bool key(uint8_t code, bool down, uint64_t now) {
    (void)now;
    for (int i = 0; kb[i]; i++) if (code == (uint8_t)kb[i]) {           /* the note at the cursor, and on */
        if (down) {
            int note = harmony_note(CLAMP((kb_octave + 1) * 12 + i, 24, 108));
            reich.notes[cursor] = (uint8_t)note; cursor = (cursor + 1) % CLAMP(reich.len, 2, PHASE_STEPS);
            if (!reich.playing) { synth_note_off_tag(0xCF0); synth_note_on_pan((uint8_t)note, 100, phase_sounds[reich.sound % PHASE_SOUNDS], 0xCF0, 0); preview = (uint8_t)code; }
        } else if (preview == code) { synth_note_off_tag(0xCF0); preview = 0; }
        return true;
    }
    if (!down) return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_SPACE || code == KEY_TAB ||
                      code == KEY_ENTER || code == KEY_BACKSPACE || code == '[' || code == ']' || code == '-' || code == '=' ||
                      code == KEY_PGUP || code == KEY_PGDN || code == KEY_HOME;
    switch (code) {
    case KEY_UP:        knob = (knob + KNOBS - 1) % KNOBS; return true;
    case KEY_DOWN:      knob = (knob + 1) % KNOBS; return true;
    case KEY_LEFT:      turn(knob, -1); return true;
    case KEY_RIGHT:     turn(knob, +1); return true;
    case KEY_SPACE:     phase_play(!reich.playing); return true;
    case KEY_TAB:       turn(K_MODE, +1); return true;
    case KEY_ENTER:     phase_from_chord(); return true;
    case KEY_BACKSPACE: reich.notes[cursor] = 0; cursor = (cursor + 1) % CLAMP(reich.len, 2, PHASE_STEPS); return true;
    case '[':           cursor = (cursor + CLAMP(reich.len, 2, PHASE_STEPS) - 1) % CLAMP(reich.len, 2, PHASE_STEPS); return true;
    case ']':           cursor = (cursor + 1) % CLAMP(reich.len, 2, PHASE_STEPS); return true;
    case '-':           turn(K_STEPS, -1); return true;
    case '=':           turn(K_STEPS, +1); return true;
    case KEY_PGUP:      kb_octave = MIN(kb_octave + 1, 7); return true;
    case KEY_PGDN:      kb_octave = MAX(kb_octave - 1, 1); return true;
    case KEY_HOME:      phase_defaults(); cursor = 0; return true;
    }
    return false;
}
static bool midi(uint8_t note, uint8_t vel, uint64_t now) {           /* a note from a keyboard is written at the cursor */
    (void)now;
    if (vel) { reich.notes[cursor] = (uint8_t)harmony_note(note); cursor = (cursor + 1) % CLAMP(reich.len, 2, PHASE_STEPS); }
    return true;
}

/* ---- the picture ---- */
static void at_angle(int cx, int cy, int rad, int32_t turn_q16, int *x, int *y) {   /* turn_q16: a whole turn is 65536, 0 at the top */
    int a = (int)(((uint32_t)turn_q16 >> 3) & 8191);
    *x = cx + (int)((int64_t)sine_q15_8192[a] * rad / 32768);
    *y = cy - (int)((int64_t)sine_q15_8192[(a + 2048) & 8191] * rad / 32768);
}
static void rings(struct rect r, const int32_t *off, int np, int n) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2, R = MIN(r.w, r.h) / 2 - text_font()->height / 2;
    if (R < 12) return;
    int dot = MAX(2, R / 22), lo = 127, hi = 0;
    for (int i = 0; i < n; i++) if (reich.notes[i]) { lo = MIN(lo, reich.notes[i]); hi = MAX(hi, reich.notes[i]); }
    int played[PHASE_PLAYERS];
    for (int k = 0; k < np; k++) {
        int rad = R * (12 - 3 * k) / 12;
        uint8_t ink = inks[k];
        for (int s = 0; s < 96; s++) { int x, y; at_angle(cx, cy, rad, s * 65536 / 96, &x, &y); gfx_pixel(x, y, ramp(ink, 4)); }   /* the ring */
        played[k] = (phase_pl[k].pos + n - 1) % n;
        for (int i = 0; i < n; i++) {
            int x, y; at_angle(cx, cy, rad, (int32_t)(((int64_t)i * 65536 - off[k]) / n), &x, &y);
            bool now_ = reich.playing && i == played[k];
            if (!reich.notes[i] && reich.mode != PM_LOOP) { gfx_ring(x, y, dot, ramp(ink, now_ ? 12 : 5)); continue; }   /* a rest */
            int lift = hi > lo ? (reich.notes[i] - lo) * 5 / (hi - lo) : 2;
            gfx_disc(x, y, dot + lift * dot / 5, now_ ? C_BRIGHT : ramp(ink, 7 + lift));
        }
    }
    if (reich.playing) for (int k = 1; k < np; k++) {                   /* what sounds together: the first's step and k's */
        int x0, y0, x1, y1;
        at_angle(cx, cy, R, (int32_t)((int64_t)played[0] * 65536 / n), &x0, &y0);
        at_angle(cx, cy, R * (12 - 3 * k) / 12, (int32_t)(((int64_t)played[k] * 65536 - off[k]) / n), &x1, &y1);
        gfx_line(x0, y0, x1, y1, ramp(inks[k], 9));
    }
}

static void pattern_row(int x, int y, int w) {                           /* the pattern, a cell a step */
    int n = CLAMP(reich.len, 2, PHASE_STEPS), cw = MAX(4, MIN(6, w / n));
    for (int i = 0; i < n && x + i * cw + cw <= x + w; i++) {
        char c[8];
        int note = reich.notes[i];
        if (note) snfmt(c, sizeof c, "%s%d", note_names[note % 12], note / 12 - 1); else snfmt(c, sizeof c, "·");
        bool on = reich.playing && i == (phase_pl[0].pos + n - 1) % n;
        uint8_t fg = i == cursor ? C_BLACK : on ? C_BRIGHT : note ? C_TEXT : C_DIM, bg = i == cursor ? C_AMBER : C_PANEL;
        text_fill(x + i * cw, y, cw - 1, 1, ' ', fg, bg);
        text_str_n(x + i * cw, y, c, cw - 1, fg, bg);
    }
}

static void draw(uint64_t now) {
    struct lin_layout L; lineage_layout(&L, 30);
    int np = CLAMP(reich.players, 2, PHASE_PLAYERS), n = reich.mode == PM_LOOP ? 1 : CLAMP(reich.len, 2, PHASE_STEPS);
    int32_t off[PHASE_PLAYERS] = { 0 };
    for (int k = 1; k < np; k++) off[k] = reich.mode == PM_LOOP ? 0 : phase_offset_q16(k);
    char t[96];
    snfmt(t, sizeof t, "REICH · %s · %d players · %d steps · %d BPM", phase_mode_names[reich.mode % PHASE_MODES], np, n, seq.bpm);
    ui_panel(L.x, L.y, L.pw, L.ph, t, C_AMBER);
    int ch = MAX(4, L.ph - 2 - 4);
    struct rect r;
    uint32_t hk = ui_hash_int(UI_HASH0, text_cols() << 16 | text_rows());
    hk = ui_hash_int(hk, reich.playing | np << 1 | n << 4 | reich.mode << 9);
    hk = ui_hash(hk, reich.notes, sizeof reich.notes);
    for (int k = 0; k < np; k++) hk = ui_hash_int(ui_hash_int(hk, off[k] >> 10), phase_pl[k].pos);
    if (reich.playing) hk = ui_hash_int(hk, (int32_t)(now / 33));                /* 30 frames a second at most */
    if (ui_canvas_keyed(&r, L.x + 1, L.y + 1, L.pw - 2, ch, C_BG, hk)) rings(r, off, np, n);
    /* where each player is, in words */
    int ty = L.y + 1 + ch, lim = L.y + L.ph - 1;
    for (int k = 1; k < np && ty < lim - 2; k++) {
        char s[96]; int ahead = (off[k] + 32768) >> 16;
        const struct phase_player *P = &phase_pl[k];
        if (!reich.playing) snfmt(s, sizeof s, "player %d waits", k + 1);
        else if (reich.mode == PM_PHASE) snfmt(s, sizeof s, "player %d: %s, %d.%02d steps ahead%s", k + 1, P->moving ? "pulling ahead" : "locked",
                                               off[k] >> 16, (off[k] & 65535) * 100 >> 16, P->moving ? "" : " (holding)");
        else if (reich.mode == PM_SHIFT) snfmt(s, sizeof s, "player %d: %d steps ahead, jumps after %d of %d repeats", k + 1, ahead % n, P->count, reich.hold);
        else snfmt(s, sizeof s, "player %d: %d.%d%% faster, %d.%02d steps ahead", k + 1, reich.drift * k / 10, reich.drift * k % 10, off[k] >> 16, (off[k] & 65535) * 100 >> 16);
        text_str_n(L.x + 2, ty++, s, L.pw - 4, inks[k] == R_CYAN ? C_CYAN : inks[k] == R_GREEN ? C_GREEN : inks[k] == R_PINK ? C_PINK : C_AMBER, C_PANEL);
    }
    if (reich.mode == PM_LOOP) {
        const struct sample *sm = &samples[reich.slot % SAMPLE_SLOTS];
        char s[80];
        if (sm->len) snfmt(s, sizeof s, "the loop: slot %d, %s", reich.slot + 1, sm->name[0] ? sm->name : "(untitled)");
        else snfmt(s, sizeof s, "slot %d is empty: record a voice on WAVE › sampler first", reich.slot + 1);
        if (ty < lim) text_str_n(L.x + 2, ty++, s, L.pw - 4, sm->len ? C_TEXT : C_DIM, C_PANEL);
    } else if (lim - 1 > ty) pattern_row(L.x + 2, lim - 1, L.pw - 4);
    /* the knobs */
    ui_panel(L.kx, L.y, L.kw, L.ph, reich.playing ? "PLAYERS · on" : "PLAYERS", C_CYAN);
    static const char *const names[KNOBS] = { "process", "players", "steps", "a beat", "tempo", "hold", "move", "sound", "level" };
    int ry = L.y + 2;
    for (int i = 0; i < KNOBS && ry < lim; i++, ry++) {
        char v[32]; const char *nm = names[i];
        switch (i) {
        case K_MODE:     snfmt(v, sizeof v, "%s", phase_mode_names[reich.mode % PHASE_MODES]); break;
        case K_PLAYERS:  snfmt(v, sizeof v, "%d", np); break;
        case K_STEPS:    snfmt(v, sizeof v, "%d", CLAMP(reich.len, 2, PHASE_STEPS)); break;
        case K_PER_BEAT: snfmt(v, sizeof v, "%d steps", reich.per_beat); break;
        case K_TEMPO:    snfmt(v, sizeof v, "%d BPM", seq.bpm); break;
        case K_HOLD:     snfmt(v, sizeof v, "%d repeat%s", reich.hold, reich.hold == 1 ? "" : "s"); break;
        case K_MOVE:     if (reich.mode == PM_PHASE) snfmt(v, sizeof v, "over %d repeat%s", reich.move, reich.move == 1 ? "" : "s");
                         else if (reich.mode == PM_SHIFT) snfmt(v, sizeof v, "a jump");
                         else { nm = "drift"; snfmt(v, sizeof v, "%d.%d%% a player", reich.drift / 10, reich.drift % 10); }
                         break;
        case K_SOUND:    if (reich.mode == PM_LOOP) { nm = "slot"; snfmt(v, sizeof v, "%d", reich.slot + 1); }
                         else snfmt(v, sizeof v, "%s", synth_preset_name(phase_sounds[reich.sound % PHASE_SOUNDS])); break;
        default:         snfmt(v, sizeof v, "%d", reich.level); break;
        }
        lineage_knob(L.kx + 1, ry, L.kw - 2, nm, v, i == knob);
    }
    ry++;
    static const char *const how[PHASE_MODES] = { "moves a step, then locks", "jumps a step, on the grid", "faster, never locking", "a recording, against itself" };
    if (ry < lim) text_str_n(L.kx + 2, ry++, how[reich.mode % PHASE_MODES], L.kw - 4, C_DIM, C_PANEL);
    if (ry < lim && reich.mode != PM_LOOP) { char s[32]; snfmt(s, sizeof s, "keys from C%d", kb_octave); text_str_n(L.kx + 2, ry++, s, L.kw - 4, C_DIM, C_PANEL); }
    ui_lesson(L.x, L.ly, text_cols() - 4, L.lh, &lesson_reich);
    FOOTER("SPACE", reich.playing ? "stop" : "play", "TAB", "process", "Z-/", "a note at the cursor", "BKSP", "a rest", "[ ]", "cursor",
           "- =", "steps", "ENTER", "from the chord", "↑ ↓ ← →", "knobs", "PGUP PGDN", "octave");
}

const struct view lin_reich = { "REICH", key, 0, 0, draw, midi, "1965" };
