#include "ui.h"
#include "splash.h"
#include "gfx.h"
#include "keys.h"
#include "audio.h"
#include "synth.h"
#include "seq.h"
#include "stretch.h"
#include "tape.h"
#include "platform.h"

extern bool app_thermal; extern int app_temp;

const struct page *const ui_pages[PAGE_COUNT] = { &page_play, &page_seq, &page_wave, &page_stretch, &page_fm, &page_tape, &page_file, &page_mix, &page_touch, &page_fx, &page_ans,
                                                   &page_xen };
bool ui_shift, ui_help;

int ui_cells(const char *p) { int n = 0; for (; *p; p++) if ((*p & 0xC0) != 0x80) n++; return n; }

/* ---- pointer ---- */
struct pointer ptr;

struct touchpad pad;

void ui_pointer_update(uint64_t now, bool page_owns_pad) {
    static bool placed;
    int W = gfx_width(), H = gfx_height();
    if (!placed) { ptr.x = W / 2; ptr.y = H / 2; placed = true; }
    bool was = ptr.down;
    pad.touched = pad.lifted = false;
    for (int k = 0; k < FINGERS; k++) pad.f[k].touched = pad.f[k].lifted = false;
    struct pointer_event ev;
    while (plat_pointer_poll(&ev)) {
        if (ev.touch) {
            bool on = ev.z >= 30;                                /* lighter contact than a finger is ignored */
            pad.present = true;
            struct finger *fg = &pad.f[MIN(ev.finger, FINGERS - 1)];
            if (on && !fg->on) fg->touched = true;
            if (!on && fg->on) fg->lifted = true;
            fg->on = on;
            if (on) { fg->x = ev.tx; fg->y = ev.ty; fg->z = ev.z; fg->size = ev.size; }
            ptr.buttons = ev.buttons;
            if (ev.finger) continue;                             /* finger 0 is also the pointer's */
            if (on && pad.on && !page_owns_pad) {                /* a finger moving: the pointer follows, relatively */
                int dx = (ev.tx - pad.x) * W / 20000, dy = (ev.ty - pad.y) * H / 14000;
                ptr.x += dx + dx * (dx < 0 ? -dx : dx) / 10;
                ptr.y += dy + dy * (dy < 0 ? -dy : dy) / 10;
                if (dx || dy) ptr.moved_ms = now;
            }
            if (on && !pad.on) pad.touched = true;
            if (!on && pad.on) pad.lifted = true;
            pad.on = on;
            if (on) { pad.x = ev.tx; pad.y = ev.ty; pad.z = ev.z; }
            ptr.buttons = ev.buttons;
            continue;
        }
        if (ev.abs) { ptr.x = (int)((uint32_t)ev.ax * (uint32_t)W / 32768); ptr.y = (int)((uint32_t)ev.ay * (uint32_t)H / 32768); }
        else {
            /* a little acceleration: slow moves stay precise, fast ones cross the screen */
            int dx = ev.dx, dy = ev.dy;
            ptr.x += dx + dx * (dx < 0 ? -dx : dx) / 6;
            ptr.y += dy + dy * (dy < 0 ? -dy : dy) / 6;
        }
        if (ev.dx || ev.dy || ev.abs) ptr.moved_ms = now;
        ptr.buttons = ev.buttons;
    }
    pad.n = 0;
    for (int k = 0; k < FINGERS; k++) pad.n += pad.f[k].on;
    ptr.x = CLAMP(ptr.x, 0, W - 1); ptr.y = CLAMP(ptr.y, 0, H - 1);
    ptr.down = ptr.buttons & 1;
    ptr.pressed = ptr.down && !was; ptr.released = !ptr.down && was;
    if (ptr.buttons) ptr.moved_ms = now;
}

/* ---- building blocks ---- */
void ui_panel(int x, int y, int w, int h, const char *title, uint8_t accent) {
    text_box(x, y, w, h, C_BORDER, C_PANEL, false);
    if (title && *title) {
        text_put(x + 2, y, ' ', C_BORDER, C_PANEL);
        text_str_n(x + 3, y, title, w - 6, accent, C_PANEL);
        text_put(x + 3 + MIN(ui_cells(title), w - 6), y, ' ', C_BORDER, C_PANEL);
    }
}

struct rect ui_canvas(int x, int y, int w, int h, uint8_t bg) {
    struct rect r = text_gfx(x, y, w, h);
    gfx_fill(r.x, r.y, r.w, r.h, bg);
    return r;
}

/* canvases drawn last frame with their keys; cleared when the page changes (the grid drew over everything then) */
static struct { int x, y, w, h; uint32_t key, frame; } cache[96];
static uint32_t frame_no;
bool ui_canvas_keyed(struct rect *r, int x, int y, int w, int h, uint8_t bg, uint32_t key) {
    *r = text_gfx(x, y, w, h);
    int free_i = -1;
    for (int i = 0; i < (int)ARRAY_LEN(cache); i++) {
        if (cache[i].w && cache[i].x == x && cache[i].y == y && cache[i].w == w && cache[i].h == h) {
            bool same = cache[i].key == key && cache[i].frame + 1 == frame_no;
            cache[i].key = key; cache[i].frame = frame_no;
            if (same) return false;
            gfx_fill(r->x, r->y, r->w, r->h, bg);
            return true;
        }
        if (free_i < 0 && (!cache[i].w || cache[i].frame + 1 < frame_no)) free_i = i;
    }
    if (free_i >= 0) { cache[free_i].x = x; cache[free_i].y = y; cache[free_i].w = w; cache[free_i].h = h; cache[free_i].key = key; cache[free_i].frame = frame_no; }
    gfx_fill(r->x, r->y, r->w, r->h, bg);
    return true;
}

int ui_legend(int x, int y, int w, int maxrows, const char *const *kv, int n, uint8_t bg) {
    int cx = 0, row = 0;
    for (int i = 0; i < n; i++) {
        const char *k = kv[2 * i], *d = kv[2 * i + 1];
        int kw = ui_cells(k) + 2, len = kw + (*d ? 1 + ui_cells(d) : 0);
        if (cx && cx + 3 + len > w) { row++; cx = 0; if (row >= maxrows) return maxrows; }
        if (cx) cx += 3;
        if (len > w) continue;
        if (!*k) { len = ui_cells(d); if (y >= 0) text_str(x + cx, y + row, d, C_DIM, bg); cx += len; continue; }   /* a plain hint */
        if (y >= 0) {
            text_put(x + cx, y + row, ' ', C_TEXT, C_BORDER);                   /* a keycap: the key on a raised tile */
            text_str(x + cx + 1, y + row, k, C_BRIGHT, C_BORDER);
            text_put(x + cx + kw - 1, y + row, ' ', C_TEXT, C_BORDER);
            if (*d) text_str(x + cx + kw + 1, y + row, d, C_DIM, bg);
        }
        cx += len;
    }
    return row + 1;
}

void ui_footer(const char *const *kv, int n) {
    int cols = text_cols(), rows = text_rows();
    text_fill(0, rows - 1, cols, 1, ' ', C_DIM, C_PANEL);
    ui_legend(2, rows - 1, cols - 4, 1, kv, n, C_PANEL);
}

void ui_label(int x, int y, const char *label, const char *value, uint8_t fg, uint8_t bg) {
    text_str(x, y, label, C_DIM, bg);
    text_str(x + ui_cells(label) + 1, y, value, fg, bg);
}

void ui_led(int x, int y, bool on, uint8_t color, const char *label, uint8_t bg) {
    text_put(x, y, on ? G_DISC : G_CIRCLE, on ? color : C_DIM, bg);
    if (label) text_str(x + 2, y, label, on ? C_TEXT : C_DIM, bg);
}

/* a slider: a thin track, the value as a rounded bar on it */
void ui_bar(int x, int y, int w, int value, int max, uint8_t fg, uint8_t bg) {
    struct rect r;
    value = CLAMP(value, 0, max);
    uint32_t key = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, value), max), fg | bg << 8);
    if (!ui_canvas_keyed(&r, x, y, w, 1, bg, key)) return;
    int th = MAX(2, r.h / 8), bh = MAX(4, r.h * 3 / 8);
    gfx_fill(r.x, r.y + (r.h - th) / 2, r.w, th, ramp(R_PANEL, 7));
    int fw = max > 0 ? (int)((int64_t)value * r.w / max) : 0;
    if (fw > 0) gfx_round(r.x, r.y + (r.h - bh) / 2, MAX(fw, 3), bh, 2, fg, -1);
}

/* ---- pixel widgets ---- */
void ui_meter_px(struct rect r, int level, int max, bool vertical) {
    int len = vertical ? r.h : r.w;
    int lit = max > 0 ? (int)((int64_t)CLAMP(level, 0, max) * len / max) : 0;
    for (int i = 0; i < len; i++) {
        int zone = i * 100 / len;
        uint8_t c = i < lit ? (zone < 70 ? ramp(R_GREEN, 12) : zone < 88 ? ramp(R_AMBER, 12) : ramp(R_RED, 12))
                            : (zone < 70 ? ramp(R_GREEN, 2) : zone < 88 ? ramp(R_AMBER, 2) : ramp(R_RED, 2));
        if (vertical) gfx_hline(r.x, r.y + r.h - 1 - i, r.w, c); else gfx_vline(r.x + i, r.y, r.h, c);
    }
}

void ui_wave_px(struct rect r, const int16_t *tab, int len, uint8_t line, uint8_t fill) {
    int mid = r.y + r.h / 2, amp = r.h / 2 - 1, prev = mid;
    bool thick = r.h >= 64;                                     /* big views get a 2-pixel line */
    for (int x = 0; x < r.w; x++) {
        int v = tab[x * len / r.w];
        int y = mid - v * amp / 32768;
        if (fill) { if (y < mid) gfx_vline(r.x + x, y, mid - y, fill); else if (y > mid) gfx_vline(r.x + x, mid + 1, y - mid, fill); }
        if (x) { gfx_line(r.x + x - 1, prev, r.x + x, y, line); if (thick) gfx_line(r.x + x - 1, prev + 1, r.x + x, y + 1, line); }
        else gfx_pixel(r.x, y, line);
        prev = y;
    }
}

/* A canvas that keeps its pixels from frame to frame: false while they are still there (the caller updates what
   changed), true when they are gone — first frame, another page, a new place — and it has been cleared. */
static bool canvas_persist(struct rect *r, int x, int y, int w, int h, uint8_t bg) {
    return ui_canvas_keyed(r, x, y, w, h, bg, 0x5C0BE5u);
}

/* The scope. A rising zero crossing holds the picture still, ~20 ms across; each column is its samples' span with a
   one-pixel glow. It redraws at 30 fps and only touches the columns that changed: the old trace is put back to the
   graticule underneath, then the new one drawn — the cheapest picture for a framebuffer that may not be cached. */
static struct { int16_t top[4096], bot[4096]; int w; uint8_t row[2400], col[4096]; } trace;   /* gfx.c's largest screen */
/* the graticule under any pixel: dotted lines at eighths across and quarters down, from tables made with the canvas */
static inline uint8_t scope_bg(int x, int y) {
    if (trace.row[y] && x % 3 == 0) return trace.row[y];
    if (trace.col[x] && y % 3 == 0) return ramp(R_GREEN, 2);
    return C_BG;
}
void ui_scope(int x, int y, int w, int h) {
    struct rect r;
    bool fresh = canvas_persist(&r, x, y, w, h, C_BG);
    if (r.w < 16 || r.h < 8) return;
    int W = MIN(r.w, 4096), Hh = MIN(r.h, 2400);
    if (fresh) {
        memset(trace.row, 0, sizeof trace.row); memset(trace.col, 0, sizeof trace.col);
        for (int i = 1; i < 4; i++) trace.row[Hh * i / 4] = ramp(R_GREEN, i == 2 ? 4 : 2);
        for (int i = 1; i < 8; i++) trace.col[W * i / 8] = 1;
        for (int yy = 0; yy < Hh; yy++) for (int xx = 0; xx < W; xx++) { uint8_t g = scope_bg(xx, yy); if (g != C_BG) gfx_pixel(r.x + xx, r.y + yy, g); }
        trace.w = 0;
    } else if (frame_no & 1) return;                         /* 30 fps is plenty for a scope */
    r.h = Hh;
    const int16_t *L, *R; uint32_t head;
    audio_scope_lr(&L, &R, &head);
    const uint32_t M = AUDIO_SCOPE_LEN - 1;
    uint32_t span = audio_rate() / 50;
    if (span > AUDIO_SCOPE_LEN / 2) span = AUDIO_SCOPE_LEN / 2;
    uint32_t start = (head - span) & M;
    for (uint32_t back = span; back < AUDIO_SCOPE_LEN - span - 2; back++) {
        uint32_t i = (head - back) & M, j = (i - 1) & M;
        if (L[j] + R[j] < 0 && L[i] + R[i] >= 0) { start = i; break; }
    }
    int mid = r.h / 2, amp = r.h / 2 - 3, py0 = mid, py1 = mid;
    for (int c = 0; c < W; c++) {
        uint32_t a = (uint32_t)c * span / (uint32_t)W, b = (uint32_t)(c + 1) * span / (uint32_t)W;
        int lo = 32767, hi = -32768;
        for (uint32_t k = a; k <= b; k++) { uint32_t i = (start + k) & M; int v = (L[i] + R[i]) >> 1; if (v < lo) lo = v; if (v > hi) hi = v; }
        int t = mid - hi * amp / 32768, bt = mid - lo * amp / 32768;
        int jt = MIN(t, py1), jb = MAX(bt, py0);                 /* joined to the last column: steep edges stay whole */
        py0 = t; py1 = bt;
        jt = CLAMP(jt, 1, r.h - 2); jb = CLAMP(jb, 1, r.h - 2);
        if (c < trace.w && trace.top[c] == jt && trace.bot[c] == jb) continue;
        uint8_t *col = gfx_row(r.y) + r.x + c;                  /* this column, top to bottom */
        int stride = (int)(gfx_row(r.y + 1) - gfx_row(r.y));
        int e0 = c < trace.w ? MIN(trace.top[c] - 1, jt - 1) : jt - 1, e1 = c < trace.w ? MAX(trace.bot[c] + 1, jb + 1) : jb + 1;
        if (c < trace.w)                                         /* put the graticule back where the old trace was */
            for (int yy = MAX(0, trace.top[c] - 1); yy <= MIN(r.h - 1, trace.bot[c] + 1); yy++) col[yy * stride] = scope_bg(c, yy);
        col[(jt - 1) * stride] = ramp(R_GREEN, 5);
        for (int yy = jt; yy <= jb; yy++) col[yy * stride] = ramp(R_GREEN, 14);
        col[(jb + 1) * stride] = ramp(R_GREEN, 5);
        gfx_dirty(r.x + c, r.y + MAX(0, e0), 1, MIN(r.h - 1, e1) - MAX(0, e0) + 1);
        trace.top[c] = (int16_t)jt; trace.bot[c] = (int16_t)jb;
    }
    trace.w = W;
}

/* Stereo image: the last ~20 ms as a Lissajous figure turned 45° — mono is a vertical line, wide material spreads.
   The previous figure is erased line by line before the new one goes down. */
static struct { int16_t x[1024], y[1024]; int n; } figure;
void ui_stereo(int x, int y, int w, int h) {
    struct rect r;
    bool fresh = canvas_persist(&r, x, y, w, h, C_BG);
    if (fresh) figure.n = 0;
    else if (frame_no & 1) return;
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2, s = MIN(r.w, r.h) / 2 - 2;
    for (int i = 1; i < figure.n; i++) gfx_line(figure.x[i - 1], figure.y[i - 1], figure.x[i], figure.y[i], C_BG);
    for (int k = -s; k <= s; k += 3) { gfx_pixel(cx + k, cy, ramp(R_CYAN, 2)); gfx_pixel(cx, cy + k, ramp(R_CYAN, 2)); }
    const int16_t *L, *R; uint32_t head;
    audio_scope_lr(&L, &R, &head);
    int n = (int)MIN(audio_rate() / 50, 1024u);
    for (int k = 0; k < n; k++) {                            /* oldest first, so the newest lies on top, brightest */
        uint32_t i = (head - (uint32_t)n + (uint32_t)k) & (AUDIO_SCOPE_LEN - 1);
        int side = (L[i] - R[i]) >> 1, mid = (L[i] + R[i]) >> 1;
        figure.x[k] = (int16_t)(cx + side * s / 32768); figure.y[k] = (int16_t)(cy - mid * s / 32768);
        if (k) gfx_line(figure.x[k - 1], figure.y[k - 1], figure.x[k], figure.y[k], ramp(R_CYAN, 4 + k * 10 / n));
    }
    figure.n = n;
}

/* SCAN: a 512-pixel strip under the pointer, 16 pixels tall, read as brightness (upper half counts double) */
void ui_scan(int16_t out[256]) {
    int W = gfx_width(), H = gfx_height(), y0 = CLAMP(ptr.y - 8, 0, H - 16), full = 255 * (8 * 2 + 8);
    for (int i = 0; i < 256; i++) {
        int x = ptr.x - 256 + i * 2, v = 0;
        if (x >= 0 && x < W) for (int r = 0; r < 16; r++) v += gfx_luma(gfx_row(y0 + r)[x]) * (r < 8 ? 2 : 1);
        out[i] = (int16_t)(v * 65535 / full - 32768);
    }
}

/* ---- the frame ---- */
static char notice[48]; static uint64_t notice_ms;
void ui_notice(const char *m, uint64_t now) { snfmt(notice, sizeof notice, "%s", m); notice_ms = now; }
static void title_bar(int page, uint64_t now) {
    int cols = text_cols();
    text_fill(0, 0, cols, 1, ' ', C_TEXT, C_PANEL);
    text_put(1, 0, G_DIAMOND, C_AMBER, C_PANEL);
    text_str(3, 0, "BARE!", C_BRIGHT, C_PANEL);
    int x = 10;
    bool wide = cols >= 150, tight = false;
    for (int i = 0, end = x; i < PAGE_COUNT; i++) { end += ui_cells(ui_pages[i]->name) + (i < 9 ? 1 : 2) + 3; tight |= !wide && end > cols - 9; }
    int pad = tight ? 2 : 3;                                  /* tight: short names, and one space less a tab */
    for (int i = 0; i < PAGE_COUNT; i++) {
        const struct page *p = ui_pages[i];
        char key[8]; if (wide) snfmt(key, sizeof key, "%s", p->key_name); else snfmt(key, sizeof key, "%d", i + 1);
        const char *nm = tight && p->short_name ? p->short_name : p->name;
        bool on = i == page;
        uint8_t bg = on ? C_AMBER : C_PANEL;
        int w = ui_cells(key) + ui_cells(nm) + pad;
        text_fill(x, 0, w, 1, ' ', C_TEXT, bg);
        text_str(x + 1, 0, key, on ? C_AMBER_D : C_DIM, bg);
        text_str(x + 2 + ui_cells(key), 0, nm, on ? C_BLACK : C_TEXT, bg);
        x += w + (wide ? 1 : 0);
    }
    /* right side, from the edge inwards: output meter, then status, while there is room */
    int rx = cols - 1;
    struct rect m = text_gfx(rx - 6, 0, 6, 1);
    gfx_fill(m.x, m.y, m.w, m.h, C_PANEL);
    int bh = MAX(2, m.h / 4);
    ui_meter_px((struct rect){ m.x, m.y + m.h / 2 - bh - 1, m.w, bh }, audio_peak(0), 32767, false);
    ui_meter_px((struct rect){ m.x, m.y + m.h / 2 + 1, m.w, bh }, audio_peak(1), 32767, false);
    rx -= 8;
    char buf[48];
    struct item { const char *t; uint8_t c; } items[8]; int n = 0;
    static char vol[16]; snfmt(vol, sizeof vol, audio_muted() ? "MUTE" : "%d dB", audio_volume_db());
    items[n++] = (struct item){ vol, audio_muted() ? C_RED : C_DIM };
    snfmt(buf, sizeof buf, "%d/%d", synth_active_voices(), SYNTH_MAX_VOICES);
    static char vox[16]; snfmt(vox, sizeof vox, "%s", buf); items[n++] = (struct item){ vox, C_DIM };
    static char dsp[16]; if (plat_audio_load() >= 0) { snfmt(dsp, sizeof dsp, "dsp %d%%", plat_audio_load()); items[n++] = (struct item){ dsp, plat_audio_load() > 60 ? C_RED : C_DIM }; }
    static char temp[24]; if (app_temp >= 0) { snfmt(temp, sizeof temp, "%s%d°C", app_thermal ? "THERMAL " : "", app_temp); items[n++] = (struct item){ temp, app_thermal ? C_RED : C_DIM }; }
    items[n++] = (struct item){ plat_audio_onebit() ? "1-BIT" : "ECHO", plat_audio_onebit() ? C_AMBER : audio_echo() ? C_GREEN : C_BORDER };
    static char state[48]; state[0] = 0;
    if (notice[0] && now - notice_ms < 2500) items[n++] = (struct item){ notice, C_BRIGHT };
    if (tape.recording) snfmt(state, sizeof state, "● REC");
    else if (stretch.frozen) snfmt(state, sizeof state, "◆ FROZEN");
    else if (seq.playing) snfmt(state, sizeof state, "▶ %s", seq.title);
    if (state[0]) items[n++] = (struct item){ state, tape.recording ? C_RED : stretch.frozen ? C_CYAN : C_GREEN };
    for (int i = 0; i < n; i++) {
        int w = ui_cells(items[i].t);
        if (rx - w - 2 <= x) break;
        rx -= w; text_str(rx, 0, items[i].t, items[i].c, C_PANEL); rx -= 3;
    }
}

/* ⇧?: the keys that work on every page. It takes the page's place, so closing it redraws the page like a page switch. */
static void draw_help(void) {
    static const char *const keys[][2] = {
        { "F1 … F12", "pages: play, sequencer, wave, stretch, operator, tape, file, mixer, touch, effects, ANS, Xenakis" },
        { "Ctrl+1 … 0 - =", "the same, on keyboards without F keys" },
        { "⇧↑  ⇧↓", "chord sound" },
        { "⇧←  ⇧→", "strum sound" },
        { "⇧B", "1-bit: the PC speaker's square wave" },
        { "⇧E", "echo" },
        { "⇧F", "freeze what you played (stretch), from any page" },
        { "⇧T", "thermal: the CPU's temperature bends the sound" },
        { "⇧-  ⇧=", "volume; the laptop's volume keys work too" },
        { "⇧M", "mute" },
        { "⇧H", "colours: ten schemes, kept with the next project save" },
        { "Ctrl+Z  Ctrl+Y", "undo, redo (or ⇧Z, ⇧Y)" },
        { "Esc", "stop, all notes off" },
        { "⇧?", "this help, or Esc" },
    };
    int n = (int)(sizeof keys / sizeof keys[0]), cols = text_cols(), rows = text_rows();
    int w = MIN(cols - 4, 86), h = MIN(rows - 4, n + 6), x = (cols - w) / 2, y = 2 + (rows - 4 - h) / 2;
    ui_panel(x, y, w, h, "KEYS", C_AMBER);
    for (int i = 0; i < n && 2 + i < h - 3; i++) {
        text_str(x + 3, y + 2 + i, keys[i][0], C_BRIGHT, C_PANEL);
        text_str_n(x + 17, y + 2 + i, keys[i][1], w - 20, C_TEXT, C_PANEL);
    }
    text_str_n(x + 3, y + h - 2, "The strip at the bottom of each page shows its own keys.", w - 6, C_DIM, C_PANEL);
}

static int last_page = -1;
void ui_redraw_all(void) {
    last_page = -3;                                          /* a page switch, as far as the canvases know */
    gfx_fill(0, 0, gfx_width(), gfx_height(), C_BG);         /* the edges the cell grid doesn't reach too */
    text_invalidate();
}

void ui_draw(uint64_t now, int page) {
    int shown = ui_help ? -2 : page;
    if (shown != last_page) { memset(cache, 0, sizeof cache); last_page = shown; }
    frame_no++;
    text_clear(C_BG);
    ptr.shape = PTR_ARROW;
    title_bar(page, now);
    if (ui_help) draw_help(); else ui_pages[page]->draw(now);
}

void ui_boot_message(const char *l1, const char *l2) {
    text_clear(C_BG);
    int cols = text_cols(), rows = text_rows();
    for (int r = 0; r < SPLASH_LOGO_ROWS; r++) text_str((cols - SPLASH_LOGO_COLS) / 2, rows / 2 - 8 + r, splash_logo[r], C_AMBER, C_BG);
    text_str(cols / 2 - ui_cells(l1) / 2, rows / 2, l1, C_TEXT, C_BG);
    text_str(cols / 2 - ui_cells(l2) / 2, rows / 2 + 1, l2, C_DIM, C_BG);
    text_flush();
    gfx_pointer(0, 0, PTR_HIDDEN);
    gfx_present();
}
