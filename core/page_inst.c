/* An instrument's page (core/inst.c), shown in PLAY's place when F1 steps to it. Its surface is what its file asks
   for: the touchpad as a strip (a finger across it is the pitch, pressing harder louder), a grid of pads (the
   touchpad stands for the grid when there is no strip: a finger a pad), the letter rows as its keyboard (chromatic,
   or up its scale); MIDI notes play it as they come. The knobs are the ones its file names; Space holds what is played
   (an arpeggio keeps going), or closes every note a toggle instrument has open. With bellows, a finger to and fro on
   the touchpad (or the mouse moving, or Enter held) pumps the air. Every key reaches it, so nothing falls through to
   the omnichord behind. */
#include "ui.h"
#include "gfx.h"
#include "inst.h"
#include "keys.h"

static int knob, octave_shift;                              /* the knob chosen; the keys' octave, ± */
static struct rect strip_px, pads_px;
static int finger_pad[FINGERS + 1] = { -1, -1, -1, -1, -1, -1 };
static bool strip_mouse, pumping;                          /* pumping: Enter held (the bellows) */
static int last_fx = -1, last_fy, last_px = -1, last_py;
static const char kb_low[] = "zsxdcvgbhnjm,l.;/", kb_high[] = "q2w3er5t6y7ui9o0p";

static const char *const nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static void note_name(int q8, char *out, int cap) { int n = (q8 + 128) >> 8; snfmt(out, cap, "%s%d", nm[n % 12], n / 12 - 1); }

/* the pitch a key of the letter rows plays, or -1 */
static int key_pitch(const struct inst *in, uint8_t code) {
    int k = -1;
    for (int i = 0; kb_low[i]; i++) if (code == (uint8_t)kb_low[i]) k = i;
    for (int i = 0; kb_high[i] && k < 0; i++) if (code == (uint8_t)kb_high[i]) k = 17 + i;
    if (k < 0 || in->keys == KEYS_OFF) return -1;
    int root = in->keys_root + octave_shift * 12;
    if (in->keys == KEYS_CHROMATIC) { int n = root + (k < 17 ? k : k - 17 + 12); return n >= 0 && n <= 127 ? n * 256 : -1; }
    return inst_step(in, CLAMP(root, 0, 127), k);                               /* up the scale: the upper row goes on from the lower */
}
static int pad_pitch(const struct inst *in, int p) { return inst_step(in, in->pads_root, p); }
static int32_t strip_pitch(const struct inst *in, int x32767) {  /* where across the strip, in its steps */
    int lo = in->strip_lo * 256, hi = in->strip_hi * 256;
    int32_t q = lo + (int32_t)((int64_t)(hi - lo) * CLAMP(x32767, 0, 32767) / 32767);
    if (in->steps == STEPS_SEMI) return (q + 128) & ~255;
    if (in->steps == STEPS_SCALE) {                          /* the nearest step of its scale */
        int32_t best = q, bd = 1 << 30;
        for (int k = 0; k < 128; k++) { int s = inst_step(in, in->strip_lo, k); if (s < 0 || s > hi + 256) break; int d = s > q ? s - q : q - s; if (d < bd) { bd = d; best = s; } }
        return best;
    }
    return q;
}

bool inst_page_key(int i, uint8_t code, bool down, uint64_t now) {
    (void)now;
    struct inst *in = &insts[i];
    int p = key_pitch(in, code);
    if (p >= 0) { inst_note(code, p, 105, down); return true; }
    if (code == KEY_ENTER) pumping = down && in->bellows;
    if (!down) return true;
    switch (code) {
    case KEY_UP:    if (in->nknobs) knob = (knob + in->nknobs - 1) % in->nknobs; break;
    case KEY_DOWN:  if (in->nknobs) knob = (knob + 1) % in->nknobs; break;
    case KEY_LEFT: case KEY_RIGHT: case '[': case ']': {
        if (!in->nknobs) break;
        int k = in->knob[knob % in->nknobs], v = inst_knob_get(in, k), d = code == KEY_LEFT || code == '[' ? -1 : 1;
        bool big = code == '[' || code == ']';
        int step = k == IK_ATTACK || k == IK_DECAY || k == IK_RELEASE || k == IK_GLIDE ? (big ? MAX(10, v / 4) : MAX(1, v / 16)) : k == IK_WAVE || k == IK_RATE || k == IK_OCTAVES ? 1 : big ? 10 : 2;
        inst_knob_set(in, k, v + d * step); inst_apply(i);
        break; }
    case KEY_PGUP:  octave_shift = MIN(octave_shift + 1, 3); break;
    case KEY_PGDN:  octave_shift = MAX(octave_shift - 1, -3); break;
    case KEY_SPACE: if (in->hold == HOLD_TOGGLE) inst_clear(); else inst_latch(!inst_latched()); break;
    }
    return true;                                             /* nothing falls through to the omnichord */
}

bool inst_page_midi(int i, uint8_t n, uint8_t vel, uint64_t now) { (void)i; (void)now; inst_note(512 + n, n * 256, vel ? vel : 64, vel > 0); return true; }

static int pad_at(int x, int y, const struct inst *in) {    /* the pad under a point of the grid, or -1 */
    if (!ui_in(pads_px, x, y) || !in->pads_w) return -1;
    int c = (x - pads_px.x) * in->pads_w / MAX(1, pads_px.w), r = (y - pads_px.y) * in->pads_h / MAX(1, pads_px.h);
    return (in->pads_h - 1 - r) * in->pads_w + c;            /* the bottom row first, as on a pad controller */
}
static void to_pad(const struct inst *in, int who, int pad) {
    if (pad == finger_pad[who]) return;
    if (finger_pad[who] >= 0) inst_note(256 + who, 0, 0, false);
    finger_pad[who] = pad;
    if (pad >= 0) { int p = pad_pitch(in, pad); if (p >= 0) inst_note(256 + who, p, 105, true); else finger_pad[who] = -1; }
}

/* the bellows: air from Enter held, and from a finger's (or the mouse's) travel where the touchpad isn't a strip or pads */
static int dist(int a, int b) { return a > b ? a - b : b - a; }
static void pump(const struct inst *in) {
    if (pumping) inst_pump(900);
    bool pad_free = !in->strip_hi && !in->pads_w;
    const struct finger *f = &pad.f[0];
    if (pad_free && f->on) {
        if (last_fx >= 0) inst_pump((dist(f->x, last_fx) + dist(f->y, last_fy)) * 3 / 4);
        last_fx = f->x; last_fy = f->y;
    } else last_fx = -1;
    if (pad_free && last_px >= 0) inst_pump((dist(ptr.x, last_px) + dist(ptr.y, last_py)) * 60);
    last_px = ptr.x; last_py = ptr.y;
}

void inst_page_pointer(int i, uint64_t now) {
    (void)now;
    const struct inst *in = &insts[i];
    if (in->bellows) pump(in);
    if (in->strip_hi) {                                      /* the strip: the touchpad's first finger, or the mouse on it */
        const struct finger *f = &pad.f[0];
        if (f->on) inst_strip(true, strip_pitch(in, f->x), (uint8_t)(f->z > 0 && f->z != 60 ? CLAMP(40 + (f->z - 30) * 87 / 70, 40, 127) : 100));
        else if (ptr.down && strip_px.w && (strip_mouse || (ptr.pressed && ui_in(strip_px, ptr.x, ptr.y)))) {
            strip_mouse = true; inst_strip(true, strip_pitch(in, (ptr.x - strip_px.x) * 32767 / MAX(1, strip_px.w - 1)), 100);
        } else { strip_mouse = false; inst_strip(false, 0, 0); }
    } else if (in->pads_w)                                   /* no strip: the touchpad is the grid, a finger a pad */
        for (int k = 0; k < FINGERS; k++) {
            const struct finger *f = &pad.f[k];
            to_pad(in, k, f->on ? (in->pads_h - 1 - f->y * in->pads_h / 32768) * in->pads_w + f->x * in->pads_w / 32768 : -1);
        }
    if (in->pads_w) to_pad(in, FINGERS, ptr.down ? pad_at(ptr.x, ptr.y, in) : -1);
}

void inst_page_draw(int i, uint64_t now) {
    (void)now;
    const struct inst *in = &insts[i];
    int cols = text_cols(), rows = text_rows();
    int sw = 30, x = 2, y = 2, pw = cols - 4 - sw - 1, ph = rows - y - 3;
    char t[80]; snfmt(t, sizeof t, "%s%s%s", in->name, in->by[0] ? " · by " : "", in->by);
    ui_panel(x, y, pw, ph, t, C_AMBER);
    int ry = y + 1;
    if (in->about[0]) text_str_n(x + 2, ry++, in->about, pw - 4, C_TEXT, C_PANEL);
    if (in->errors) {
        char e[96]; snfmt(e, sizeof e, "%s: %d mistake%s, %s", in->file, in->errors, in->errors > 1 ? "s" : "", in->err);
        text_str_n(x + 2, ry++, e, pw - 4, C_RED, C_PANEL);
    }
    int32_t sound[20]; int ns = inst_sounding(sound, 20);
    int top = ry + 1, avail = y + ph - 1 - top;
    const struct font *f = text_font();
    strip_px = (struct rect){ 0, 0, 0, 0 }; pads_px = (struct rect){ 0, 0, 0, 0 };
    if (in->strip_hi && avail >= 4) {                         /* the strip: a key a step (a smooth band when free) */
        int sh = MIN(8, MAX(4, avail / 3));
        struct rect r = ui_canvas(x + 2, top, pw - 4, sh, C_BG);
        strip_px = r;
        int lo = in->strip_lo, hi = in->strip_hi;
        if (in->steps == STEPS_FREE) {
            for (int px = 0; px < r.w; px++) gfx_fill(r.x + px, r.y + 2, 1, r.h - 4, ramp(R_AMBER, 3 + px * 6 / MAX(1, r.w)));
            for (int n = lo; n <= hi; n++) if (n % 12 == 0) { int px = r.x + (n - lo) * (r.w - 1) / MAX(1, hi - lo); gfx_fill(px, r.y, 1, r.h, ramp(R_GRAY, 10)); }
        } else {
            int steps[128], nst = 0;
            for (int k = 0; k < 128 && nst < 128; k++) { int s = in->steps == STEPS_SEMI ? (lo + k) * 256 : inst_step(in, lo, k); if (s < 0 || s > hi * 256) break; steps[nst++] = s; }
            for (int k = 0; k < nst; k++) {
                int x0 = r.x + k * r.w / MAX(1, nst), x1 = r.x + (k + 1) * r.w / MAX(1, nst);
                bool lit = false; for (int s = 0; s < ns; s++) lit |= (sound[s] + 128) >> 8 == (steps[k] + 128) >> 8;
                gfx_round(x0 + 1, r.y + 2, x1 - x0 - 2, r.h - 4, 3, lit ? ramp(R_AMBER, 14) : ramp(R_PANEL, (steps[k] >> 8) % 12 == 0 ? 6 : 4), ramp(R_GRAY, 6));
                if (x1 - x0 > f->width * 2 + 2 && (k == 0 || (steps[k] >> 8) % 12 == 0 || x1 - x0 > f->width * 4)) {
                    char nn[8]; note_name(steps[k], nn, sizeof nn);
                    gfx_text(x0 + 3, r.y + r.h - f->height - 3, nn, f, lit ? C_BLACK : C_DIM, -1, 1);
                }
            }
        }
        for (int s = 0; s < ns; s++)                          /* where it sounds */
            if (sound[s] >= lo * 256 && sound[s] <= hi * 256) {
                int px = r.x + (int)((int64_t)(sound[s] - lo * 256) * (r.w - 1) / MAX(1, (hi - lo) * 256));
                gfx_fill(px - 1, r.y, 3, r.h, ramp(R_AMBER, 15));
            }
        top += sh + 1; avail -= sh + 1;
    }
    if (in->pads_w && avail >= 3) {                           /* the pads */
        int gh = avail - 1, gw = MIN(pw - 4, gh * 2 * f->height / f->width * in->pads_w / MAX(1, in->pads_h));
        struct rect r = ui_canvas(x + 2 + (pw - 4 - gw) / 2, top, gw, gh, C_BG);
        pads_px = r;
        for (int p = 0; p < in->pads_w * in->pads_h; p++) {
            int c = p % in->pads_w, rr = in->pads_h - 1 - p / in->pads_w;
            int x0 = r.x + c * r.w / in->pads_w, x1 = r.x + (c + 1) * r.w / in->pads_w, y0 = r.y + rr * r.h / in->pads_h, y1 = r.y + (rr + 1) * r.h / in->pads_h;
            int pitch = pad_pitch(in, p);
            bool lit = false; for (int s = 0; s < ns; s++) lit |= pitch >= 0 && (sound[s] + 128) >> 8 == (pitch + 128) >> 8;
            gfx_round(x0 + 3, y0 + 3, x1 - x0 - 6, y1 - y0 - 6, 6, pitch < 0 ? ramp(R_PANEL, 2) : lit ? ramp(R_CYAN, 13) : ramp(R_PANEL, (pitch >> 8) % 12 == in->pads_root % 12 ? 7 : 4), ramp(R_GRAY, 7));
            if (pitch >= 0) { char nn[8]; note_name(pitch, nn, sizeof nn); gfx_text(x0 + 8, y0 + 7, nn, f, lit ? C_BLACK : C_DIM, -1, 1); }
        }
        top += gh + 1; avail -= gh + 1;
    }
    if (in->strip_hi && avail >= 4) {                         /* what the strip sounds, large */
        int32_t now_p = -1; for (int k = 0; k < ns; k++) now_p = sound[k];
        int rh = MIN(avail - 1, in->keys != KEYS_OFF ? 4 : 8);
        struct rect r = ui_canvas(x + 2, top, pw - 4, rh, C_PANEL);
        int sc = MAX(1, MIN(4, r.h / (f->height + 2)));
        char big[24] = "";
        if (now_p >= 0) {
            int n = (now_p + 128) >> 8, cents = ((now_p - n * 256) * 100) / 256;
            if (in->steps == STEPS_FREE) snfmt(big, sizeof big, "%s%d %c%d", nm[n % 12], n / 12 - 1, cents < 0 ? '-' : '+', cents < 0 ? -cents : cents);
            else snfmt(big, sizeof big, "%s%d", nm[n % 12], n / 12 - 1);
        }
        if (big[0]) gfx_text(r.x + (r.w - gfx_text_width(big, f, sc)) / 2, r.y + (r.h - f->height * sc) / 2, big, f, C_AMBER, -1, sc);
        top += rh + 1; avail -= rh + 1;
    }
    if (in->keys != KEYS_OFF && avail >= 4) {                 /* the letter rows as keycaps, each with its note */
        static const char *const rowk[2] = { "q2w3er5t6y7ui9o0p", "zsxdcvgbhnjm,l.;/" };
        int kh = MIN(avail - 1, in->strip_hi || in->pads_w ? 8 : 14);
        struct rect r = ui_canvas(x + 2, top, pw - 4, kh, C_PANEL);
        for (int row = 0; row < 2; row++) {
            int n = (int)strlen(rowk[row]), cw = r.w / n, y0 = r.y + row * r.h / 2, hh = r.h / 2 - 3;
            for (int k = 0; k < n; k++) {
                int p = key_pitch(in, (uint8_t)rowk[row][k]);
                bool lit = false; for (int s2 = 0; s2 < ns; s2++) lit |= p >= 0 && (sound[s2] + 128) >> 8 == (p + 128) >> 8;
                int x0 = r.x + k * cw + (row ? 0 : cw / 2);
                if (x0 + cw > r.x + r.w) break;
                gfx_round(x0 + 2, y0 + 2, cw - 4, hh, 4, p < 0 ? ramp(R_PANEL, 2) : lit ? ramp(R_AMBER, 13) : ramp(R_PANEL, 4), ramp(R_GRAY, 6));
                char kc[2] = { (char)(rowk[row][k] >= 'a' ? rowk[row][k] - 32 : rowk[row][k]), 0 };
                gfx_text(x0 + 5, y0 + 4, kc, f, lit ? C_BLACK : C_DIM, -1, 1);
                if (p >= 0 && cw >= f->width * 3 + 6) { char nn[8]; note_name(p, nn, sizeof nn); gfx_text(x0 + 5, y0 + 2 + hh - f->height - 2, nn, f, lit ? C_BLACK : C_TEXT, -1, 1); }
            }
        }
        top += kh + 1; avail -= kh + 1;
    }
    if (in->bellows && avail >= 5) {                          /* the bellows from the side: open as far as the air */
        int a = inst_air();
        struct rect r;
        if (ui_canvas_keyed(&r, x + 2, top, pw - 4, avail - 1, C_PANEL, ui_hash_int(UI_HASH0, a >> 8))) {
            int w = MIN(r.w * 3 / 4, r.h * 4), x0 = r.x + (r.w - w) / 2, plate = MAX(4, r.h / 16), room = r.h - 2 * plate - 8;
            int gap = room / 6 + (room - room / 6) * a / 32767, y0 = r.y + (r.h - 2 * plate - gap) / 2, folds = 10, depth = MAX(6, w / 40);
            uint8_t hue = ramp(R_CYAN, 5 + 9 * a / 32767);
            int lx = x0, rx = x0 + w, ly = y0 + plate;
            for (int i = 0; i <= folds; i++) {                /* the pleats: in and out, edge to edge */
                int y = y0 + plate + gap * i / folds, in_ = i % 2 ? depth : 0;
                gfx_hline(x0 + in_, y, w - 2 * in_, i % 2 ? ramp(R_GRAY, 6) : hue);
                if (i) { gfx_line(lx, ly, x0 + in_, y, hue); gfx_line(rx, ly, x0 + w - in_, y, hue); }
                lx = x0 + in_; rx = x0 + w - in_; ly = y;
            }
            gfx_round(x0 - 8, y0, w + 16, plate, 3, ramp(R_AMBER, 8), -1);          /* the box with the reeds */
            gfx_round(x0 - 8, y0 + plate + gap, w + 16, plate, 3, ramp(R_AMBER, 11), -1);   /* the plate the hand moves */
        }
    }

    int kx = x + pw + 1, ky = y + 2, lim = y + ph - 1;
    ui_panel(kx, y, sw, ph, inst_latched() ? "SOUND · held" : "SOUND", C_CYAN);
    if (in->bellows && ky + 1 < lim) {                       /* the air in the bellows */
        text_str(kx + 3, ky, "air", C_TEXT, C_PANEL);
        ui_bar(kx + 13, ky, sw - 15, inst_air(), 32767, C_CYAN, C_PANEL);
        ky += 2;
    }
    for (int k = 0; k < in->nknobs && ky < lim; k++, ky++) {
        char v[24]; inst_knob_text(in, in->knob[k], v, sizeof v);
        bool on = k == knob % MAX(1, in->nknobs);
        text_put(kx + 1, ky, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(kx + 3, ky, inst_knob_names[in->knob[k]], on ? C_BRIGHT : C_TEXT, C_PANEL);
        text_str_n(kx + 13, ky, v, sw - 15, on ? C_AMBER : C_DIM, C_PANEL);
    }
    ky++;
    char line[48];
    static const char *const arp_names[5] = { "", "up", "down", "updown", "random" };
    if (in->arp && ky < lim) { snfmt(line, sizeof line, "arp %s, %d a beat", arp_names[in->arp], in->rate); text_str_n(kx + 3, ky++, line, sw - 5, C_GREEN, C_PANEL); }
    if (in->octaves > 1 && in->arp && ky < lim) { snfmt(line, sizeof line, "over %d octaves", in->octaves); text_str_n(kx + 3, ky++, line, sw - 5, C_GREEN, C_PANEL); }
    if (in->mono && ky < lim) text_str(kx + 3, ky++, in->glide_ms ? "one note, gliding" : "one note at a time", C_GREEN, C_PANEL);
    if (in->scale != SC_CHROMATIC && ky < lim) { snfmt(line, sizeof line, "scale: %s", inst_scale_names[in->scale]); text_str_n(kx + 3, ky++, line, sw - 5, C_GREEN, C_PANEL); }
    if (in->tuning == TUNING_JUST && ky < lim) { snfmt(line, sizeof line, "just intonation from %s", nm[in->tonic % 12]); text_str_n(kx + 3, ky++, line, sw - 5, C_GREEN, C_PANEL); }
    if (in->hold == HOLD_TOGGLE && ky < lim) text_str_n(kx + 3, ky++, "a key opens, again closes", sw - 5, C_GREEN, C_PANEL);
    ky++;
    if (ky < lim) { snfmt(line, sizeof line, "its sound: preset %d,", P_INST1 + i + 1); text_str_n(kx + 3, ky++, line, sw - 5, C_DIM, C_PANEL); }
    if (ky < lim) text_str_n(kx + 3, ky++, "on other pages too", sw - 5, C_DIM, C_PANEL);
    if (ky < lim) { snfmt(line, sizeof line, "file: %s", in->file); text_str_n(kx + 3, ky++, line, sw - 5, C_DIM, C_PANEL); }
    if (in->bellows && !in->strip_hi && !in->pads_w && in->hold == HOLD_TOGGLE)
        FOOTER("Z-/ Q-P", "open, close", "", "a finger to and fro: the bellows", "ENTER", "pump", "SPACE", "close all", "↑ ↓ ← →", "knobs",
               "F1", "next instrument");
    else if (in->bellows && !in->strip_hi && !in->pads_w)
        FOOTER("Z-/ Q-P", "play", "", "a finger to and fro: the bellows", "ENTER", "pump", "SPACE", "hold", "↑ ↓ ← →", "knobs", "F1", "next instrument");
    else if (in->strip_hi) FOOTER("", "the touchpad is the strip", "↑ ↓ ← →", "knobs", "SPACE", "hold", "Z-/ Q-P", "keys", "F1", "next instrument");
    else if (in->pads_w) FOOTER("", "the touchpad is the pads", "↑ ↓ ← →", "knobs", "SPACE", "hold", "Z-/ Q-P", "keys", "F1", "next instrument");
    else FOOTER("Z-/ Q-P", "play", "PGUP PGDN", "octave", "↑ ↓ ← →", "knobs", "SPACE", "hold", "F1", "next instrument");
}
