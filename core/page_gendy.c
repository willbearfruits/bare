/* GENDY (XENAKIS, a view): the four GENDY patches (core/gendy.c), played anywhere as the sounds GENDY 1-4. The picture
   is an oscillator of the patch walking before your eyes: the newest period in front, the ones before it stacked
   behind, the mirrors dashed. The knobs are the patch; the letter rows play it and Space holds a drone to turn knobs
   against; the touchpad, or the mouse on the picture, sets the walk's two step sizes (across: the heights'; up: the
   lengths'), so a held note can be calmed or stirred by hand. */
#include "xen.h"
#include "harmony.h"
#include "gendy.h"
#include "synth.h"
#include "gfx.h"
#include "keys.h"
#include "undo.h"

#define TAG_KEYS 0x580                                    /* | note: the keyboard and MIDI here; 0x5FF the drone */
#define TAG_DRONE 0x5FF
enum { G_POINTS, G_ADIST, G_ASTEP, G_AMIRROR, G_INERTIA, G_DDIST, G_DSTEP, G_DMIRROR, G_DRIFT, G_ATTACK, G_DECAY, G_SUSTAIN,
       G_RELEASE, G_CUTOFF, G_RESO, G_LEVEL, GK };
static const char *const knob_names[GK] = { "points", "dist", "step", "mirror", "inertia", "dist", "step", "mirror", "drift",
                                            "attack", "decay", "sustain", "release", "cutoff", "reso", "level" };
static const char *const headings[GK] = { [G_ADIST] = "heights", [G_DDIST] = "lengths", [G_DRIFT] = "pitch", [G_ATTACK] = "envelope",
                                          [G_CUTOFF] = "filter" };
static int patch, knob = G_ASTEP, octave = 3;
static bool naming, drone; static char name[12]; static int name_len;
static bool held[128];
static struct rect pic_px;
static bool steering;

/* the picture's own oscillator and the periods it walked through */
#define HIST 14
static struct gendy_osc mon; static int mon_patch = -1, mon_points;
static struct { uint8_t n; uint16_t x[GENDY_POINTS + 1]; int16_t y[GENDY_POINTS + 1]; } hist[HIST];
static int hist_at;

static struct gendy_patch *P(void) { return &gendy_bank[patch]; }
static uint8_t *field(int k) {
    struct gendy_patch *p = P();
    switch (k) {
    case G_POINTS: return &p->points; case G_ADIST: return &p->adist; case G_ASTEP: return &p->astep; case G_AMIRROR: return &p->amirror;
    case G_INERTIA: return &p->inertia; case G_DDIST: return &p->ddist; case G_DSTEP: return &p->dstep; case G_DMIRROR: return &p->dmirror;
    case G_DRIFT: return &p->drift; case G_SUSTAIN: return &p->s_pct; case G_CUTOFF: return &p->cutoff; case G_RESO: return &p->reso;
    default: return &p->level;
    }
}
static uint16_t *ms_field(int k) { return k == G_ATTACK ? &P()->a_ms : k == G_DECAY ? &P()->d_ms : &P()->r_ms; }
static bool is_ms(int k) { return k == G_ATTACK || k == G_DECAY || k == G_RELEASE; }

static void turn(int k, int d, bool big) {
    if (is_ms(k)) {
        uint16_t *m = ms_field(k); int v = *m, step = big ? MAX(10, v / 4) : MAX(1, v / 20);
        *m = (uint16_t)CLAMP(v + d * step, 1, k == G_ATTACK ? 4000 : 8000);
        return;
    }
    uint8_t *f = field(k);
    int v = *f, lo = 0, hi = 100, step = big ? 10 : 1;
    switch (k) {
    case G_POINTS: lo = 2; hi = GENDY_POINTS; step = big ? 4 : 1; break;
    case G_ADIST: case G_DDIST: hi = GD_DISTS - 1; step = 1; v = (v + d + GD_DISTS) % GD_DISTS; *f = (uint8_t)v; return;
    case G_AMIRROR: lo = 5; break;
    case G_CUTOFF: hi = 127; step = big ? 16 : 2; break;
    }
    *f = (uint8_t)CLAMP(v + d * step, lo, hi);
}
static void knob_text(int k, char *out, int cap) {
    if (is_ms(k)) { snfmt(out, cap, "%u ms", *ms_field(k)); return; }
    int v = *field(k);
    switch (k) {
    case G_ADIST: case G_DDIST: snfmt(out, cap, "%s", gendy_dist_names[v % GD_DISTS]); break;
    case G_AMIRROR: case G_SUSTAIN: case G_LEVEL: snfmt(out, cap, "%d%%", v); break;
    case G_DMIRROR: { int r8 = 8 + v * 56 / 100; snfmt(out, cap, v ? "%d.%d to 1" : "equal", r8 * r8 / 64, r8 * r8 % 64 * 10 / 64); break; }
    case G_DRIFT: if (v) snfmt(out, cap, "±%d semitones", v * 24 / 100); else snfmt(out, cap, "in tune"); break;
    case G_CUTOFF: if (v >= 127) snfmt(out, cap, "open"); else snfmt(out, cap, "%d", v); break;
    default: snfmt(out, cap, "%d", v);
    }
}

static int preset(void) { return P_GD1 + patch; }
static void note(int n, uint8_t vel, bool on) {                  /* the key's tag is its own note, whatever it plays */
    if (n < 0 || n > 127) return;
    if (on) synth_note_on((uint8_t)harmony_note(n), vel, (uint8_t)preset(), (uint16_t)(TAG_KEYS | n));
    else synth_note_off_tag((uint16_t)(TAG_KEYS | n));
}
static void edit(uint64_t now) { char w[24]; snfmt(w, sizeof w, "GENDY patch %d", patch + 1); undo_one(U_GENDY, patch, w, now); }

static bool typing(void) { return naming; }
static bool key(uint8_t code, bool down, uint64_t now) {
    if (naming) {
        if (!down) return true;
        if (code == KEY_ENTER) { naming = false; if (name_len) snfmt(P()->name, sizeof P()->name, "%s", name); }
        else if (code == KEY_ESC) naming = false;
        else if (code == KEY_BACKSPACE) { if (name_len) name[--name_len] = 0; }
        else if (code >= ' ' && code < 0x7F && name_len < (int)sizeof name - 1) { char c = (char)code; if (c >= 'a' && c <= 'z') c -= 32; name[name_len++] = c; name[name_len] = 0; }
        return true;
    }
    int n = xen_key_note(code, octave);
    if (n >= 0) {
        if (down != held[n & 127]) { held[n & 127] = down; note(n, 110, down); }
        return true;
    }
    if (!down) return code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == '[' || code == ']' ||
                      code == KEY_HOME || code == KEY_END || code == KEY_PGUP || code == KEY_PGDN || code == KEY_SPACE || code == KEY_ENTER;
    switch (code) {
    case KEY_UP:    knob = (knob + GK - 1) % GK; return true;
    case KEY_DOWN:  knob = (knob + 1) % GK; return true;
    case KEY_LEFT: case KEY_RIGHT: case '[': case ']':
        edit(now); turn(knob, code == KEY_LEFT || code == '[' ? -1 : 1, code == '[' || code == ']'); return true;
    case KEY_HOME:  patch = (patch + GENDY_PATCHES - 1) % GENDY_PATCHES; return true;
    case KEY_END:   patch = (patch + 1) % GENDY_PATCHES; return true;
    case KEY_PGUP:  octave = MIN(octave + 1, 6); return true;
    case KEY_PGDN:  octave = MAX(octave - 1, 0); return true;
    case KEY_SPACE:                                           /* a drone on the keyboard's C, to turn knobs against */
        drone = !drone;
        if (drone) synth_note_on((uint8_t)(12 * (octave + 1)), 100, (uint8_t)preset(), TAG_DRONE); else synth_note_off_tag(TAG_DRONE);
        return true;
    case KEY_ENTER: naming = true; name_len = 0; name[0] = 0; return true;
    }
    return false;
}

static bool midi(uint8_t n, uint8_t vel, uint64_t now) { (void)now; note(n, vel, vel > 0); return true; }

/* the picture as an XY pad for the walk's step sizes: the mouse on it, or the touchpad's first finger */
static void pointer(uint64_t now) {
    int x = -1, y = -1;
    if (pad.f[0].on) { x = pad.f[0].x * 100 / 32767; y = (32767 - pad.f[0].y) * 100 / 32767; }
    else if (ptr.down && pic_px.w && (steering || (ptr.pressed && ui_in(pic_px, ptr.x, ptr.y)))) {
        x = CLAMP((ptr.x - pic_px.x) * 100 / MAX(1, pic_px.w - 1), 0, 100); y = CLAMP((pic_px.y + pic_px.h - 1 - ptr.y) * 100 / MAX(1, pic_px.h - 1), 0, 100);
    }
    if (x < 0) { steering = false; return; }
    if (!steering) edit(now);
    steering = true;
    P()->astep = (uint8_t)x; P()->dstep = (uint8_t)y;
}

static void walk_picture(void) {
    const struct gendy_patch *p = P();
    if (mon_patch != patch || mon_points != p->points) { gendy_osc_start(&mon, p, 0x5EED0000u + (uint32_t)patch); mon_patch = patch; mon_points = p->points; }
    for (int s = 0; s < 2; s++) {
        gendy_period(&mon, p);
        hist_at = (hist_at + 1) % HIST;
        hist[hist_at].n = mon.n;
        for (int i = 0; i <= mon.n; i++) { hist[hist_at].x[i] = (uint16_t)(mon.edge[i] >> 16); hist[hist_at].y[i] = mon.amp[i]; }
        hist[hist_at].x[mon.n] = 0xFFFF;
    }
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    int sw = 30, x = 2, y = XEN_TOP, pw = cols - 4 - sw - 1, ph = rows - y - 3;
    const struct gendy_patch *p = P();
    char t[64]; snfmt(t, sizeof t, "GENDY %d · %s · %d points", patch + 1, p->name, p->points);
    ui_panel(x, y, pw, ph, t, C_AMBER);
    static uint32_t frames; frames++;                         /* the walk drawn 30 times a second (15 on a 4K screen) */
    struct rect r;
    int slow = text_font()->height >= 32 ? 2 : 1;
    uint32_t key = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, cols << 16 | rows), (int32_t)(frames >> slow)), p->astep << 8 | p->dstep);
    bool fresh = ui_canvas_keyed(&r, x + 1, y + 1, pw - 2, ph - 2, C_BG, key);
    pic_px = r;
    if (fresh) walk_picture();
    int m = MAX(6, r.w / 40), dx = MAX(2, r.w / 70), dy = MAX(2, r.h / 44);
    int W = r.w - 2 * m - (HIST - 1) * dx, H = r.h - 2 * m - (HIST - 1) * dy;
    if (fresh && W > 40 && H > 40) {
        int bx = r.x + m, cy = r.y + m + (HIST - 1) * dy + H / 2;
        int mir = H / 2 * p->amirror / 100;
        for (int xx = bx; xx < bx + W; xx += 6) {                 /* the newest period's mirrors */
            gfx_fill(xx, cy - mir, 3, 1, ramp(R_RED, 9)); gfx_fill(xx, cy + mir, 3, 1, ramp(R_RED, 9));
        }
        gfx_fill(bx, cy, W, 1, ramp(R_PANEL, 5));
        for (int a = HIST - 1; a >= 0; a--) {                     /* the oldest at the back */
            int h = (hist_at - a + HIST) % HIST, n = hist[h].n;
            if (!n) continue;
            int ox = bx + a * dx, oy = cy - a * dy;
            uint8_t c = a ? ramp(R_CYAN, 13 - a * 12 / HIST) : ramp(R_AMBER, 15);
            int px0 = ox, py0 = oy - hist[h].y[0] * (H / 2) / 32768;
            for (int i = 1; i <= n; i++) {
                int px1 = ox + (int)((uint32_t)hist[h].x[i] * (uint32_t)W >> 16), py1 = oy - hist[h].y[i] * (H / 2) / 32768;
                xen_line(px0, py0, px1, py1, c);
                if (!a) gfx_fill(px0 - 1, py0 - 1, 3, 3, ramp(R_AMBER, 15));
                px0 = px1; py0 = py1;
            }
        }
        /* the step sizes, where the pad stands */
        int sx = r.x + p->astep * (r.w - 1) / 100, sy = r.y + r.h - 1 - p->dstep * (r.h - 1) / 100;
        gfx_fill(sx - 6, sy, 13, 1, ramp(R_GREEN, 12)); gfx_fill(sx, sy - 6, 1, 13, ramp(R_GREEN, 12));
    }
    char st[48]; snfmt(st, sizeof st, " step: heights %d, lengths %d ", p->astep, p->dstep);
    text_str(x + 2, y + ph - 1, st, C_DIM, C_PANEL);

    int kx = x + pw + 1;
    ui_panel(kx, y, sw, ph, naming ? "name it · Enter" : drone ? "GENDY · drone" : "GENDY", C_CYAN);
    int ry = y + 1;
    if (naming) { char nm[16]; snfmt(nm, sizeof nm, "%s_", name); text_str(kx + 3, ry, nm, C_AMBER, C_PANEL); }
    else { char pn[24]; snfmt(pn, sizeof pn, "%d of 4: %s", patch + 1, p->name); text_str_n(kx + 3, ry, pn, sw - 5, C_BRIGHT, C_PANEL); }
    ry++;
    for (int k = 0; k < GK && ry < y + ph - 1; k++) {
        if (headings[k]) { if (++ry >= y + ph - 1) break; text_str(kx + 3, ry, headings[k], C_GREEN, C_PANEL); ry++; }
        if (ry >= y + ph - 1) break;
        char v[24]; knob_text(k, v, sizeof v);
        bool on = k == knob;
        text_put(kx + 1, ry, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(kx + 3, ry, knob_names[k], on ? C_BRIGHT : C_TEXT, C_PANEL);
        text_str_n(kx + 12, ry, v, sw - 14, on ? C_AMBER : C_DIM, C_PANEL);
        ry++;
    }
    FOOTER("Z-/ Q-P", "play", "SPACE", "drone", "↑ ↓ ← →", "knobs", "HOME END", "patch", "ENTER", "name", "PGUP PGDN", "octave",
           "", "the touchpad: the walk's steps");
}

const struct view xen_gendy = { "GENDY", key, typing, pointer, draw, midi, "1991" };
