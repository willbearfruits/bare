/* MIX: the mixer. A strip per channel (PLAY, SEQ, RHYTHM, INPUT, STRETCH, TAPE) with its meter, fader, pan, echo and
   reverb sends, mute and solo; the master with the volume and the limiter at work; and the effects along the bottom:
   the reverb and the echo the sends feed, and the master's filter, drive and crusher. The INPUT strip picks the line
   in or a microphone, heard only when MON is on. */
#include "ui.h"
#include "gfx.h"
#include "mix.h"
#include "fx.h"
#include "audio.h"
#include "keys.h"
#include "platform.h"
#include "doomhost.h"

#define MASTER MIX_CHANNELS
/* the DOOM strip is there once Doom has been started (typing iddqd): until then nothing gives it away */
static int strips(void) { return doom_started() ? MIX_CHANNELS : CH_DOOM; }
enum { FX_REVERB, FX_ECHO, FX_FILTER, FX_DRIVE, FX_CRUSH, FX_UNITS };
#define FX0 (MASTER + 1)                          /* sel beyond the master: the effect units */
static int sel, knob, fxp;                        /* sel: a channel, MASTER or FX0 + unit; knob: 0 pan 1 echo 2 reverb */
static struct rect fader_px[MIX_CHANNELS + 1], knob_px[MIX_CHANNELS][3], mute_px[MIX_CHANNELS + 1], solo_px[MIX_CHANNELS],
                   src_px, fxp_px[FX_UNITS][3], fxon_px[FX_UNITS];
static int drag = -1;                             /* what the pointer holds: fader c, 20 + 3c + k a knob, 60 + 3u + p an effect */

/* the effects' parameters: a name, a value 0..max, and how it reads */
static const char *const fx_names[FX_UNITS] = { "REVERB", "ECHO", "FILTER", "DRIVE", "CRUSH" };
static const uint8_t fx_nparams[FX_UNITS] = { 3, 2, 3, 1, 2 };
static uint8_t *fx_param(int u, int p, int *max) {
    static uint8_t echo_on_dummy;
    switch (u) {
    case FX_REVERB: *max = 100; return p == 0 ? &fx.rev_size : p == 1 ? &fx.rev_damp : &fx.rev_level;
    case FX_ECHO:   if (p == 0) { *max = FX_ECHO_DIVS - 1; return &fx.echo_div; } *max = 90; return &fx.echo_feedback;
    case FX_FILTER: if (p == 0) { *max = 2; return &fx.filter_mode; } if (p == 1) { *max = 127; return &fx.filter_cut; } *max = 100; return &fx.filter_res;
    case FX_DRIVE:  *max = 100; return &fx.drive;
    case FX_CRUSH:  if (p == 0) { *max = 16; return &fx.crush_bits; } *max = 32; return &fx.crush_rate;
    }
    *max = 1; return &echo_on_dummy;
}
static const char *fx_param_name(int u, int p) {
    static const char *const n[FX_UNITS][3] = { { "size", "damp", "level" }, { "time", "repeats", "" }, { "mode", "cutoff", "res" },
                                                { "amount", "", "" }, { "bits", "rate", "" } };
    return n[u][p];
}
static bool *fx_on(int u) {
    static bool always = true;
    return u == FX_FILTER ? &fx.filter_on : u == FX_DRIVE ? &fx.drive_on : u == FX_CRUSH ? &fx.crush_on : &always;
}
static void fx_step(int u, int p, int d) {
    int max; uint8_t *v = fx_param(u, p, &max);
    int step = max >= 90 ? 5 : 1, lo = (u == FX_CRUSH) ? 1 : 0;
    *v = (uint8_t)CLAMP((int)*v + d * step, lo, max);
    if (u != FX_REVERB && u != FX_ECHO) *fx_on(u) = true;           /* turning an insert's knob turns it on */
}

/* a fader's travel: the top half is -12 .. +12 dB, the bottom half -60 .. -12, the very bottom is off */
static int db_pos(int db, int h) {
    if (db < MIX_DB_MIN) return 0;
    int p = db >= -12 ? 500 + (db + 12) * 500 / 24 : 50 + (db + 60) * 450 / 48;
    return p * h / 1000;
}
static int pos_db(int px, int h) {
    int p = px * 1000 / MAX(1, h);
    if (p < 25) return MIX_DB_OFF;
    return p >= 500 ? CLAMP(-12 + (p - 500) * 24 / 500, -12, MIX_DB_MAX) : CLAMP(-60 + (p - 50) * 48 / 450, MIX_DB_MIN, -12);
}
/* a level (Q15, 32768 = 0 dB) in whole dB below full scale, rounded to the nearest */
static int down_db(int32_t q15, int max) {
    int db = 0;
    while (db < max && (mix_gain_q12(-db) + mix_gain_q12(-db - 1)) * 4 > q15) db++;
    return db;
}
static int level_db(int32_t peak) { return peak < 33 ? MIX_DB_OFF : -down_db(peak, -MIX_DB_MIN); }   /* a meter */

static void input_next(void) {
    const char *names[8]; int n = plat_audio_inputs(names, 8);
    int next = mix.input + 1 >= n ? -1 : mix.input + 1;
    if (!mix_set_input(next)) mix_set_input(-1);
}

static void step_db(int c, int d) {
    if (c == MASTER) { audio_volume_step(d > 0 ? 1 : -1); return; }
    struct mix_channel *ch = &mix.ch[c];
    int db = ch->db < MIX_DB_MIN ? (d > 0 ? MIX_DB_MIN : MIX_DB_OFF) : ch->db + d;
    ch->db = (int8_t)(db < MIX_DB_MIN ? MIX_DB_OFF : MIN(db, MIX_DB_MAX));
}

static bool key(uint8_t code, bool down, uint64_t now) {
    (void)now;
    if (!down) return false;
    int units = FX0 + FX_UNITS;
    struct mix_channel *ch = sel < MASTER ? &mix.ch[sel] : 0;
    bool unit = sel >= FX0; int u = sel - FX0;
    switch (code) {
    case KEY_LEFT:  sel = (sel + units - 1) % units; if (sel == strips() && sel < MASTER) sel = strips() - 1; fxp = 0; return true;
    case KEY_RIGHT: sel = (sel + 1) % units; if (sel == strips() && sel < MASTER) sel = MASTER; fxp = 0; return true;
    case KEY_TAB:   knob = (knob + (ui_shift ? 2 : 1)) % 3; return true;
    case KEY_UP:    if (unit) fxp = (fxp + fx_nparams[u] - 1) % fx_nparams[u]; else step_db(sel, 1); return true;
    case KEY_DOWN:  if (unit) fxp = (fxp + 1) % fx_nparams[u]; else step_db(sel, -1); return true;
    case KEY_PGUP:  if (!unit) for (int i = 0; i < 6; i++) step_db(sel, 1); return true;
    case KEY_PGDN:  if (!unit) for (int i = 0; i < 6; i++) step_db(sel, -1); return true;
    case KEY_HOME:  if (ch) ch->db = 0; else if (!unit) audio_set_volume_index(3); return true;
    case KEY_END:   if (ch) ch->db = MIX_DB_OFF; return true;
    case KEY_ENTER:
        if (ch) ch->mute = !ch->mute;
        else if (!unit) audio_toggle_mute();
        else if (u == FX_ECHO) audio_set_echo(!audio_echo());
        else if (u != FX_REVERB) *fx_on(u) = !*fx_on(u);
        return true;
    case KEY_BACKSPACE: if (ch) ch->solo = !ch->solo; return true;
    case '[': case ']': {                             /* the knob in focus, or the effect's parameter */
        int d = code == '[' ? -1 : 1;
        if (unit) fx_step(u, fxp, d);
        else if (ch && knob == 0) ch->pan = (int8_t)CLAMP(ch->pan + d * 10, -100, 100);
        else if (ch && knob == 1) ch->echo = (uint8_t)CLAMP(ch->echo + d * 10, 0, 100);
        else if (ch) ch->reverb = (uint8_t)CLAMP(ch->reverb + d * 10, 0, 100);
        return true; }
    case '\\': input_next(); return true;
    case '`': mix.in_mono = !mix.in_mono; return true;
    }
    return false;
}

static void pointer(uint64_t now) {
    (void)now;
    if (!ptr.down) { drag = -1; return; }
    if (ptr.pressed) {
        drag = -1;
        for (int c = 0; c <= MASTER; c++) {
            if (ui_in(fader_px[c], ptr.x, ptr.y)) { drag = c; sel = c; }
            if (ui_in(mute_px[c], ptr.x, ptr.y)) { if (c < MASTER) mix.ch[c].mute = !mix.ch[c].mute; else audio_toggle_mute(); sel = c; return; }
            if (c == MASTER) continue;
            if (ui_in(solo_px[c], ptr.x, ptr.y)) { mix.ch[c].solo = !mix.ch[c].solo; sel = c; return; }
            for (int k = 0; k < 3; k++) if (ui_in(knob_px[c][k], ptr.x, ptr.y)) { drag = 20 + 3 * c + k; sel = c; knob = k; }
        }
        for (int u = 0; u < FX_UNITS; u++) {
            if (ui_in(fxon_px[u], ptr.x, ptr.y)) { sel = FX0 + u; if (u == FX_ECHO) audio_set_echo(!audio_echo()); else if (u != FX_REVERB) *fx_on(u) = !*fx_on(u); return; }
            for (int p = 0; p < fx_nparams[u]; p++) if (ui_in(fxp_px[u][p], ptr.x, ptr.y)) { drag = 60 + 3 * u + p; sel = FX0 + u; fxp = p; }
        }
        if (ui_in(src_px, ptr.x, ptr.y)) { input_next(); sel = CH_INPUT; return; }
    }
    if (drag < 0) return;
    if (drag <= MASTER) {
        struct rect r = fader_px[drag];
        int db = pos_db(r.y + r.h - 1 - ptr.y, r.h - 1);
        if (drag < MASTER) mix.ch[drag].db = (int8_t)db;
        else audio_set_volume_index(db < -40 ? 20 : CLAMP(-db / 2, 0, 20));
    } else if (drag >= 60) {
        int u = (drag - 60) / 3, p = (drag - 60) % 3, max;
        struct rect r = fxp_px[u][p];
        uint8_t *v = fx_param(u, p, &max);
        *v = (uint8_t)CLAMP((ptr.x - r.x) * (max + 1) / MAX(1, r.w), u == FX_CRUSH ? 1 : 0, max);
        if (u != FX_REVERB && u != FX_ECHO) *fx_on(u) = true;
    } else {
        int c = (drag - 20) / 3, k = (drag - 20) % 3;
        struct rect r = knob_px[c][k];
        if (k == 0) { int p = CLAMP((ptr.x - r.x) * 200 / MAX(1, r.w - 1) - 100, -100, 100); mix.ch[c].pan = (int8_t)(p > -8 && p < 8 ? 0 : p); }   /* the centre catches */
        else { uint8_t v = (uint8_t)CLAMP((ptr.x - r.x) * 100 / MAX(1, r.w - 1), 0, 100); if (k == 1) mix.ch[c].echo = v; else mix.ch[c].reverb = v; }
    }
}

/* ---- drawing ---- */
static void meter(int x, int y, int h, int32_t l, int32_t r) {
    struct rect m;
    int dl = level_db(l), dr = level_db(r);
    if (!ui_canvas_keyed(&m, x, y, 2, h, C_BG, ui_hash_int(ui_hash_int(UI_HASH0, dl), dr * 7 + h))) return;
    int bw = MAX(2, m.w / 2 - 2);
    for (int side = 0; side < 2; side++) {
        int lit = db_pos(side ? dr : dl, m.h), bx = m.x + 1 + side * (bw + 2);
        for (int i = 0; i < m.h; i += 2) {
            int zone = pos_db(i, m.h);
            uint8_t on = zone > -3 ? ramp(R_RED, 12) : zone > -12 ? ramp(R_AMBER, 12) : ramp(R_GREEN, 12);
            uint8_t off = zone > -3 ? ramp(R_RED, 2) : zone > -12 ? ramp(R_AMBER, 2) : ramp(R_GREEN, 2);
            gfx_hline(bx, m.y + m.h - 1 - i, bw, i < lit ? on : off);
        }
    }
}

static void fader(int c, int x, int y, int w, int h, int db, bool on) {
    struct rect r;
    bool keyed = ui_canvas_keyed(&r, x, y, w, h, C_BG, ui_hash_int(ui_hash_int(UI_HASH0, db * 4 + on), sel == c));
    fader_px[c] = r;
    if (!keyed) return;
    int cx = r.x + r.w / 2, travel = r.h - 1;
    static const int8_t ticks[] = { 12, 6, 0, -6, -12, -24, -36, -60 };
    const struct font *f = text_font();
    int kw = MIN(r.w - 4, f->width * 4), kh = MAX(6, f->height / 2);
    bool labels = cx - r.x - kw / 2 - 4 >= f->width * 3;
    for (unsigned i = 0; i < sizeof ticks; i++) {
        int ty = r.y + travel - db_pos(ticks[i], travel);
        gfx_hline(cx - (ticks[i] ? 5 : 9), ty, ticks[i] ? 11 : 19, ticks[i] ? ramp(R_PANEL, 7) : ramp(R_PANEL, 12));
        if (!labels || (ticks[i] == 6 && travel < 12 * f->height)) continue;
        char t[6]; snfmt(t, sizeof t, "%d", ticks[i]);
        int tw = gfx_text_width(t, f, 1);
        gfx_text(cx - kw / 2 - 4 - tw, CLAMP(ty - f->height / 2, r.y, r.y + r.h - f->height), t, f, ticks[i] ? ramp(R_PANEL, 10) : C_DIM, -1, 1);
    }
    gfx_vline(cx, r.y + 2, r.h - 4, ramp(R_PANEL, 9));
    int ky = r.y + travel - db_pos(db, travel);
    gfx_round(cx - kw / 2, CLAMP(ky - kh / 2, r.y, r.y + r.h - kh), kw, kh, 2, sel == c ? C_AMBER : on ? C_TEXT : C_DIM, -1);
    gfx_hline(cx - kw / 2 + 2, CLAMP(ky, r.y + 1, r.y + r.h - 2), kw - 4, C_BLACK);
}

static void db_text(char *out, int cap, int db) { if (db < MIX_DB_MIN) snfmt(out, (size_t)cap, "OFF"); else snfmt(out, (size_t)cap, "%+d dB", db); }

static void strip(int c, int x, int y, int w, int fh) {
    struct mix_channel *ch = &mix.ch[c];
    bool on = sel == c, heard = mix_heard(c);
    static const char *const tight[MIX_CHANNELS] = { "PLAY", "SEQ", "RHY", "IN", "STR", "TAPE", "TOUCH", "ANS", "UPIC", "CLOUD", "DOOM" };
    const char *name = ui_cells(mix_names[c]) > w - 2 ? tight[c] : mix_names[c];     /* narrow strips: short names */
    int nw = ui_cells(name);
    text_fill(x, y, w - 1, 1, ' ', C_TEXT, on ? C_AMBER : C_PANEL);
    text_str(x + (w - 1 - nw) / 2, y, name, on ? C_BLACK : heard ? C_BRIGHT : C_DIM, on ? C_AMBER : C_PANEL);
    meter(x + 1, y + 1, fh, ch->vu_l, ch->vu_r);
    fader(c, x + 3, y + 1, w - 5, fh, ch->db, heard);
    char t[24]; db_text(t, sizeof t, ch->db);
    text_str(x + (w - 1 - ui_cells(t)) / 2, y + fh + 1, t, heard ? C_TEXT : C_DIM, C_PANEL);
    int bw = w - 7;
    static const char *const kn[3] = { "PAN", "ECH", "REV" };
    for (int k = 0; k < 3; k++) {                                        /* the knob in focus is lit on the chosen strip */
        text_str(x + 1, y + fh + 2 + k, kn[k], on && knob == k ? C_AMBER : C_DIM, C_PANEL);
        knob_px[c][k] = text_rect(x + 5, y + fh + 2 + k, bw, 1);
    }
    struct rect pr;
    if (ui_canvas_keyed(&pr, x + 5, y + fh + 2, bw, 1, C_PANEL, ui_hash_int(UI_HASH0, ch->pan))) {
        int mid = pr.x + pr.w / 2, px = pr.x + (ch->pan + 100) * (pr.w - 1) / 200, th = MAX(2, pr.h / 8);
        gfx_fill(pr.x, pr.y + (pr.h - th) / 2, pr.w, th, ramp(R_PANEL, 7));
        gfx_vline(mid, pr.y + pr.h / 4, pr.h / 2, ramp(R_PANEL, 11));
        gfx_disc(px, pr.y + pr.h / 2, MAX(2, pr.h / 5), ch->pan ? C_CYAN : C_TEXT);
    }
    ui_bar(x + 5, y + fh + 3, bw, ch->echo, 100, C_GREEN, C_PANEL);
    ui_bar(x + 5, y + fh + 4, bw, ch->reverb, 100, C_CYAN, C_PANEL);
    const char *m = c == CH_INPUT ? " MON " : " M ";
    bool lit = c == CH_INPUT ? !ch->mute : ch->mute;
    text_str(x + 1, y + fh + 5, m, lit ? C_BLACK : C_DIM, lit ? (c == CH_INPUT ? C_GREEN : C_RED) : C_BORDER);
    mute_px[c] = text_rect(x + 1, y + fh + 5, ui_cells(m), 1);
    text_str(x + 2 + ui_cells(m), y + fh + 5, " S ", ch->solo ? C_BLACK : C_DIM, ch->solo ? C_AMBER : C_BORDER);
    solo_px[c] = text_rect(x + 2 + ui_cells(m), y + fh + 5, 3, 1);
    if (c == CH_INPUT) {
        const char *names[8]; int n = plat_audio_inputs(names, 8);
        const char *src = mix.input >= 0 && mix.input < n ? names[mix.input] : n ? "OFF" : "NONE";
        text_str_n(x + 1, y + fh + 6, src, w - 2, mix.input >= 0 ? C_GREEN : C_DIM, C_PANEL);
        src_px = text_rect(x + 1, y + fh + 6, w - 2, 1);
        if (mix.input >= 0) ui_led(x + 1, y + fh + 7, plat_audio_input_jack(mix.input), C_GREEN, mix.in_mono ? "MONO" : "STEREO", C_PANEL);
    }
}

static void master(int x, int y, int w, int fh) {
    bool on = sel == MASTER;
    text_fill(x, y, w - 1, 1, ' ', C_TEXT, on ? C_AMBER : C_PANEL);
    text_str(x + (w - 7) / 2, y, "MASTER", on ? C_BLACK : C_BRIGHT, on ? C_AMBER : C_PANEL);
    meter(x + 1, y + 1, fh, audio_peak(0), audio_peak(1));
    int db = audio_muted() ? MIX_DB_OFF : audio_volume_db();
    fader(MASTER, x + 3, y + 1, 8, fh, db, !audio_muted());
    /* the limiter: how far it is turning the output down */
    int red = down_db(audio_limiter_q15(), 24);
    struct rect lr;
    if (ui_canvas_keyed(&lr, x + 12, y + 1, 2, fh, C_BG, ui_hash_int(UI_HASH0, red))) {
        int d = red * lr.h / 24;
        gfx_frame(lr.x + 2, lr.y, lr.w - 4, lr.h, ramp(R_PANEL, 6));
        if (d) gfx_fill(lr.x + 3, lr.y + 1, lr.w - 6, d, ramp(R_AMBER, 11));
    }
    text_str(x + 14, y + 1, "LIMIT", C_DIM, C_PANEL);
    char t[24]; snfmt(t, sizeof t, red ? "-%d dB" : "0 dB", red);
    text_str(x + 14, y + 2, t, red ? C_AMBER : C_DIM, C_PANEL);
    text_str(x + 14, y + 4, "ECHO", C_DIM, C_PANEL);
    snfmt(t, sizeof t, audio_echo() ? "%u ms" : "off", audio_echo_ms());
    text_str(x + 14, y + 5, t, audio_echo() ? C_GREEN : C_DIM, C_PANEL);
    if (plat_audio_onebit()) text_str(x + 14, y + 7, "1-BIT", C_AMBER, C_PANEL);
    db_text(t, sizeof t, db);
    text_str(x + 2, y + fh + 1, audio_muted() ? "MUTE" : t, audio_muted() ? C_RED : C_TEXT, C_PANEL);
    text_str(x + 1, y + fh + 5, " M ", audio_muted() ? C_BLACK : C_DIM, audio_muted() ? C_RED : C_BORDER);
    mute_px[MASTER] = text_rect(x + 1, y + fh + 5, 3, 1);
    if (mix.in_xruns) { snfmt(t, sizeof t, "input gaps %u", mix.in_xruns); text_str_n(x + 1, y + fh + 7, t, w - 2, C_DIM, C_PANEL); }
}

/* the effects along the bottom: a column each, its parameters as bars, the chosen one lit */
static void effects(int x, int y, int w) {
    int cw = w / FX_UNITS;
    for (int u = 0; u < FX_UNITS; u++) {
        int cx = x + u * cw, bw = cw - 11;
        bool on = sel == FX0 + u, lit = u == FX_ECHO ? audio_echo() : *fx_on(u);
        text_fill(cx, y, cw - 1, 1, ' ', C_TEXT, on ? C_AMBER : C_PANEL);
        text_put(cx + 1, y, lit ? G_DISC : G_CIRCLE, lit ? (on ? C_BLACK : C_GREEN) : C_DIM, on ? C_AMBER : C_PANEL);
        fxon_px[u] = text_rect(cx + 1, y, 1, 1);
        text_str(cx + 3, y, fx_names[u], on ? C_BLACK : C_BRIGHT, on ? C_AMBER : C_PANEL);
        for (int p = 0; p < fx_nparams[u]; p++) {
            int max; uint8_t *v = fx_param(u, p, &max);
            bool sp = on && fxp == p;
            text_str(cx + 1, y + 1 + p, fx_param_name(u, p), sp ? C_AMBER : C_DIM, C_PANEL);
            fxp_px[u][p] = text_rect(cx + 9, y + 1 + p, bw, 1);
            char t[12];
            if (u == FX_ECHO && p == 0) { text_str(cx + 9, y + 1 + p, fx_echo_div_names[*v % FX_ECHO_DIVS], C_GREEN, C_PANEL); continue; }
            if (u == FX_FILTER && p == 0) { static const char *const m[3] = { "LOW", "BAND", "HIGH" }; text_str(cx + 9, y + 1 + p, m[*v % 3], C_CYAN, C_PANEL); continue; }
            if (u == FX_CRUSH) {                          /* the bits, and the rate the holding leaves */
                if (p == 0) snfmt(t, sizeof t, "%u bit", *v); else { uint32_t hz = audio_rate() / MAX(1, *v); snfmt(t, sizeof t, "%u.%u kHz", hz / 1000, hz % 1000 / 100); }
                text_str(cx + 9, y + 1 + p, t, lit ? C_PINK : C_DIM, C_PANEL); continue; }
            ui_bar(cx + 9, y + 1 + p, bw, *v, max, lit ? (u == FX_REVERB ? C_CYAN : C_GREEN) : C_DIM, C_PANEL);
        }
    }
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    ui_panel(1, 1, cols - 2, rows - 3, "MIXER", C_AMBER);
    int n = strips(), mw = cols >= 140 ? 28 : 22, sw = (cols - 4 - mw - 1) / n, fh = rows - 20, y = 2;
    for (int c = 0; c < n; c++) strip(c, 2 + c * sw, y, sw, fh);
    int mx = 2 + n * sw + 1, fy = y + fh + 8;
    for (int j = y; j < fy - 1; j++) text_put(mx - 1, j, G_VLINE, C_BORDER, C_PANEL);
    master(mx, y, cols - 3 - mx, fh);
    for (int i = 2; i < cols - 2; i++) text_put(i, fy - 1, G_HLINE, C_BORDER, C_PANEL);
    effects(2, fy, cols - 4);
    if (sel >= FX0) FOOTER("← →", "unit", "↑ ↓", "setting", "[ ]", "change", "ENTER", "on/off", "1-= Z-/", "play");
    else FOOTER("← →", "channel", "↑ ↓", "fader", "PGUP PGDN", "±6 dB", "HOME", "0 dB", "TAB", "pan/echo/reverb", "[ ]", "turn it",
                "ENTER", "mute / MON", "BKSP", "solo", "\\", "input", "`", "mono", "1-= Z-/", "play");
}

const struct page page_mix = { "MIX", "F8", KEY_F8, true, key, 0, pointer, 0, draw };
