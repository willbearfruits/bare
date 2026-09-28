/* WAVE: a Fairlight-style page, green on black like the CMI's monitor, in four sub-pages (Tab):
     D  the eight wave slots stacked in 3D, front to back: the path MORPH plays through
     5  harmonics: 32 bars build the slot's wave
     6  drawing the slot's wave
     8  the sampler: record, grab what you just played, trim, loop, 8-bit, hand it to the stretcher
   The touchpad is a pen here: the pad is the screen, and a finger draws, drags and picks where it lands.
   The bottom two letter rows are a chromatic keyboard for the current sound. */
#include "ui.h"
#include "harmony.h"
#include "gfx.h"
#include "wave.h"
#include "synth.h"
#include "sampler.h"
#include "stretch.h"
#include "omni.h"
#include "mix.h"
#include "keys.h"
#include "fkeys.h"
#include "undo.h"

#define GR_HI    ramp(R_GREEN, 15)
#define GR_TX    ramp(R_GREEN, 12)
#define GR_MID   ramp(R_GREEN, 8)
#define GR_LO    ramp(R_GREEN, 5)
#define GR_FAINT ramp(R_GREEN, 3)

enum { SUB_D, SUB_5, SUB_6, SUB_8, SUBS };
static const char *const sub_key[SUBS] = { "D", "5", "6", "8" }, *const sub_name[SUBS] = { "WAVEFORMS", "HARMONICS", "DRAW", "SAMPLE" };
static int sub = SUB_D;
#define slot synth_draw_slot                     /* the wave slot DRAWN and MORPH start from */
static int sound = P_DRAWN;                      /* what the keyboard plays on the wave sub-pages */
static int sslot;                                /* the sample slot */
static int octave = 4, hcur, dcur, mark = 2;     /* mark: the sample marker the keys move, 0 start 1 loop 2 end */
static int skew = 2, depth = 4;                  /* page D's view */
static struct { int16_t tab[WAVE_LEN]; uint8_t harm[WAVE_HARMS]; bool valid; } clip;
static int sclip = -1;
static char msg[48]; static uint64_t msg_ms;
static uint32_t held;                            /* a bit per keyboard key down */

static const char kb[] = "zsxdcvgbhnjm,l.;/";   /* C to E: the bottom letter rows as a piano, black keys on the row above */
#define TAG_KB 0x500

static void say(const char *m, uint64_t now) { snfmt(msg, sizeof msg, "%s", m); msg_ms = now; }
static const char *with_key(const char *m, int page) {           /* "…" and, where a key opens the page, " (F4)" */
    static char b[64]; const char *k = fkeys_page_key(page);
    if (k) snfmt(b, sizeof b, "%s (%s)", m, k); else snfmt(b, sizeof b, "%s", m);
    return b;
}
static int play_preset(void) { return sub == SUB_8 ? P_SMP1 + sslot : sound; }

/* ---- the sample markers ---- */
static uint32_t *mark_ptr(struct sample *s, int m) { return m == 0 ? &s->start : m == 1 ? &s->loop_start : &s->end; }
static void set_mark(struct sample *s, int m, int64_t v) {
    if (!s->len) return;
    int64_t lo = m == 2 ? (int64_t)s->start + 1 : m == 1 ? (int64_t)s->start : 0, hi = m == 2 ? (int64_t)s->len : (int64_t)s->end - 1;
    *mark_ptr(s, m) = (uint32_t)CLAMP(v, lo, MAX(lo, hi));
    if (s->loop_start < s->start) s->loop_start = s->start;
    if (s->loop_start >= s->end) s->loop_start = s->end - 1;
}

static void sample_status(struct sample *s, char *out, int cap) {
    uint32_t r = s->rate ? s->rate : 48000;
    snfmt(out, (size_t)cap, "%s  %d.%02d S  %d BIT %d KHZ", s->name, s->len / r, s->len % r * 100 / r, s->bits, (r + 500) / 1000);
}

static void save_wave(uint64_t now) { char w[24]; snfmt(w, sizeof w, "wave %d", slot + 1); undo_one(U_WAVE, slot, w, now); }
static void save_sample(bool frames, uint64_t now) { char w[24]; snfmt(w, sizeof w, "sample %d", sslot + 1); undo_one(frames ? U_SAMPLE : U_SMETA, sslot, w, now); }

/* ---- keys ---- */
static bool wave_key(uint8_t code, uint64_t now) {
    int16_t *t = wave_bank[slot].tab; uint8_t *h = wave_bank[slot].harm;
    if (((code == KEY_UP || code == KEY_DOWN) && sub != SUB_D) || code == 'r' || code == 't' || code == 'y' || code == 'u' || code == 'i' || (code == 'p' && clip.valid))
        save_wave(now);
    switch (code) {
    case KEY_LEFT:  if (sub == SUB_D) skew = MAX(0, skew - 1); else if (sub == SUB_5) hcur = (hcur + WAVE_HARMS - 1) % WAVE_HARMS; else dcur = (dcur + WAVE_LEN - 4) % WAVE_LEN; return true;
    case KEY_RIGHT: if (sub == SUB_D) skew = MIN(8, skew + 1); else if (sub == SUB_5) hcur = (hcur + 1) % WAVE_HARMS; else dcur = (dcur + 4) % WAVE_LEN; return true;
    case KEY_UP: case KEY_DOWN: {
        int d = code == KEY_UP ? 1 : -1;
        if (sub == SUB_D) depth = CLAMP(depth + d, 1, 6);
        else if (sub == SUB_5) { h[hcur] = (uint8_t)CLAMP((int)h[hcur] + d * 5, 0, 100); wave_from_harmonics(slot); }
        else for (int i = dcur; i < dcur + 4; i++) t[i] = (int16_t)CLAMP(t[i] + d * 1500, -32000, 32000);
        return true; }
    case 'r': wave_random(slot); say("RANDOM HARMONICS", now); return true;
    case 't': wave_smooth(slot); return true;
    case 'y': wave_normalize(slot); return true;
    case 'u': wave_invert(slot); return true;
    case 'i': wave_clear(slot); return true;
    case 'o': memcpy(clip.tab, t, sizeof clip.tab); memcpy(clip.harm, h, sizeof clip.harm); clip.valid = true; say("COPIED", now); return true;
    case 'p': if (clip.valid) { memcpy(t, clip.tab, sizeof clip.tab); memcpy(h, clip.harm, sizeof clip.harm); say("PASTED", now); } return true;
    case '\'': sound = sound == P_DRAWN ? P_MORPH : sound == P_MORPH ? P_SCAN : sound == P_SCAN ? P_ROM : P_DRAWN; return true;
    case '\\': wave_rom_src = (wave_rom_src + 1) % ROM_SOURCES; wave_rom_refresh(); return true;
    case '-': wave_rom_off -= 256; wave_rom_refresh(); return true;
    case '=': wave_rom_off += 256; wave_rom_refresh(); return true;
    case KEY_PGUP: wave_rom_off -= 65536; wave_rom_refresh(); return true;
    case KEY_PGDN: wave_rom_off += 65536; wave_rom_refresh(); return true;
    case KEY_HOME: wave_rom_off = 0; wave_rom_refresh(); return true;
    }
    return false;
}

static bool sample_key(uint8_t code, uint64_t now) {
    struct sample *s = &samples[sslot];
    char m[48];
    switch (code) {                                         /* what an undo will want back */
    case 'r': if (sampler.state == SMP_IDLE) save_sample(true, now); break;
    case 'a': case 'w': case 'e': case 't': case 'y': case 'u': case 'i': save_sample(true, now); break;
    case 'p': if (sclip >= 0) save_sample(true, now); break;
    case 'k': case '\'': case KEY_LEFT: case KEY_RIGHT: case KEY_PGUP: case KEY_PGDN: case KEY_HOME: case KEY_END: case '-': case '=':
        save_sample(false, now); break;
    }
    int64_t fine = MAX(1, (int64_t)s->len / 1024), coarse = MAX(1, (int64_t)s->len / 32);
    switch (code) {
    case 'r':
        if (!sampler.slot_bytes) { say("NO MEMORY FOR SAMPLES", now); return true; }
        if (sampler.state != SMP_IDLE) { sampler_stop(); say("STOPPED", now); return true; }
        if (sampler.source == SMP_IN && mix.input < 0 && !mix_set_input(0)) { say("NO INPUT ON THIS MACHINE", now); return true; }
        sampler_record(sslot);
        say(sampler.source == SMP_IN ? "ARMED: WAITING FOR THE INPUT" : "ARMED: PLAY SOMETHING", now);
        return true;
    case 'a':
        if (sampler_grab(sslot)) { sample_status(s, m, sizeof m); say(m, now); }
        else say("NOTHING PLAYED IN THE LAST 10 S", now);
        return true;
    case 'f': say(sampler_to_stretch(sslot) ? with_key("THE STRETCHER PLAYS IT", PAGE_STRETCH) : "THE SLOT IS EMPTY", now); return true;
    case 'q': if (sampler.state == SMP_IDLE) sampler.source = (uint8_t)((sampler.source + 1) % SMP_SOURCES); return true;
    case 'w': {
        int ri = (s->rate_i + 1) % SAMPLE_RATES;
        sampler.rec_rate_i = (uint8_t)ri; sampler_set_format(sslot, s->bits, ri);
        snfmt(m, sizeof m, "%u HZ", sampler_rate_hz(ri)); say(m, now); return true; }
    case 'e': {
        int b = s->bits == 16 ? 8 : 16;
        sampler.rec_bits = (uint8_t)b; sampler_set_format(sslot, b, s->rate_i);
        say(b == 8 ? "8 BIT" : "16 BIT", now); return true; }
    case 't': sampler_trim(sslot); say("TRIMMED", now); return true;
    case 'y': sampler_normalize(sslot); say("NORMALISED", now); return true;
    case 'u': sampler_reverse(sslot); say("REVERSED", now); return true;
    case 'i': sampler_clear(sslot); say("CLEARED", now); return true;
    case 'o': sclip = sslot; say("COPIED", now); return true;
    case 'p': if (sclip >= 0) { sampler_copy(sclip, sslot); say("PASTED", now); } return true;
    case 'k': s->loop = !s->loop; if (s->loop && s->loop_start <= s->start) s->loop_start = s->start; mark = s->loop ? 1 : mark; return true;
    case '\'': s->oneshot = !s->oneshot; return true;
    case KEY_UP:   mark = (mark + 2) % 3; return true;
    case KEY_DOWN: mark = (mark + 1) % 3; return true;
    case KEY_LEFT:  set_mark(s, mark, (int64_t)*mark_ptr(s, mark) - fine); return true;
    case KEY_RIGHT: set_mark(s, mark, (int64_t)*mark_ptr(s, mark) + fine); return true;
    case KEY_PGUP:  set_mark(s, mark, (int64_t)*mark_ptr(s, mark) - coarse); return true;
    case KEY_PGDN:  set_mark(s, mark, (int64_t)*mark_ptr(s, mark) + coarse); return true;
    case KEY_HOME:  set_mark(s, mark, 0); return true;
    case KEY_END:   set_mark(s, mark, s->len); return true;
    case '-': if (s->root > 12) s->root--; return true;
    case '=': if (s->root < 120) s->root++; return true;
    }
    return false;
}

static bool key(uint8_t code, bool down, uint64_t now) {
    for (int i = 0; kb[i]; i++) {
        if (code != (uint8_t)kb[i]) continue;
        uint16_t tag = (uint16_t)(TAG_KB | i);
        if (down && !(held >> i & 1)) {
            held |= 1u << i;
            synth_note_on((uint8_t)CLAMP(harmony_note(12 * (octave + 1) + i), 0, 127), 110, (uint8_t)play_preset(), tag);
        } else if (!down) { held &= ~(1u << i); synth_note_off_tag(tag); }
        return true;
    }
    if (code == KEY_SPACE) {                        /* the sound at its own pitch: a sample's root, or middle C */
        if (down && !(held >> 31 & 1)) { held |= 1u << 31; synth_note_on(sub == SUB_8 ? samples[sslot].root : 60, 110, (uint8_t)play_preset(), TAG_KB | 31); }
        else if (!down) { held &= ~(1u << 31); synth_note_off_tag(TAG_KB | 31); }
        return true;
    }
    if (!down) return false;
    if (code == KEY_TAB) { sub = (sub + (ui_shift ? SUBS - 1 : 1)) % SUBS; return true; }
    if (code >= '1' && code <= '8') { if (sub == SUB_8) sslot = code - '1'; else slot = code - '1'; return true; }
    if (code == '[') { if (octave > 0) octave--; return true; }
    if (code == ']') { if (octave < 8) octave++; return true; }
    return sub == SUB_8 ? sample_key(code, now) : wave_key(code, now);
}

/* ---- where things are, from the last frame's layout ---- */
static struct rect tabs_px[SUBS], canvas, bars, over, side_rows, sliders[3];
static int drag = -1, prev_x = -1; static int16_t prev_v;

/* level, attack and release as fractions of a slider: attack and release on a square law, fine at the short end */
static int slider_get(const struct sample *s, int i, int w) {
    if (i == 0) return s->level * w / 100;
    int v = i == 1 ? s->attack_ms : s->release_ms, max = i == 1 ? 2000 : 4000, x = 0;
    while (x < w && (int64_t)x * x * max / ((int64_t)w * w) < v) x++;
    return x;
}
static void slider_set(struct sample *s, int i, int x, int w) {
    x = CLAMP(x, 0, w);
    if (i == 0) s->level = (uint8_t)(x * 100 / w);
    else if (i == 1) s->attack_ms = (uint16_t)MAX(1, (int64_t)x * x * 2000 / ((int64_t)w * w));
    else s->release_ms = (uint16_t)MAX(5, (int64_t)x * x * 4000 / ((int64_t)w * w));
}

/* The pad is the screen: a finger on it is the pen, at the same place on the page. */
static void pointer(uint64_t now) {
    bool down = ptr.down, pressed = ptr.pressed;
    if (pad.present && (pad.on || pad.lifted)) {
        struct rect a = text_rect(0, 1, text_cols(), text_rows() - 2);
        ptr.x = a.x + (int)((int64_t)pad.x * a.w / 32768); ptr.y = a.y + (int)((int64_t)pad.y * a.h / 32768);
        ptr.moved_ms = now; down = pad.on; pressed = pad.touched;
    }
    if (!down) { drag = -1; prev_x = -1; return; }
    if (pressed) {
        for (int i = 0; i < SUBS; i++) if (ui_in(tabs_px[i], ptr.x, ptr.y)) { sub = i; return; }
        if (ui_in(side_rows, ptr.x, ptr.y)) {
            int r = (ptr.y - side_rows.y) * SAMPLE_SLOTS / side_rows.h;
            if (sub == SUB_8) sslot = r; else slot = r;
            return;
        }
    }
    if (sub == SUB_D) {
        if (!pressed || !ui_in(canvas, ptr.x, ptr.y)) return;
        /* the layer whose baseline is nearest: slot 1 at the front, at the bottom */
        int dy = canvas.h * depth / 40, amp = MIN(dy * 3 / 2, (canvas.h - dy * (WAVE_SLOTS - 1)) * 2 / 5), best = 0, bd = 1 << 30;
        for (int k = 0; k < WAVE_SLOTS; k++) {
            int base = canvas.y + canvas.h - 4 - amp - k * dy, d = ptr.y > base ? ptr.y - base : base - ptr.y;
            if (d < bd) { bd = d; best = k; }
        }
        slot = best;
    } else if (sub == SUB_5) {
        if (!ui_in(bars, ptr.x, ptr.y)) return;
        save_wave(now);
        int hn = CLAMP((ptr.x - bars.x) * WAVE_HARMS / bars.w, 0, WAVE_HARMS - 1), bh = bars.h - text_font()->height - 2;
        wave_bank[slot].harm[hn] = (uint8_t)CLAMP(100 - (ptr.y - bars.y) * 100 / MAX(1, bh - 1), 0, 100);
        wave_from_harmonics(slot);
        hcur = hn;
    } else if (sub == SUB_6) {
        if (!ui_in(canvas, ptr.x, ptr.y)) { prev_x = -1; return; }
        save_wave(now);
        int x = (ptr.x - canvas.x) * WAVE_LEN / canvas.w;
        int16_t v = (int16_t)CLAMP(32000 - (ptr.y - canvas.y) * 64000 / MAX(1, canvas.h - 1), -32000, 32000);
        wave_draw(slot, x, v, prev_x, prev_v);         /* a line from the last position: fast strokes leave no gaps */
        prev_x = x; prev_v = v; dcur = x & ~3;
    } else {
        struct sample *s = &samples[sslot];
        if (pressed) {
            drag = -1;
            for (int i = 0; i < 3; i++) if (ui_in(sliders[i], ptr.x, ptr.y)) drag = 10 + i;
            if (drag < 0 && ui_in(over, ptr.x, ptr.y) && s->len) {     /* the nearest marker */
                int bd = 1 << 30;
                for (int m = 0; m < 3; m++) {
                    if (m == 1 && !s->loop) continue;
                    int mx = over.x + (int)((int64_t)*mark_ptr(s, m) * over.w / s->len), d = ptr.x > mx ? ptr.x - mx : mx - ptr.x;
                    if (d < bd) { bd = d; drag = m; }
                }
                if (drag >= 0) mark = drag;
            }
        }
        if (drag >= 0) save_sample(false, now);
        if (drag >= 10) slider_set(s, drag - 10, ptr.x - sliders[drag - 10].x, sliders[drag - 10].w);
        else if (drag >= 0 && s->len) set_mark(s, drag, (int64_t)(ptr.x - over.x) * s->len / MAX(1, over.w));
    }
}

/* ---- drawing ---- */
/* one layer of the 3D view: the curve, and everything under it cleared so nearer layers hide farther ones */
static void layer(int x, int base, int w, int amp, const int16_t *tab, uint8_t line, int bottom) {
    int prev = 0;
    for (int i = 0; i < w; i++) {
        int y = base - tab[i * WAVE_LEN / w] * amp / 32768;
        gfx_vline(x + i, y, bottom - y, ramp(R_GREEN, 1));          /* a solid slice: it hides what is behind it */
        if (i) gfx_line(x + i - 1, prev, x + i, y, line); else gfx_pixel(x, y, line);
        prev = y;
    }
}

static void draw_stack(void) {
    const struct font *f = text_font();
    int n = WAVE_SLOTS, dx = canvas.w * skew / 90, dy = canvas.h * depth / 40;
    int w = canvas.w - dx * (n - 1) - 12 - f->width, amp = MIN(dy * 3 / 2, (canvas.h - dy * (n - 1)) * 2 / 5);
    if (w < 16 || amp < 4) return;
    gfx_clip(canvas.x, canvas.y, canvas.w, canvas.h);
    for (int k = n - 1; k >= 0; k--) {                          /* farthest first */
        int x = canvas.x + 6 + k * dx, base = canvas.y + canvas.h - 4 - amp - k * dy;
        bool sel = k == slot;
        layer(x, base, w, amp, wave_bank[k].tab, sel ? GR_HI : ramp(R_GREEN, 12 - k), base + amp + 1);
        gfx_hline(x, base, w, sel ? GR_MID : GR_FAINT);
        char lab[2] = { (char)('1' + k), 0 };
        gfx_text(x + w + 3, base - f->height / 2, lab, f, sel ? GR_HI : GR_LO, -1, 1);
    }
    gfx_noclip();
}

/* the bars, their numbers under them (in the canvas, so they line up), and the wave they add up to */
static void draw_harmonics(struct rect wave_r) {
    const struct wave_slot *ws = &wave_bank[slot];
    const struct font *f = text_font();
    int bw = bars.w / WAVE_HARMS, bh = bars.h - f->height - 2, ox = bars.x + (bars.w - bw * WAVE_HARMS) / 2;
    for (int i = 1; i < 4; i++) for (int xx = bars.x; xx < bars.x + bars.w; xx += 3) gfx_pixel(xx, bars.y + bh * i / 4, GR_FAINT);
    for (int hn = 0; hn < WAVE_HARMS; hn++) {
        int hh = ws->harm[hn] * (bh - 2) / 100, bx = ox + hn * bw;
        bool cur = hn == hcur;
        if (cur) gfx_fill(bx, bars.y, bw, bh, GR_FAINT);
        if (hh) { gfx_fill(bx + 2, bars.y + bh - hh, bw - 4, hh, cur ? GR_HI : GR_LO); gfx_hline(bx + 2, bars.y + bh - hh, bw - 4, GR_HI); }
        if (hn % (bw < 3 * f->width ? 2 : 1) && hn != hcur) continue;
        char lab[4]; snfmt(lab, sizeof lab, "%d", hn + 1);
        gfx_text(bx + (bw - gfx_text_width(lab, f, 1)) / 2, bars.y + bh + 2, lab, f, cur ? GR_HI : GR_LO, -1, 1);
    }
    ui_wave_px(wave_r, ws->tab, WAVE_LEN, GR_HI, GR_FAINT);
}

static void draw_wave(void) {
    const struct wave_slot *ws = &wave_bank[slot];
    int mid = canvas.y + canvas.h / 2;
    for (int i = 1; i < 8; i++) for (int yy = canvas.y; yy < canvas.y + canvas.h; yy += 4) gfx_pixel(canvas.x + canvas.w * i / 8, yy, GR_FAINT);
    for (int xx = canvas.x; xx < canvas.x + canvas.w; xx += 2) gfx_pixel(xx, mid, GR_LO);
    int cx0 = canvas.x + dcur * canvas.w / WAVE_LEN, cx1 = canvas.x + (dcur + 4) * canvas.w / WAVE_LEN;
    gfx_fill(cx0, canvas.y, MAX(1, cx1 - cx0), canvas.h, GR_FAINT);
    ui_wave_px((struct rect){ canvas.x, canvas.y + 2, canvas.w, canvas.h - 4 }, ws->tab, WAVE_LEN, GR_HI, ramp(R_GREEN, 2));
}

/* The whole sample: each column its frames' span, cached until the sample changes. The picture stays on screen while
   voices play it: only the columns their playheads leave and reach are drawn again. */
static struct { int slot, w; uint32_t gen, len; int16_t lo[4096], hi[4096]; int xs, xe, xl; int16_t hx[8]; int nhx; } ov = { -1 };
static void ov_column(const struct sample *s, int c) {
    int x = over.x + c, mid = over.y + over.h / 2, amp = over.h / 2 - 1;
    bool looped = s->loop && x >= ov.xl && x < ov.xe;
    gfx_vline(x, over.y, over.h, looped ? ramp(R_GREEN, 2) : C_BLACK);
    if (!(c & 1)) gfx_pixel(x, mid, GR_FAINT);
    if (!s->len) return;
    int t = mid - ov.hi[c] * amp / 32768, b = mid - ov.lo[c] * amp / 32768;
    gfx_vline(x, t, b - t + 1, x >= ov.xs && x < ov.xe ? GR_TX : GR_LO);
    for (int m = 0; m < 3; m++) {                                  /* a marker's line runs down this column */
        if (m == 1 && !s->loop) continue;
        int mx = m == 0 ? ov.xs : m == 1 ? ov.xl : MIN(ov.xe, over.x + over.w - 1);
        if (mx != x) continue;
        for (int yy = over.y; yy < over.y + over.h; yy += m == mark ? 1 : 2) gfx_pixel(x, yy, m == mark ? GR_HI : GR_MID);
    }
}
static void ov_labels(const struct sample *s) {
    static const char mk[3] = { 'S', 'L', 'E' };
    const struct font *f = text_font();
    for (int m = 0; m < 3 && s->len; m++) {
        if (m == 1 && !s->loop) continue;
        int x = m == 0 ? ov.xs : m == 1 ? ov.xl : MIN(ov.xe, over.x + over.w - 1), lx = m == 2 ? x - f->width - 1 : x + 2;
        char lab[2] = { mk[m], 0 };
        gfx_fill(lx, over.y, f->width, f->height, m == mark ? GR_HI : C_BLACK);
        gfx_text(lx, over.y, lab, f, m == mark ? C_BLACK : GR_TX, -1, 1);
    }
}
static void draw_overview(const struct sample *s, bool fresh) {
    int w = MIN(over.w, 4096);
    if (fresh) {
        if (ov.slot != sslot || ov.gen != s->gen || ov.w != w || ov.len != s->len) {
            ov.slot = sslot; ov.gen = s->gen; ov.w = w; ov.len = s->len;
            for (int c = 0; c < w && s->len; c++) {
                int32_t lo, hi;
                sampler_span(s, (uint32_t)((uint64_t)c * s->len / w), (uint32_t)((uint64_t)(c + 1) * s->len / w) + 1, &lo, &hi);
                ov.lo[c] = (int16_t)lo; ov.hi[c] = (int16_t)hi;
            }
        }
        uint32_t len = s->len ? s->len : 1;
        ov.xs = over.x + (int)((uint64_t)s->start * w / len); ov.xe = over.x + (int)((uint64_t)s->end * w / len);
        ov.xl = over.x + (int)((uint64_t)s->loop_start * w / len);
        for (int c = 0; c < w; c++) ov_column(s, c);
        ov_labels(s);
        ov.nhx = 0;
    }
    /* the playheads: put back the columns they left, draw where they are */
    uint32_t heads[8]; int nh = s->len ? synth_sample_heads(sslot, heads, 8) : 0;
    int16_t hx[8];
    for (int i = 0; i < nh; i++) hx[i] = (int16_t)MIN(w - 1, (int)((uint64_t)heads[i] * w / s->len));
    bool restored = false;
    for (int i = 0; i < ov.nhx; i++) {
        bool still = false;
        for (int k = 0; k < nh; k++) if (hx[k] == ov.hx[i]) still = true;
        if (!still) { ov_column(s, ov.hx[i]); restored = true; }
    }
    if (restored) ov_labels(s);
    for (int i = 0; i < nh; i++) gfx_vline(over.x + hx[i], over.y, over.h, ramp(R_AMBER, 13));
    memcpy(ov.hx, hx, sizeof hx); ov.nhx = nh;
}

/* close up on the selected marker, two pixels a frame; for the loop, the splice: the end, then where it jumps back to */
static int64_t zoom_frame(const struct sample *s, int k, bool splice) {
    return splice ? (k < 0 ? (int64_t)s->end + k : (int64_t)s->loop_start + k) : (int64_t)*mark_ptr((struct sample *)s, mark) + k;
}
static void draw_zoom(struct rect r, const struct sample *s) {
    int mid = r.y + r.h / 2, half = r.w / 4;                                   /* frames each side */
    gfx_hline(r.x, mid, r.w, GR_FAINT);
    if (!s->len) return;
    bool splice = mark == 1 && s->loop;
    int32_t peak = 1024;                                  /* scaled to what it shows, up to 32 times */
    for (int k = -half; k < half; k++) { int64_t fi = zoom_frame(s, k, splice); if (fi >= 0 && fi < (int64_t)s->len) { int32_t a = sampler_frame(s, (uint32_t)fi); if (a < 0) a = -a; if (a > peak) peak = a; } }
    int amp = (int)((int64_t)(r.h / 2 - 2) * 32768 / peak);
    const struct font *f = text_font();
    if (peak < 16384) { char z[8]; snfmt(z, sizeof z, "x%d", 32768 / peak); gfx_text(r.x + 2, r.y + 1, z, f, GR_LO, -1, 1); }
    int prev = mid;
    for (int k = -half; k < half; k++) {
        int64_t fi = zoom_frame(s, k, splice);
        int y = fi < 0 || fi >= (int64_t)s->len ? mid : mid - sampler_frame(s, (uint32_t)fi) * amp / 32768;
        int x = r.x + (k + half) * 2;
        if (s->bits == 8) { gfx_vline(x, MIN(prev, y), (prev > y ? prev - y : y - prev) + 1, GR_TX); gfx_hline(x, y, 2, GR_HI); }   /* 8-bit: the steps */
        else gfx_line(x - 2, prev, x, y, GR_HI);
        prev = y;
    }
    for (int yy = r.y; yy < r.y + r.h; yy += 2) gfx_pixel(r.x + half * 2, yy, GR_MID);
}

static void draw_keyboard(int x, int y, int w) {
    struct rect r;
    uint32_t key = ui_hash_int(ui_hash_int(UI_HASH0, (int32_t)held), octave);
    if (!ui_canvas_keyed(&r, x, y, w, 2, C_BG, key)) return;
    const struct font *f = text_font();
    int kw = MIN(r.w / 11, f->width * 4), white = 0;
    static const int8_t white_of[17] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9 };
    for (int i = 0; kb[i]; i++) {
        if (white_of[i] < 0) continue;
        int kx = r.x + white_of[i] * kw;
        bool on = held >> i & 1;
        gfx_round(kx + 1, r.y, kw - 2, r.h, 3, on ? GR_HI : ramp(R_GREEN, 2), GR_LO);
        char lab[2] = { (char)(kb[i] >= 'a' ? kb[i] - 32 : kb[i]), 0 };
        gfx_text(kx + (kw - f->width) / 2, r.y + r.h - f->height - 1, lab, f, on ? C_BLACK : GR_TX, -1, 1);
        white++;
    }
    for (int i = 0; kb[i]; i++) {
        if (white_of[i] >= 0) continue;
        int kx = r.x + white_of[i - 1] * kw + kw * 2 / 3;
        bool on = held >> i & 1;
        gfx_round(kx, r.y, kw * 2 / 3, r.h / 2 + 2, 2, on ? GR_HI : C_BLACK, GR_MID);
        char lab[2] = { (char)(kb[i] >= 'a' ? kb[i] - 32 : kb[i]), 0 };
        gfx_text(kx + (kw * 2 / 3 - f->width) / 2, r.y + 1, lab, f, on ? C_BLACK : GR_TX, -1, 1);
    }
    char lab[24], nn[6]; omni_note_name(12 * (octave + 1), nn);
    snfmt(lab, sizeof lab, "%s", nn);
    gfx_text(r.x + white * kw + f->width, r.y + (r.h - f->height) / 2, lab, f, GR_MID, -1, 1);
}

/* the side panel: the eight wave slots, or the eight sample slots */
static void draw_side(int x, int y, int w, int h) {
    text_box(x, y, w, h, GR_LO, C_BLACK, false);
    text_str(x + 2, y, sub == SUB_8 ? " SAMPLES " : " WAVES ", GR_TX, C_BLACK);
    const struct font *f = text_font();
    side_rows = text_rect(x + 1, y + 1, w - 2, SAMPLE_SLOTS);
    for (int i = 0; i < SAMPLE_SLOTS; i++) {
        bool sel = sub == SUB_8 ? i == sslot : i == slot;
        uint8_t bg = sel ? ramp(R_GREEN, 2) : C_BLACK;
        text_fill(x + 1, y + 1 + i, w - 2, 1, ' ', GR_TX, bg);
        char n[3] = { (char)('1' + i), 0 };
        text_str(x + 2, y + 1 + i, n, sel ? GR_HI : GR_MID, bg);
        struct rect r;
        if (sub == SUB_8) {
            const struct sample *s = &samples[i];
            text_str_n(x + 4, y + 1 + i, s->name, 9, s->len ? GR_TX : GR_LO, bg);
            uint32_t k = ui_hash_int(ui_hash_int(UI_HASH0, (int32_t)s->gen), sel);
            if (ui_canvas_keyed(&r, x + 14, y + 1 + i, w - 16, 1, bg, k) && s->len) {
                int mid = r.y + r.h / 2, amp = r.h / 2 - 1;
                for (int c = 0; c < r.w; c++) {
                    int32_t lo, hi;
                    sampler_span(s, (uint32_t)((uint64_t)c * s->len / r.w), (uint32_t)((uint64_t)(c + 1) * s->len / r.w), &lo, &hi);
                    gfx_vline(r.x + c, mid - hi * amp / 32768, (hi - lo) * amp / 32768 + 1, sel ? GR_HI : GR_MID);
                }
            }
        } else {
            uint32_t k = ui_hash(ui_hash_int(UI_HASH0, sel), wave_bank[i].tab, sizeof wave_bank[i].tab);
            if (ui_canvas_keyed(&r, x + 4, y + 1 + i, w - 6, 1, bg, k))
                ui_wave_px((struct rect){ r.x, r.y + 2, r.w, r.h - 4 }, wave_bank[i].tab, WAVE_LEN, sel ? GR_HI : GR_MID, 0);
        }
    }
    (void)f;
    int iy = y + SAMPLE_SLOTS + 2;
    char line[48];
    if (sub == SUB_8) {
        uint32_t per = sampler.slot_bytes, r = sampler_rate_hz(0);
        text_str(x + 2, iy++, "EACH SLOT HOLDS", GR_LO, C_BLACK);
        snfmt(line, sizeof line, "%u.%u S  16 BIT %u KHZ", per / 2 / r, per / 2 % r * 10 / r, r / 1000);
        text_str_n(x + 2, iy++, line, w - 4, GR_TX, C_BLACK);
        snfmt(line, sizeof line, "%u S  8 BIT %u KHZ", per / sampler_rate_hz(4), sampler_rate_hz(4) / 1000);
        text_str_n(x + 2, iy++, line, w - 4, GR_TX, C_BLACK);
        iy++;
        if (stretch.frozen && iy < y + h - 1) text_str_n(x + 2, iy++, with_key("STRETCHER FROZEN", PAGE_STRETCH), w - 4, GR_HI, C_BLACK);
    } else if (iy + 8 < y + h) {
        static const int sounds[4] = { P_DRAWN, P_MORPH, P_SCAN, P_ROM };
        text_str(x + 2, iy++, "KEYS PLAY", GR_LO, C_BLACK);
        for (int i = 0; i < 4; i++) text_str(x + 3 + i * 6, iy, synth_preset_name(sounds[i]), sounds[i] == sound ? GR_HI : GR_LO, C_BLACK);
        iy += 2;
        snfmt(line, sizeof line, "ROM %s %06x", wave_rom_names[wave_rom_src], wave_rom_off);
        text_str_n(x + 2, iy++, line, w - 4, GR_TX, C_BLACK);
        struct rect r;
        if (ui_canvas_keyed(&r, x + 2, iy, w - 4, 2, C_BLACK, ui_hash(UI_HASH0, wave_rom_tab, sizeof wave_rom_tab)))
            ui_wave_px(r, wave_rom_tab, WAVE_LEN, GR_TX, GR_FAINT);
        iy += 3;
        if (iy + 2 < y + h) {
            text_str(x + 2, iy++, "SCAN  UNDER THE POINTER", GR_TX, C_BLACK);
            if (ui_canvas_keyed(&r, x + 2, iy, w - 4, 2, C_BLACK, ui_hash(UI_HASH0, wave_scan_tab, sizeof wave_scan_tab)))
                ui_wave_px(r, wave_scan_tab, WAVE_LEN, GR_TX, GR_FAINT);
        }
    }
}

static void draw_sample_page(int x, int y, int w, int h, uint64_t now) {
    struct sample *s = &samples[sslot];
    if (!sampler.slot_bytes) { text_str(x + 2, y + 2, "NO MEMORY FOR SAMPLES ON THIS MACHINE", GR_TX, C_BLACK); return; }
    int oh = MAX(4, (h - 7) * 3 / 5), zh = MAX(3, h - 7 - oh);
    uint32_t key = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, (int32_t)s->gen), sslot | mark << 4 | s->loop << 6), (int32_t)s->len);
    key = ui_hash_int(ui_hash_int(ui_hash_int(key, (int32_t)s->start), (int32_t)s->end), (int32_t)s->loop_start);
    draw_overview(s, ui_canvas_keyed(&over, x, y, w, oh, C_BLACK, key));
    ptr.shape = ui_in(over, ptr.x, ptr.y) ? PTR_CROSS : ptr.shape;
    uint32_t r = s->rate ? s->rate : 48000;
    char line[96];
    static const char *const mark_names[3] = { "START", "LOOP", "END" };
    int ly = y + oh;
    for (int m = 0; m < 3; m++) {
        uint32_t v = *mark_ptr(s, m);
        snfmt(line, sizeof line, "%s %u.%03u", mark_names[m], v / r, v % r * 1000 / r);
        text_str(x + 1 + m * 18, ly, line, m == mark ? GR_HI : (m == 1 && !s->loop) ? GR_LO : GR_TX, C_BLACK);
    }
    text_str(x + w - 12, ly, mark == 1 && s->loop ? "LOOP SPLICE" : "CLOSE UP", GR_LO, C_BLACK);
    struct rect zr;
    key = ui_hash_int(ui_hash_int(key, mark), s->bits);
    if (ui_canvas_keyed(&zr, x, ly + 1, w, zh, C_BLACK, key)) draw_zoom(zr, s);
    int py = ly + 2 + zh;
    char nn[6]; omni_note_name(s->root, nn);
    snfmt(line, sizeof line, "%s   %d BIT   %u HZ   %u.%02u S   ROOT %s", s->name, s->bits, r, s->len / r, s->len % r * 100 / r, nn);
    text_str_n(x + 1, py++, line, w - 2, GR_TX, C_BLACK);
    ui_led(x + 1, py, s->loop, GR_HI, "LOOP", C_BLACK);
    ui_led(x + 10, py, s->oneshot, GR_HI, "ONE SHOT", C_BLACK);
    snfmt(line, sizeof line, "LEVEL %d   ATTACK %d MS   RELEASE %d MS", s->level, s->attack_ms, s->release_ms);
    text_str_n(x + 24, py++, line, w - 25, GR_LO, C_BLACK);
    static const char *const sl_names[3] = { "LEVEL", "ATTACK", "RELEASE" };
    int sw = (w - 2) / 3 - 10;
    for (int i = 0; i < 3; i++) {
        int sx = x + 1 + i * (sw + 10);
        text_str(sx, py, sl_names[i], GR_LO, C_BLACK);
        sliders[i] = text_rect(sx + 8, py, sw, 1);
        ui_bar(sx + 8, py, sw, slider_get(s, i, sw * 8), sw * 8, GR_TX, C_BLACK);
    }
    py += 2;
    if (py < y + h) {
        static const char *const src[SMP_SOURCES] = { "OUT", "FREEZE", "IN" };
        snfmt(line, sizeof line, "RECORD FROM %s   %d BIT %u HZ", src[sampler.source], sampler.rec_bits, sampler_rate_hz(sampler.rec_rate_i));
        text_str(x + 1, py, line, GR_TX, C_BLACK);
        const char *st = sampler.state == SMP_ARMED ? "ARMED" : sampler.state == SMP_RECORDING ? "RECORDING" : "";
        if (*st) {
            bool blink = (now / 400) & 1;
            snfmt(line, sizeof line, "%s %u.%u S", st, sampler.rec_len / r, sampler.rec_len % r * 10 / r);
            text_str(x + 44, py, line, blink ? GR_HI : GR_MID, C_BLACK);
        }
        if (x + w - 14 > x + 60) {
            struct rect m = text_gfx(x + w - 14, py, 12, 1);
            gfx_fill(m.x, m.y, m.w, m.h, C_BLACK);
            ui_meter_px((struct rect){ m.x, m.y + m.h / 3, m.w, m.h / 3 }, sampler.vu, 32767, false);
        }
    }
}

static void draw(uint64_t now) {
    int cols = text_cols(), rows = text_rows();
    int side_w = cols >= 140 ? 34 : 26, mx = 1, my = 1, mw = cols - 3 - side_w, mh = rows - 5;
    text_box(mx, my, mw, mh, GR_LO, C_BLACK, false);
    /* the sub-page tabs, CMI style: PAGE D, 5, 6, 8 */
    int tx = mx + 2;
    for (int i = 0; i < SUBS; i++) {
        char t[24]; snfmt(t, sizeof t, " %s %s ", sub_key[i], sub_name[i]);
        int tw = ui_cells(t);
        bool on = i == sub;
        text_str(tx, my + 1, t, on ? C_BLACK : GR_TX, on ? GR_HI : C_BLACK);
        tabs_px[i] = text_rect(tx, my + 1, tw, 1);
        tx += tw + 1;
    }
    char line[96];
    snfmt(line, sizeof line, "PAGE %s", sub_key[sub]);
    text_str(mx + mw - 2 - ui_cells(line), my + 1, line, GR_LO, C_BLACK);
    int cx = mx + 1, cy = my + 2, cw = mw - 2, ch = mh - 4;
    const struct wave_slot *ws = &wave_bank[slot];
    uint32_t key;
    switch (sub) {
    case SUB_D:
        key = ui_hash_int(ui_hash_int(UI_HASH0, slot | skew << 4 | depth << 8), ch);
        for (int s = 0; s < WAVE_SLOTS; s++) key = ui_hash(key, wave_bank[s].tab, sizeof wave_bank[s].tab);
        if (ui_canvas_keyed(&canvas, cx, cy, cw, ch, C_BLACK, key)) draw_stack();
        if (ui_in(canvas, ptr.x, ptr.y)) ptr.shape = PTR_CROSS;
        break;
    case SUB_5: {
        int bh = ch * 2 / 3;
        key = ui_hash(ui_hash_int(UI_HASH0, hcur | slot << 8), ws->harm, sizeof ws->harm);
        struct rect wr;
        bool redraw = ui_canvas_keyed(&bars, cx + 1, cy, cw - 2, bh, C_BLACK, key);
        bool redraw2 = ui_canvas_keyed(&wr, cx + 1, cy + bh, cw - 2, ch - bh, C_BLACK, key);
        if (redraw || redraw2) { gfx_fill(bars.x, bars.y, bars.w, bars.h, C_BLACK); gfx_fill(wr.x, wr.y, wr.w, wr.h, C_BLACK); draw_harmonics(wr); }
        if (ui_in(bars, ptr.x, ptr.y)) ptr.shape = PTR_CROSS;
        break; }
    case SUB_6:
        key = ui_hash(ui_hash_int(UI_HASH0, dcur | slot << 8), ws->tab, sizeof ws->tab);
        if (ui_canvas_keyed(&canvas, cx + 1, cy, cw - 2, ch - 1, C_BLACK, key)) draw_wave();
        snfmt(line, sizeof line, "SAMPLE %d-%d   VALUE %d", dcur, dcur + 3, ws->tab[dcur] * 100 / 32000);
        text_str(cx + 1, cy + ch - 1, line, GR_LO, C_BLACK);
        if (ui_in(canvas, ptr.x, ptr.y)) ptr.shape = PTR_CROSS;
        break;
    case SUB_8:
        draw_sample_page(cx + 1, cy, cw - 2, ch, now);
        break;
    }
    /* the command line: what plays, and the last thing done */
    if (sub == SUB_8) snfmt(line, sizeof line, "SAMPLE %d   OCTAVE %d", sslot + 1, octave);
    else snfmt(line, sizeof line, "WAVE %d   KEYS PLAY %s   OCTAVE %d", slot + 1, synth_preset_name(sound), octave);
    text_str_n(mx + 2, my + mh - 2, line, mw - 4, GR_MID, C_BLACK);
    if (msg[0] && now - msg_ms < 4000) text_str(mx + mw - 2 - ui_cells(msg), my + mh - 2, msg, GR_HI, C_BLACK);
    draw_keyboard(mx + 1, my + mh, mw - 2);
    draw_side(cols - 1 - side_w, 1, side_w, rows - 3);
    switch (sub) {
    case SUB_D: FOOTER("TAB", "page", "1-8", "wave", "Z-/", "play", "[ ]", "octave", "'", "sound", "← →", "turn", "↑ ↓", "depth",
                       "R", "random", "T", "smooth", "Y", "normalize", "U", "invert", "I", "clear", "O P", "copy/paste", "\\ - =", "ROM"); break;
    case SUB_5: FOOTER("TAB", "page", "1-8", "wave", "Z-/", "play", "← →", "harmonic", "↑ ↓", "level", "'", "sound", "[ ]", "octave",
                       "R", "random", "T", "smooth", "Y", "normalize", "I", "clear", "O P", "copy/paste"); break;
    case SUB_6: FOOTER("TAB", "page", "1-8", "wave", "Z-/", "play", "← →", "cursor", "↑ ↓", "nudge", "'", "sound", "[ ]", "octave",
                       "T", "smooth", "Y", "normalize", "U", "invert", "I", "clear", "O P", "copy/paste"); break;
    default:    FOOTER("TAB", "page", "1-8", "sample", "Z-/", "play", "R", "record", "A", "grab", "F", "stretch it", "Q", "source",
                       "W", "rate", "E", "8/16 bit", "↑ ↓", "marker", "← →", "move", "K", "loop", "'", "one shot", "- =", "root",
                       "T", "trim", "Y", "normalize", "U", "reverse", "I", "clear", "O P", "copy/paste"); break;
    }
}

static int strum_sound(void) { return play_preset(); }     /* what MIDI plays here */

const struct page page_wave = { "WAVE", false, key, 0, pointer, strum_sound, draw, true };
