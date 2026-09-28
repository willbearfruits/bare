/* CARLOS, a LINEAGE view (after Wendy Carlos): a Moog-style voice on the letter rows, in equal temperament or her
   alpha, beta and gamma scales (core/carlos.c); the touchpad a ribbon. The picture: two octaves as a ruler — equal
   temperament's notes above, the scale's steps below, the pure intervals (5:4, 3:2, 7:4) marked where they fall — and
   the patch, VCO to VCF to VCA, with the knobs' values. Enter plays the omnichord's chord in the scale and then in
   equal temperament, and says how far each interval is from pure. */
#include "lineage.h"
#include "carlos.h"
#include "omni.h"
#include "lessons.h"
#include "gfx.h"
#include "keys.h"

enum { K_SCALE, K_OCTAVE, K_MONO, K_GLIDE, K_WAVE, K_CUTOFF, K_RESO, K_CONTOUR, K_ATTACK, K_DECAY, K_SUSTAIN, K_RELEASE, K_LEVEL, K_RIBBON, KNOBS };
static const char *const knob_names[KNOBS] = { "scale", "octave", "voices", "glide", "wave", "cutoff", "res", "contour",
                                               "attack", "decay", "sustain", "release", "level", "ribbon" };
static const char *const note_letters[12] = { "C", 0, "D", 0, "E", "F", 0, "G", 0, "A", 0, "B" };
static const char rows_[3][13] = { "zxcvbnm,./", "asdfghjkl;'", "qwertyuiop[]" };
static int knob;
static bool held[CARLOS_KEYS];
static struct rect ruler_px;
/* the chord compared: the scale's version, then equal temperament's, 1.6 s each */
static int demo = -1; static uint64_t demo_ms; static int32_t demo_q8[2][3]; static int demo_n;
static char demo_text[3][64];

static int step_of(uint8_t code) {
    for (int r = 0, k = 0; r < 3; r++) for (int i = 0; rows_[r][i]; i++, k++) if (code == (uint8_t)rows_[r][i]) return k;
    return -1;
}
static void turn(int k, int d) {
    switch (k) {
    case K_SCALE:   carlos_all_off(); carlos.scale = (uint8_t)((carlos.scale + d + CARLOS_SCALES) % CARLOS_SCALES); break;
    case K_OCTAVE:  carlos_all_off(); carlos.octave = (int8_t)CLAMP(carlos.octave + d, 1, 6); break;
    case K_MONO:    carlos_all_off(); carlos.mono = !carlos.mono; break;
    case K_GLIDE:   carlos.glide = (uint8_t)CLAMP(carlos.glide + d * 5, 0, 100); break;
    case K_WAVE:    carlos.wave = (uint8_t)((carlos.wave + d + 3) % 3); break;
    case K_CUTOFF:  carlos.cutoff = (uint8_t)CLAMP(carlos.cutoff + d * 4, 0, 127); break;
    case K_RESO:    carlos.reso = (uint8_t)CLAMP(carlos.reso + d * 5, 0, 100); break;
    case K_CONTOUR: carlos.contour = (uint8_t)CLAMP(carlos.contour + d * 3, 0, 60); break;
    case K_ATTACK:  carlos.attack = (uint8_t)CLAMP(carlos.attack + d * 5, 0, 100); break;
    case K_DECAY:   carlos.decay = (uint8_t)CLAMP(carlos.decay + d * 5, 0, 100); break;
    case K_SUSTAIN: carlos.sustain = (uint8_t)CLAMP(carlos.sustain + d * 5, 0, 100); break;
    case K_RELEASE: carlos.release = (uint8_t)CLAMP(carlos.release + d * 5, 0, 100); break;
    case K_LEVEL:   carlos.level = (uint8_t)CLAMP(carlos.level + d * 5, 0, 100); break;
    case K_RIBBON:  carlos.ribbon_steps = !carlos.ribbon_steps; break;
    }
    carlos_apply();
}

/* the omnichord's chord, its intervals from the root: in equal temperament, in the scale's nearest steps, and pure */
static int32_t pure_c100(int semis) {
    static const int32_t c[12] = { 0, 11173, 20391, 31564, 38631, 49804, 58251, 70196, 81369, 88436, 96883, 108827 };   /* 16:15 9:8 6:5 5:4 4:3 7:5 3:2 8:5 5:3 7:4 15:8 */
    return c[((semis % 12) + 12) % 12];
}
static void demo_start(uint64_t now) {
    uint8_t pcs[3]; omni_chord_tones(omni_chord_word, pcs);
    int32_t root = carlos_pitch_q8(0) + (int32_t)((pcs[0] + 12 - 0) % 12) * 256;
    int32_t step = carlos_step_c100[carlos.scale];
    demo_n = 3;
    for (int i = 0; i < 3; i++) {
        int semis = (pcs[i] - pcs[0] + 12) % 12;
        int32_t k = (semis * 10000 + step / 2) / step;                        /* the scale's nearest step */
        int32_t scale_c100 = k * step, et_c100 = semis * 10000;
        demo_q8[0][i] = root + scale_c100 * 256 / 10000; demo_q8[1][i] = root + semis * 256;
        if (i) snfmt(demo_text[i], sizeof demo_text[i], "%s    %4d.%d  equal %4d  pure %4d.%d", i == 1 ? "3rd" : "5th",
                     (int)(scale_c100 / 100), (int)(scale_c100 % 100 / 10), (int)(et_c100 / 100), (int)(pure_c100(semis) / 100), (int)(pure_c100(semis) % 100 / 10));
    }
    snfmt(demo_text[0], sizeof demo_text[0], "cents from the root, in %s:", carlos_scale_names[carlos.scale]);
    carlos_all_off();
    demo = 0; demo_ms = now;
    for (int i = 0; i < demo_n; i++) carlos_play_q8(i, demo_q8[0][i], true);
}
static void demo_work(uint64_t now) {
    if (demo < 0) return;
    if (demo == 0 && now - demo_ms >= 1600) {                      /* the scale's, then equal temperament's */
        for (int i = 0; i < demo_n; i++) carlos_play_q8(i, 0, false);
        for (int i = 0; i < demo_n; i++) carlos_play_q8(4 + i, demo_q8[1][i], true);
        demo = 1; demo_ms = now;
    } else if (demo == 1 && now - demo_ms >= 1600) {
        for (int i = 0; i < demo_n; i++) carlos_play_q8(4 + i, 0, false);
        demo = 2;                                                  /* the numbers stay up */
    }
}

static bool key(uint8_t code, bool down, uint64_t now) {
    int k = step_of(code);
    if (k >= 0) { if (down != held[k]) { held[k] = down; carlos_key(k, down); } return true; }
    if (!down) return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_TAB || code == KEY_ENTER ||
                      code == KEY_PGUP || code == KEY_PGDN || code == KEY_SPACE;
    switch (code) {
    case KEY_UP:    knob = (knob + KNOBS - 1) % KNOBS; return true;
    case KEY_DOWN:  knob = (knob + 1) % KNOBS; return true;
    case KEY_LEFT:  turn(knob, -1); return true;
    case KEY_RIGHT: turn(knob, +1); return true;
    case KEY_TAB:   turn(K_SCALE, +1); return true;
    case KEY_PGUP:  turn(K_OCTAVE, +1); return true;
    case KEY_PGDN:  turn(K_OCTAVE, -1); return true;
    case KEY_SPACE: turn(K_MONO, 1); return true;
    case KEY_ENTER: demo_start(now); return true;
    }
    return false;
}

/* the touchpad: a ribbon two octaves long, from C of the octave, in the scale's steps or free */
static bool on_ribbon;
static void pointer(uint64_t now) {
    (void)now;
    bool on = pad.f[0].on;
    bool mouse = ptr.down && ui_in(ruler_px, ptr.x, ptr.y);
    if (!on && !mouse) { if (on_ribbon) { carlos_ribbon(0, false); on_ribbon = false; } return; }
    int x32 = on ? pad.f[0].x : (ptr.x - ruler_px.x) * 32767 / MAX(1, ruler_px.w - 1);
    int32_t q8 = carlos_pitch_q8(0) + (int32_t)((int64_t)CLAMP(x32, 0, 32767) * 24 * 256 / 32767);
    if (carlos.ribbon_steps) {                                     /* the nearest step */
        int32_t step = carlos_step_c100[carlos.scale], c100 = (q8 - carlos_pitch_q8(0)) * 10000 / 256;
        q8 = carlos_pitch_q8((c100 + step / 2) / step);
    }
    carlos_ribbon(q8, true); on_ribbon = true;
}

/* ---- the picture ---- */
static void ruler(struct rect r) {
    const struct font *f = text_font();
    int32_t lo = carlos_pitch_q8(0), span = 24 * 256;
    int ya = r.y + r.h / 3, yb = r.y + r.h * 2 / 3;
    gfx_hline(r.x, ya, r.w, ramp(R_GRAY, 8)); gfx_hline(r.x, yb, r.w, ramp(R_AMBER, 8));
    for (int s = 0; s <= 24; s++) {                                /* equal temperament: a tick a semitone, C taller */
        int x = r.x + s * (r.w - 1) / 24;
        gfx_vline(x, ya - (s % 12 ? f->height / 3 : f->height / 2), s % 12 ? f->height / 3 : f->height / 2, ramp(R_GRAY, s % 12 ? 9 : 13));
        if (note_letters[s % 12] && r.w / 24 > f->width) gfx_text(x - f->width / 2, ya - f->height - f->height / 2, note_letters[s % 12], f, ramp(R_GRAY, 11), -1, 1);
    }
    gfx_text(r.x + 2, ya + 3, "equal", f, ramp(R_GRAY, 9), -1, 1);
    gfx_text(r.x + 2, yb - f->height - 3, carlos_scale_names[carlos.scale], f, ramp(R_AMBER, 11), -1, 1);
    int32_t step = carlos_step_c100[carlos.scale];
    for (int k = 0; k * step <= 240000; k++) {                     /* the scale's steps */
        int x = r.x + (int)((int64_t)k * step * (r.w - 1) / 240000);
        gfx_vline(x, yb, f->height / 3 + (k % 5 == 0 ? 2 : 0), ramp(R_AMBER, 12));
        if (k % 5 == 0 && k) {                                      /* every fifth step: its cents */
            char c[8]; snfmt(c, sizeof c, "%d", (int)(k * step / 100));
            if (x + gfx_text_width(c, f, 1) / 2 < r.x + r.w) gfx_text(x - gfx_text_width(c, f, 1) / 2, yb + f->height / 2 + 2, c, f, ramp(R_AMBER, 9), -1, 1);
        }
    }
    static const int pure[3] = { 4, 7, 10 };                        /* 5:4, 3:2, 7:4 from the root, twice */
    for (int o = 0; o < 2; o++) for (int i = 0; i < 3; i++) {
        int x = r.x + (int)((int64_t)(o * 120000 + pure_c100(pure[i])) * (r.w - 1) / 240000);
        gfx_line(x - 3, (ya + yb) / 2 - 4, x, (ya + yb) / 2, ramp(R_GREEN, 13)); gfx_line(x + 3, (ya + yb) / 2 - 4, x, (ya + yb) / 2, ramp(R_GREEN, 13));
    }
    int32_t q[CARLOS_KEYS]; int n = carlos_sounding(q, CARLOS_KEYS);
    for (int i = 0; i < n; i++) {                                  /* what sounds now */
        int x = r.x + (int)((int64_t)(q[i] - lo) * (r.w - 1) / span);
        if (x >= r.x && x < r.x + r.w) gfx_fill(x - 1, r.y + 2, 3, r.h - 4, C_BRIGHT);
    }
    for (int d = 0; d < 2 && demo >= 0 && demo < 2; d++) for (int i = 0; i < demo_n; i++) if (d == demo) {
        int x = r.x + (int)((int64_t)(demo_q8[d][i] - lo) * (r.w - 1) / span);
        if (x >= r.x && x < r.x + r.w) gfx_fill(x - 1, r.y + 2, 3, r.h - 4, d ? ramp(R_GRAY, 14) : ramp(R_AMBER, 15));
    }
}
static void patch(struct rect r) {                                 /* VCO, VCF, VCA and the contour, in boxes, patched */
    const struct font *f = text_font();
    int bw = r.w / 5, bh = f->height * 2, y = r.y + (r.h - bh) / 2;
    const char *const box[4] = { "VCO", "VCF", "VCA", bw >= f->width * 9 ? "CONTOUR" : "ENV" };
    gfx_clip(r.x, r.y, r.w, r.h);
    for (int i = 0; i < 4; i++) {
        int x = r.x + i * (r.w - bw) / 3;
        gfx_round(x, y, bw, bh, 6, ramp(R_PANEL, 3), ramp(R_AMBER, 9));
        gfx_text(x + (bw - gfx_text_width(box[i], f, 1)) / 2, y + bh / 4, box[i], f, C_AMBER, -1, 1);
        if (i < 2) gfx_line(x + bw, y + bh / 2, x + (r.w - bw) / 3, y + bh / 2, ramp(R_CYAN, 12));
    }
    int xe = r.x + 3 * (r.w - bw) / 3, xf = r.x + (r.w - bw) / 3;  /* the contour to the filter and the amplifier */
    gfx_line(xe + bw / 2, y + bh, xe + bw / 2, y + bh + f->height / 2, ramp(R_GREEN, 12));
    gfx_line(xe + bw / 2, y + bh + f->height / 2, xf + bw / 2, y + bh + f->height / 2, ramp(R_GREEN, 12));
    gfx_line(xf + bw / 2, y + bh + f->height / 2, xf + bw / 2, y + bh, ramp(R_GREEN, 12));
    gfx_noclip();
}

static void draw(uint64_t now) {
    demo_work(now);
    struct lin_layout L; lineage_layout(&L, 32);
    char t[80]; snfmt(t, sizeof t, "CARLOS · %s · %s", carlos_scale_names[carlos.scale], carlos.mono ? "one voice, gliding" : "chords");
    ui_panel(L.x, L.y, L.pw, L.ph, t, C_AMBER);
    int rh = MAX(4, (L.ph - 2) / 2);
    struct rect r;
    uint32_t key = ui_hash_int(ui_hash_int(UI_HASH0, carlos.scale | carlos.octave << 4 | (demo + 1) << 8), (int32_t)(now / 33));
    if (ui_canvas_keyed(&r, L.x + 2, L.y + 1, L.pw - 4, rh, C_BG, key)) ruler(r);
    ruler_px = r;
    text_str_n(L.x + 2, L.y + 1 + rh, "equal temperament above · the scale below · ▼ pure 5:4 3:2 7:4 · a finger on the pad: a ribbon", L.pw - 4, C_DIM, C_PANEL);
    int py = L.y + 2 + rh, ph = L.y + L.ph - 1 - py;
    if (ph >= 4) {
        uint32_t k2 = ui_hash_int(UI_HASH0, carlos.cutoff | carlos.reso << 8 | carlos.contour << 16);
        if (ph > 6 && ui_canvas_keyed(&r, L.x + 2, py, (L.pw - 4) / 2, ph - 1, C_BG, k2)) patch(r);
        int tx = L.x + 2 + (L.pw - 4) / 2 + 2, tw = L.pw - 4 - (L.pw - 4) / 2 - 2;
        for (int i = 0; i < 3 && demo >= 0; i++) text_str_n(tx, py + 1 + i, demo_text[i], tw, i ? C_TEXT : C_GREEN, C_PANEL);
        if (demo < 0) text_str_n(tx, py + 1, "Enter: the omnichord's chord in this scale, then in equal", tw, C_DIM, C_PANEL);
    }
    ui_panel(L.kx, L.y, L.kw, L.ph, "MOOG", C_CYAN);
    for (int i = 0, ry = L.y + 2; i < KNOBS && ry < L.y + L.ph - 1; i++, ry++) {
        char v[24];
        switch (i) {
        case K_SCALE:   snfmt(v, sizeof v, "%s", carlos_scale_names[carlos.scale]); break;
        case K_OCTAVE:  snfmt(v, sizeof v, "C%d", carlos.octave); break;
        case K_MONO:    snfmt(v, sizeof v, "%s", carlos.mono ? "one (mono)" : "chords"); break;
        case K_GLIDE:   snfmt(v, sizeof v, "%d ms", carlos.glide * carlos.glide / 10); break;
        case K_WAVE:    snfmt(v, sizeof v, "%s", carlos_wave_names[carlos.wave % 3]); break;
        case K_CUTOFF:  snfmt(v, sizeof v, "%d", carlos.cutoff); break;
        case K_RESO:    snfmt(v, sizeof v, "%d", carlos.reso); break;
        case K_CONTOUR: snfmt(v, sizeof v, "%d", carlos.contour); break;
        case K_ATTACK:  snfmt(v, sizeof v, "%d", carlos.attack); break;
        case K_DECAY:   snfmt(v, sizeof v, "%d", carlos.decay); break;
        case K_SUSTAIN: snfmt(v, sizeof v, "%d", carlos.sustain); break;
        case K_RELEASE: snfmt(v, sizeof v, "%d", carlos.release); break;
        case K_LEVEL:   snfmt(v, sizeof v, "%d", carlos.level); break;
        default:        snfmt(v, sizeof v, "%s", carlos.ribbon_steps ? "steps" : "free"); break;
        }
        lineage_knob(L.kx + 1, ry, L.kw - 2, knob_names[i], v, i == knob);
    }
    ui_lesson(L.x, L.ly, text_cols() - 4, L.lh, &lesson_carlos);
    FOOTER("Z-/ A-' Q-]", "steps up the scale", "TAB", "scale", "SPACE", "one voice / chords", "ENTER", "the chord, compared",
           "↑ ↓ ← →", "knobs", "PGUP PGDN", "octave", "", "the touchpad: a ribbon");
}

const struct view lin_carlos = { "CARLOS", key, 0, pointer, draw, 0, "1968" };
