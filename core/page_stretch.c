/* STRETCH: the last 10 s of everything played, frozen and paulstretched. */
#include "ui.h"
#include "gfx.h"
#include "stretch.h"
#include "audio.h"
#include "keys.h"

static bool key(uint8_t code, bool down, uint64_t now) { return stretch_key(code, down, now); }

/* ---- the capture ring, drawn as the ring it is: each column always shows the same stretch of memory, the cursor is
   now, sweeping right and writing over the oldest sound. Only the columns that change are drawn again. ---- */
#define MAXCOL 2048
static uint8_t col_state[MAXCOL];                   /* what a column was drawn as: 1 region, 2 read window, 4 frozen, 8 cursor */
static uint32_t drawn_head;

static void capture_column(struct rect r, int c, uint8_t state) {
    uint32_t head; const int16_t *cb = stretch_capture_buf(&head);
    uint32_t len = stretch.cap_len, per = len / (uint32_t)r.w, i = (uint32_t)c * per;
    int mid = r.y + r.h / 2, amp = r.h / 2 - 1, lo = 0, hi = 0;
    for (uint32_t k = 0; k < per; k += 8, i += 8) { int v = cb[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
    uint8_t bg = state & 8 ? ramp(R_GRAY, 12) : state & 2 ? ramp(R_GRAY, 4) : state & 1 ? ramp(R_CYAN, 2) : C_BG;
    gfx_vline(r.x + c, r.y, r.h, bg);
    if (state & 8) return;                                         /* the cursor: a bright line */
    uint8_t col = state & 2 ? C_BRIGHT : state & 1 ? ramp(R_CYAN, 12) : ramp(R_CYAN, state & 4 ? 5 : 11);
    int y0 = mid - hi * amp / 32768, y1 = mid - lo * amp / 32768;
    gfx_vline(r.x + c, y0, y1 - y0 + 1, col);
}

static void capture_view(int x, int y, int w, int h) {
    struct rect r;
    bool fresh = ui_canvas_keyed(&r, x, y, w, h, C_BG, 0xCA97u ^ (uint32_t)stretch.cap_len);
    int W = MIN(r.w, MAXCOL);
    r.w = W;
    uint32_t head; stretch_capture_buf(&head);
    uint32_t len = stretch.cap_len, per = len / (uint32_t)W;
    int cur = MIN((int)(head / per), W - 1);
    /* columns the capture wrote into since the last frame get new pictures */
    int from = MIN((int)(drawn_head / per), W - 1), to = cur;
    for (int c = 0; c < W; c++) {
        uint32_t s0 = (uint32_t)c * per;
        uint8_t st = stretch.frozen ? 4 : 0;
        if (stretch.frozen) {
            if ((s0 + len - stretch.region_start) % len < stretch.region_len) st |= 1;
            if ((s0 + len - stretch.pos) % len < stretch.win) st |= 2;
        } else if (c == cur) st |= 8;
        bool written = !stretch.frozen && (from <= to ? c >= from && c <= to : c >= from || c <= to);
        if (fresh || written || st != col_state[c]) { capture_column(r, c, st); col_state[c] = st; }
    }
    drawn_head = head;
}

/* ---- spectrum: bars rise at once and fall smoothly, a peak mark holds; only the part of a bar that moved is drawn ---- */
static uint8_t bars[STRETCH_BANDS], peaks[STRETCH_BANDS], shown_bar[STRETCH_BANDS], shown_peak[STRETCH_BANDS];
static uint64_t peak_at[STRETCH_BANDS];

static uint8_t bar_colour(int row, int h) {                         /* colour follows height */
    int z = row * 100 / h;
    return z < 60 ? ramp(R_GREEN, 6 + z / 8) : z < 85 ? ramp(R_AMBER, 12) : ramp(R_RED, 12);
}
static void bar_rows(struct rect r, int bx, int bw, int from, int to, bool on) {   /* rows [from, to) up from the bottom */
    int ih = r.h - 2;
    for (int row = from; row < to; ) {
        uint8_t c = on ? bar_colour(row, ih) : C_BG;
        int end = row + 1;
        while (end < to && (on ? bar_colour(end, ih) : C_BG) == c) end++;   /* runs of one colour in one fill */
        gfx_fill(bx + 1, r.y + r.h - end, bw - 2, end - row, c);
        row = end;
    }
}

static void spectrum_view(int x, int y, int w, int h, uint64_t now) {
    for (int b = 0; b < STRETCH_BANDS; b++) {
        int target = stretch.frozen ? stretch.bands[b] : 0;
        bars[b] = (uint8_t)(target > bars[b] ? target : MAX(target, bars[b] - 3));
        if (bars[b] >= peaks[b]) { peaks[b] = bars[b]; peak_at[b] = now; }
        else if (now - peak_at[b] > 700 && peaks[b]) peaks[b]--;
    }
    struct rect r;
    bool fresh = ui_canvas_keyed(&r, x, y, w, h, C_BG, 0x5BEC7u);
    int bw = r.w / STRETCH_BANDS, ih = r.h - 2; if (bw < 2) return;
    for (int b = 0; b < STRETCH_BANDS; b++) {
        int bx = r.x + b * bw;
        int now_h = bars[b] * ih / 100, was_h = fresh ? 0 : shown_bar[b] * ih / 100;
        int pk = peaks[b] * ih / 100, was_pk = fresh ? -1 : shown_peak[b] * ih / 100;
        if (!fresh && now_h == was_h && pk == was_pk) continue;
        if (was_pk >= 0 && shown_peak[b]) gfx_hline(bx + 1, r.y + r.h - 1 - was_pk, bw - 2, was_pk < was_h ? bar_colour(was_pk, ih) : C_BG);
        if (now_h > was_h) bar_rows(r, bx, bw, was_h, now_h, true);
        else if (now_h < was_h) bar_rows(r, bx, bw, now_h, was_h, false);
        if (peaks[b]) gfx_hline(bx + 1, r.y + r.h - 1 - pk, bw - 2, ramp(R_GRAY, 12));
        shown_bar[b] = bars[b]; shown_peak[b] = peaks[b];
    }
}

static void draw(uint64_t now) {
    int cols = text_cols(), rows = text_rows();
    int x = 2, y = 2, w = cols - 4;
    ui_panel(x, y, w, 10, "CAPTURE · a ring of the last 10 seconds", C_CYAN);
    capture_view(x + 1, y + 1, w - 2, 7);
    if (stretch.frozen) {
        char r[48]; snfmt(r, sizeof r, "frozen · phrase %u.%u s", stretch.region_len / audio_rate(), stretch.region_len * 10 / audio_rate() % 10);
        text_str(x + 2, y + 8, r, C_CYAN, C_PANEL);
    } else text_str_n(x + 2, y + 8, "the bright line is now; it writes over the oldest sound as it goes", w - 4, C_DIM, C_PANEL);
    int sy = y + 11, sh = 6;
    ui_panel(x, sy, w, sh, "STRETCH", C_PINK);
    char buf[48];
    int cx = x + 2;
    if (stretch.frozen) { text_put(cx, sy + 1, G_DIAMOND, C_CYAN, C_PANEL); text_str(cx + 2, sy + 1, "FROZEN", C_BRIGHT, C_PANEL); }
    else { text_put(cx, sy + 1, G_CIRCLE, C_DIM, C_PANEL); text_str(cx + 2, sy + 1, "listening", C_TEXT, C_PANEL); }
    cx += 13;
    snfmt(buf, sizeof buf, "×%u", stretch.factor); ui_label(cx, sy + 1, "slower", buf, C_AMBER, C_PANEL); cx += 14;
    snfmt(buf, sizeof buf, "%u ms", stretch.win * 1000 / audio_rate()); ui_label(cx, sy + 1, "window", buf, C_TEXT, C_PANEL); cx += 16;
    snfmt(buf, sizeof buf, "%u%%", stretch.mix); ui_label(cx, sy + 1, "mix", buf, C_TEXT, C_PANEL); cx += 10;
    if (cx + 14 < x + w) ui_led(cx, sy + 1, stretch.stay, C_GREEN, "STAY", C_PANEL);
    ui_bar(x + 2, sy + 2, MIN(w - 4, 40), stretch.mix, 100, C_PINK, C_PANEL);
    LEGEND(x + 2, sy + 3, w - 4, 2, C_PANEL, "SPACE", "freeze", "← →", "slower/faster", "↑ ↓", "window", "[ ]", "scrub",
           "- =", "mix", "ENTER", "stay", "⇧F", "freeze from any page");
    int py = sy + sh + 1, ph = rows - py - 2;
    if (ph >= 5) {
        ui_panel(x, py, w, ph, "SPECTRUM", C_SCOPE);
        spectrum_view(x + 1, py + 1, w - 2, ph - 2, now);
    }
    FOOTER("", "Play something, freeze it: it becomes a very slow place.", "A-' Z-/", "chords and strums still play here");
}

const struct page page_stretch = { "STRETCH", "F4", KEY_F4, true, key, 0, 0, 0, draw, false, 0, "STR" };
