/* ANS (F11): the plate of core/ans.c. Draw on it — every finger on the touchpad scratches (pressing harder, brighter,
   where the pad can tell), the mouse draws and its right button erases — or draw straight glissandi with the line tool;
   put the camera's picture on it (Enter), or let the camera be the plate (\). Space plays: the slit crosses it in the
   pass's bars and loops. The letter rows are a keyboard of its tones: held while it plays, a key writes its row under
   the slit. The picture is redrawn by columns: those drawn on, and those the slit leaves and reaches. */
#include "ui.h"
#include "gfx.h"
#include "keys.h"
#include "ans.h"
#include "undo.h"
#include "platform.h"
#include "disk.h"

enum { T_PEN, T_LINE, T_ERASE, TOOLS };
static const char *const tool_names[TOOLS] = { "PEN", "LINE", "ERASE" };
enum { K_LENGTH, K_RANGE, K_LOOK, K_BLACK, K_INK, K_BRUSH, K_LEVEL, K_KEYS, KNOBS };
static const char *const knob_names[KNOBS] = { "length", "range", "picture", "black", "ink", "brush", "level", "keys" };
static const uint8_t lengths[] = { 1, 2, 4, 8, 16, 32 };                 /* bars; then seconds */
static const uint8_t secs[] = { 2, 4, 8, 16, 30, 60 };
static int tool, knob, ink = 208, brush = 4, kbd_oct = 3;
static struct rect plate_px;
static int last_col[FINGERS + 1] = { -1, -1, -1, -1, -1, -1 }, last_row[FINGERS + 1];
static int line_c0 = -1, line_r0, line_c1, line_r1, line_shown = -1, line_s0, line_s1;   /* the line tool: anchor, end, preview span */
static uint32_t shown_slit = 0xFFFFFFFF;
static int dirty_word;                                            /* where the last frame's drawing budget ran out */
static uint64_t clear_ms, snap_ms;                                  /* snap_ms: a picture asked for, waiting for one */
static uint32_t snap_after;
static bool key_held[128];

static int length_index(void) {
    if (ans.bars) { for (int i = 0; i < 6; i++) if (lengths[i] >= ans.bars) return i; return 5; }
    for (int i = 0; i < 6; i++) if (secs[i] >= ans.seconds) return 6 + i;
    return 11;
}
static void set_length(int i) {
    i = CLAMP(i, 0, 11);
    if (i < 6) ans.bars = lengths[i]; else { ans.bars = 0; ans.seconds = secs[i - 6]; }
}

/* ---- drawing on the plate ---- */
static void stamp(int col, int row, int value, bool erase) {
    if (col < 0 || col >= ANS_COLS) return;
    uint8_t *c = ans.plate + col * ANS_ROWS;
    int h = brush / 2;
    for (int d = -h; d <= h; d++) {
        int r = row + d;
        if (r < 0 || r >= ANS_ROWS) continue;
        if (erase) { c[r] = 0; continue; }
        int v = value * (h + 1 - (d < 0 ? -d : d)) / (h + 1);          /* softer at the brush's edges */
        if (v > c[r]) c[r] = (uint8_t)v;
    }
    ans_mark(col);
}
static void segment(int c0, int r0, int c1, int r1, int value, bool erase) {
    int dc = c1 > c0 ? c1 - c0 : c0 - c1, dr = r1 > r0 ? r1 - r0 : r0 - r1, n = MAX(dc, dr);
    for (int i = 0; i <= n; i++) stamp(c0 + (n ? (c1 - c0) * i / n : 0), r0 + (n ? (r1 - r0) * i / n : 0), value, erase);
}
static int px_col(int x) { return CLAMP((x - plate_px.x) * ANS_COLS / MAX(1, plate_px.w), 0, ANS_COLS - 1); }
static int px_row(int y) { return CLAMP((plate_px.y + plate_px.h - 1 - y) * ANS_ROWS / MAX(1, plate_px.h), 0, ANS_ROWS - 1); }

/* the camera: a picture, or live. On a BIOS boot the BIOS keeps USB, and the camera is on it: the key once says so,
   twice hands USB to BARE!'s own driver (as U in the MIDI view does) and waits for the camera to be found */
static uint8_t handover_key, wait_key; static uint64_t handover_ms, wait_ms;
static void picture_next(uint64_t now) {
    if (!plat_camera_on(true)) { ui_notice("the camera would not start: see the log", now); return; }
    const uint8_t *l; int w, h;
    snap_after = plat_camera_frame(&l, &w, &h); snap_ms = now;
}
static void go_live(uint64_t now) {
    if (!plat_camera_on(true)) { ui_notice("the camera would not start: see the log", now); return; }
    undo_one(U_ANS, 0, "the live camera", now); ans.camera = ANS_CAM_LIVE; ui_notice("the camera plays the plate", now);
}
static bool camera_here(uint8_t code, uint64_t now) {
    if (plat_camera(0, 0)) return true;
    if (plat_usb() != 1) { ui_notice("no camera", now); return false; }
    if (handover_key == code && now - handover_ms < 4000) {
        handover_key = 0;
        if (!plat_usb_takeover()) { ui_notice("USB could not be taken over", now); return false; }
        disk_rescan();                                             /* the stick is on our driver now too */
        wait_key = code; wait_ms = now;
        ui_notice("USB is BARE!'s now: looking for the camera", now);
        return false;
    }
    handover_key = code; handover_ms = now;
    ui_notice(code == KEY_ENTER ? "the camera is on USB, the BIOS's: Enter again hands it over" : "the camera is on USB, the BIOS's: \\ again hands it over", now);
    return false;
}

static void pointer(uint64_t now) {
    if (wait_key) {                                                /* after a handover: the camera, once USB has found it */
        if (plat_camera(0, 0)) { if (wait_key == KEY_ENTER) picture_next(now); else go_live(now); wait_key = 0; }
        else if (now - wait_ms > 8000) { ui_notice("no camera found on USB", now); wait_key = 0; }
    }
    if (snap_ms) {                                                 /* a picture was asked for: here it is, or not */
        const uint8_t *l; int w, h;
        uint32_t f = plat_camera_frame(&l, &w, &h);
        if (f && f != snap_after) { undo_one(U_ANS, 0, "the camera's picture", now); ans_snap(); snap_ms = 0; }
        else if (now - snap_ms > 3000) { ui_notice("the camera sent no picture", now); snap_ms = 0; }
        if (!snap_ms && ans.camera != ANS_CAM_LIVE) plat_camera_on(false);   /* its light off again */
    }
    if (!plate_px.w) return;
    /* the touchpad: every finger scratches, the pad standing for the plate */
    for (int k = 0; k < FINGERS; k++) {
        const struct finger *f = &pad.f[k];
        if (!f->on) { last_col[k] = -1; continue; }
        int col = f->x * (ANS_COLS - 1) / 32767, row = (32767 - f->y) * (ANS_ROWS - 1) / 32767;
        int v = f->z > 0 && f->z != 60 ? CLAMP(ink * (f->z - 20) / 70, 32, 255) : ink;
        if (last_col[k] < 0) undo_one(U_ANS, 0, "scratching the plate", now);
        segment(last_col[k] < 0 ? col : last_col[k], last_col[k] < 0 ? row : last_row[k], col, row, v, tool == T_ERASE);
        last_col[k] = col; last_row[k] = row;
    }
    /* the mouse: the tool; the right button erases */
    bool in = ui_in(plate_px, ptr.x, ptr.y), erase = tool == T_ERASE || (ptr.buttons & 2);
    int col = px_col(ptr.x), row = px_row(ptr.y);
    if (tool == T_LINE && !(ptr.buttons & 2)) {
        if (ptr.pressed && in) { line_c0 = line_c1 = col; line_r0 = line_r1 = row; }
        else if (line_c0 >= 0 && ptr.down) { line_c1 = col; line_r1 = row; }
        else if (line_c0 >= 0 && !ptr.down) {                      /* let go: the line goes onto the plate */
            undo_one(U_ANS, 0, "a line on the plate", now);
            segment(line_c0, line_r0, line_c1, line_r1, ink, false);
            line_c0 = -1;
        }
        return;
    }
    const int M = FINGERS;
    if ((ptr.down || (ptr.buttons & 2)) && (in || last_col[M] >= 0)) {
        if (last_col[M] < 0) undo_one(U_ANS, 0, erase ? "erasing on the plate" : "scratching the plate", now);
        segment(last_col[M] < 0 ? col : last_col[M], last_col[M] < 0 ? row : last_row[M], col, row, ink, erase);
        last_col[M] = col; last_row[M] = row;
    } else last_col[M] = -1;
}

/* ---- keys: the tools, the knobs, and the letter rows as a keyboard of the plate's tones ---- */
static const char kb_low[] = "zsxdcvgbhnjm,l.;/", kb_high[] = "q2w3er5t6y7ui9o0p";
static int key_note(uint8_t code) {
    for (int i = 0; kb_low[i]; i++) if (code == (uint8_t)kb_low[i]) return 12 * (kbd_oct + 1) + i;
    for (int i = 0; kb_high[i]; i++) if (code == (uint8_t)kb_high[i]) return 12 * (kbd_oct + 2) + i;
    return -1;
}
static int note_row(int note) { return (note - 12 * (ans.octave + 1)) * 6; }

static void turn(int k, int d) {
    switch (k) {
    case K_LENGTH: set_length(length_index() + d); break;
    case K_RANGE:  ans.octave = (uint8_t)CLAMP(ans.octave + d, 1, 4); break;
    case K_LOOK:   ans.look = ans.look == ANS_LIGHT ? ANS_EDGES : ANS_LIGHT; break;
    case K_BLACK:  ans.thresh = (uint8_t)CLAMP(ans.thresh + d * 10, 0, 250); break;
    case K_INK:    ink = CLAMP(ink + d * 16, 16, 255); break;
    case K_BRUSH:  brush = CLAMP(brush + d, 1, 13); break;
    case K_LEVEL:  ans.level = (uint8_t)CLAMP(ans.level + d * 5, 0, 100); break;
    case K_KEYS:   kbd_oct = CLAMP(kbd_oct + d, 1, 6); break;
    }
}

static bool key(uint8_t code, bool down, uint64_t now) {
    int note = key_note(code);
    if (note >= 0) {
        if (down == key_held[code & 127]) return true;           /* key repeat */
        key_held[code & 127] = down;
        if (down && ans.playing) undo_one(U_ANS, 0, "keys written into the plate", now);
        ans_note(note_row(note), down ? 100 : 0);
        return true;
    }
    if (!down) return code == KEY_SPACE || code == KEY_HOME || code == KEY_TAB || code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT ||
                      code == KEY_RIGHT || code == KEY_ENTER || code == '\\' || code == KEY_BACKSPACE || code == KEY_PGUP || code == KEY_PGDN;
    switch (code) {
    case KEY_SPACE: ans.playing = !ans.playing; return true;
    case KEY_HOME:  ans.seek = 1; return true;
    case KEY_TAB:   tool = (tool + 1) % TOOLS; line_c0 = -1; return true;
    case KEY_UP:    knob = (knob + KNOBS - 1) % KNOBS; return true;
    case KEY_DOWN:  knob = (knob + 1) % KNOBS; return true;
    case KEY_LEFT:  turn(knob, -1); return true;
    case KEY_RIGHT: turn(knob, +1); return true;
    case KEY_PGUP:  kbd_oct = MIN(kbd_oct + 1, 6); return true;
    case KEY_PGDN:  kbd_oct = MAX(kbd_oct - 1, 1); return true;
    case KEY_ENTER:                                                /* the next picture the camera sends goes on the plate */
        if (camera_here(code, now)) picture_next(now);
        return true;
    case '\\':
        if (ans.camera == ANS_CAM_LIVE) { ans.camera = ANS_CAM_OFF; plat_camera_on(false); ui_notice("camera off", now); }
        else if (camera_here(code, now)) go_live(now);
        return true;
    case KEY_BACKSPACE:                                            /* twice to clear */
        if (now - clear_ms < 2000) { undo_one(U_ANS, 0, "clearing the plate", now); ans_clear(); clear_ms = 0; ui_notice("plate cleared", now); }
        else { clear_ms = now; ui_notice("Backspace again to clear the plate", now); }
        return true;
    }
    return false;
}

static bool midi(uint8_t note, uint8_t vel, uint64_t now) {
    if (vel && ans.playing) undo_one(U_ANS, 0, "notes written into the plate", now);
    ans_note(note_row(note), vel);
    return true;
}

/* ---- the picture ---- */
static int slit_x(void) { return plate_px.x + (int)((uint64_t)(ans.pos_q16 >> 16) * (uint32_t)plate_px.w / ANS_COLS); }
static void column(int x, int sx) {                               /* a pixel column: the plate, the grid, lit near the slit */
    struct rect r = plate_px;
    if (x < r.x || x >= r.x + r.w) return;
    int col = (x - r.x) * ANS_COLS / r.w, d = x - sx, glow = d < 0 ? -d : d;
    const uint8_t *c = ans.plate + col * ANS_ROWS;
    uint32_t beats = ans.bars ? (uint32_t)ans.bars * 4 : 0;
    bool beat = beats && ((x - r.x) * beats % (uint32_t)r.w) < beats;
    for (int y = r.y; y < r.y + r.h; y++) {
        int row = (r.y + r.h - 1 - y) * ANS_ROWS / r.h, b = c[row];
        uint8_t v;
        if (glow <= 1 && ans.playing) v = ramp(R_AMBER, 15);
        else if (b) v = glow < 6 && ans.playing ? ramp(R_AMBER, 6 + b * 9 / 255) : ramp(R_GRAY, 2 + b * 13 / 255);
        else if (row % 72 == 0 && (x & 3) == 0) v = ramp(R_PANEL, 4);
        else if (beat && (y & 3) == 0) v = ramp(R_PANEL, 3);
        else v = glow < 6 && ans.playing ? ramp(R_AMBER, 1) : C_BG;
        gfx_row(y)[x] = v;
    }
    gfx_dirty(x, r.y, 1, r.h);
}
static int columns_of(int col) {                                   /* every pixel column of a plate column: pixels drawn */
    int x0 = plate_px.x + col * plate_px.w / ANS_COLS, x1 = plate_px.x + (col + 1) * plate_px.w / ANS_COLS, sx = slit_x();
    for (int x = x0; x < MAX(x1, x0 + 1); x++) column(x, sx);
    return MAX(x1 - x0, 1) * plate_px.h;
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    int sw = 30, x = 2, y = 2, pw = cols - 4 - sw - 1, ph = rows - y - 3;
    char t[96], o1[4], o2[4];
    snfmt(o1, sizeof o1, "C%d", ans.octave); snfmt(o2, sizeof o2, "C%d", ans.octave + 5);
    if (ans.bars) snfmt(t, sizeof t, "PLATE · %s to %s · 72 tones an octave · %d bar%s", o1, o2, ans.bars, ans.bars > 1 ? "s" : "");
    else snfmt(t, sizeof t, "PLATE · %s to %s · 72 tones an octave · %d s", o1, o2, ans.seconds);
    ui_panel(x, y, pw, ph, t, C_AMBER);
    for (int o = 0; o <= 5; o++) {                                 /* the octaves, down the left */
        int ry = y + 1 + (ph - 3) - o * (ph - 3) / 5;
        char lab[4]; snfmt(lab, sizeof lab, "C%d", ans.octave + o);
        text_str(x + 1, ry, lab, C_DIM, C_PANEL);
    }
    struct rect r;
    uint32_t key = ui_hash_int(UI_HASH0, cols << 16 | rows);
    bool fresh = ui_canvas_keyed(&r, x + 4, y + 1, pw - 5, ph - 2, C_BG, key);
    plate_px = r;
    int sx = slit_x();
    uint32_t slit = ans.playing ? ans.pos_q16 >> 16 : 0xFFFFFFFE;
    if (fresh) { for (int px = r.x; px < r.x + r.w; px++) column(px, sx); memset(ans.dirty, 0, sizeof ans.dirty); }
    else {
        /* drawn on since the last frame, about 4 ms of the screen's measured speed a frame (a live camera changes the
           whole plate 30 times a second; on a slow framebuffer that would hold up the main loop, and the camera's USB
           transfers with it): the rest the next frame, from where this one stopped */
        int budget = gfx_speed_mbs ? (int)MIN(gfx_speed_mbs, 100000u) * 1000 : 200000;
        for (int i = 0; i < ANS_COLS / 32 && budget > 0; i++) {
            int w = (dirty_word + i) % (ANS_COLS / 32);
            uint32_t bits = ans.dirty[w], took = 0;
            while (bits && budget > 0) { int b = __builtin_ctz(bits); bits &= bits - 1; took |= 1u << b; budget -= columns_of(w * 32 + b); }
            if (took) __atomic_fetch_and(&ans.dirty[w], ~took, __ATOMIC_RELAXED);   /* the audio side sets bits too */
            if (budget <= 0) dirty_word = w;
        }
        if (slit != shown_slit) {                                   /* the slit moved: where it was, and where it is */
            int ox = shown_slit < ANS_COLS ? r.x + (int)(shown_slit * (uint32_t)r.w / ANS_COLS) : -100;
            for (int px = ox - 6; px <= ox + 6; px++) column(px, sx);
            for (int px = sx - 6; px <= sx + 6; px++) column(px, sx);
        }
    }
    shown_slit = slit;
    /* the line tool's line, until it is let go: the old one's columns put back first */
    if (line_shown >= 0) { for (int c = line_s0; c <= line_s1; c++) columns_of(c); line_shown = -1; }
    if (line_c0 >= 0) {
        int xa = r.x + line_c0 * r.w / ANS_COLS, xb = r.x + line_c1 * r.w / ANS_COLS;
        int ya = r.y + r.h - 1 - line_r0 * r.h / ANS_ROWS, yb = r.y + r.h - 1 - line_r1 * r.h / ANS_ROWS;
        gfx_line(xa, ya, xb, yb, ramp(R_CYAN, 13));
        line_shown = 1; line_s0 = MIN(line_c0, line_c1); line_s1 = MAX(line_c0, line_c1);
    }
    ptr.shape = ui_in(r, ptr.x, ptr.y) ? PTR_CROSS : PTR_ARROW;
    /* the knobs and the camera */
    int kx = x + pw + 1;
    ui_panel(kx, y, sw, ph, ans.playing ? "ANS · playing" : "ANS", C_CYAN);
    for (int i = 0; i < KNOBS; i++) {
        int ry = y + 2 + i;
        if (ry >= y + ph - 1) break;
        char v[24];
        switch (i) {
        case K_LENGTH: if (ans.bars) snfmt(v, sizeof v, "%d bar%s", ans.bars, ans.bars > 1 ? "s" : ""); else snfmt(v, sizeof v, "%d s", ans.seconds); break;
        case K_RANGE:  snfmt(v, sizeof v, "%s-%s", o1, o2); break;
        case K_LOOK:   snfmt(v, sizeof v, "%s", ans.look == ANS_EDGES ? "edges" : "light"); break;
        case K_BLACK:  snfmt(v, sizeof v, "%d", ans.thresh); break;
        case K_INK:    snfmt(v, sizeof v, "%d", ink); break;
        case K_BRUSH:  snfmt(v, sizeof v, "%d", brush); break;
        case K_LEVEL:  snfmt(v, sizeof v, "%d", ans.level); break;
        case K_KEYS:   snfmt(v, sizeof v, "C%d", kbd_oct); break;
        }
        bool on = i == knob;
        text_put(kx + 1, ry, on ? G_DIAMOND : ' ', C_AMBER, C_PANEL);
        text_str(kx + 3, ry, knob_names[i], on ? C_BRIGHT : C_TEXT, C_PANEL);
        text_str(kx + 11, ry, v, on ? C_AMBER : C_DIM, C_PANEL);
    }
    int ty = y + 3 + KNOBS;
    if (ty < y + ph - 1) {
        text_str(kx + 3, ty, "tool", C_TEXT, C_PANEL);
        for (int i = 0, tx = kx + 11; i < TOOLS; i++) { text_str(tx, ty, tool_names[i], i == tool ? C_BLACK : C_DIM, i == tool ? C_AMBER : C_PANEL); tx += (int)ui_cells(tool_names[i]) + 1; }
    }
    char cam[40];
    if (ty + 2 < y + ph - 1) {
        if (!plat_camera(cam, sizeof cam)) text_str_n(kx + 3, ty + 2, plat_usb() == 1 ? "no camera yet: Enter twice" : "no camera", sw - 5, C_DIM, C_PANEL);
        else {
            text_str_n(kx + 3, ty + 2, cam, sw - 5, C_TEXT, C_PANEL);
            text_str_n(kx + 3, ty + 3, ans.camera == ANS_CAM_LIVE ? "live: the room plays" : "Enter: picture · \\: live", sw - 5, ans.camera == ANS_CAM_LIVE ? C_GREEN : C_DIM, C_PANEL);
        }
    }
    static const char *const tries[] = { "a line upward: glissando", "a still finger: one tone", "wave a hand at the camera", "edges: outlines as lines",
                                         "letters while it plays" };
    for (int i = 0, yy = ty + 5; i < (int)ARRAY_LEN(tries) && yy < y + ph - 1; i++, yy++) {
        if (i == 0) { text_str(kx + 3, yy++, "try", C_GREEN, C_PANEL); if (yy >= y + ph - 1) break; }
        text_str_n(kx + 3, yy, tries[i], sw - 5, C_DIM, C_PANEL);
    }
    FOOTER("SPACE", "play", "HOME", "start", "TAB", "pen / line / erase", "↑ ↓ ← →", "knobs", "ENTER", "camera picture", "\\", "live camera",
           "BKSP BKSP", "clear", "Z-/ Q-P", "its tones", "", "draw with the touchpad, every finger; the right button erases");
}

const struct page page_ans = { "ANS", "F11", KEY_F11, false, key, 0, pointer, 0, draw, true, midi, 0 };
