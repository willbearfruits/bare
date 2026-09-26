/* TAPE: an 8-track recorder with the tracks laid out as lanes of waveform under a ruler, the way a DAW shows them,
   and the cassette's character kept: reels, wow, hiss, varispeed, bounce. Arm tracks, record, play back, overdub;
   set a loop region and record over it; copy a region onto another track. */
#include "ui.h"
#include "gfx.h"
#include "tape.h"
#include "seq.h"
#include "tables.h"
#include "keys.h"
#include "undo.h"

static bool confirm_erase; static char msg[48]; static uint64_t msg_ms;
static uint32_t ang_l, ang_r; static uint64_t last_frame;
static uint32_t view_start; static int zoom = 3;                 /* the lanes show zooms[zoom] seconds from view_start */
static const uint16_t zooms[] = { 5, 10, 20, 40, 80, 160, 320, 640, 1280 };
static struct { int track; uint32_t from, to; bool valid; } clip;
static struct rect ruler_px, lane_px[TAPE_TRACKS], head_px[TAPE_TRACKS], arm_px[TAPE_TRACKS], mute_px[TAPE_TRACKS],
                   solo_px[TAPE_TRACKS], src_px[TAPE_TRACKS], level_px[TAPE_TRACKS];
static int drag = -1;                                            /* 0 ruler (loop region), 10 + t a level bar */
static struct rect over_px; static uint32_t over_song;          /* the song overview under the lanes */
static uint32_t drag_from;

static void say(const char *m, uint64_t now) { snfmt(msg, sizeof msg, "%s", m); msg_ms = now; }
/* before an edit of [from, to) on a track: its blocks there, for undo */
static void save_spans(int t, uint32_t from, uint32_t to, const char *what, uint64_t now) {
    undo_begin(U_TAPE, t, what, now);
    for (uint32_t s = from >> TAPE_BLOCK_SHIFT; s <= (MAX(to, from + 1) - 1) >> TAPE_BLOCK_SHIFT && s < TAPE_SPANS; s++) undo_save(U_TAPE, t << 12 | (int)s);
    undo_end();
}
static bool any_armed(void) { for (int t = 0; t < TAPE_TRACKS; t++) if (tape.tr[t].arm) return true; return false; }
static uint32_t view_len(void) { return zooms[zoom] * tape_rate(); }
static bool has_region(void) { return tape.loop_out > tape.loop_in; }
static void transport_play(bool on) {
    if (!on) { tape.playing = false; tape.recording = false; return; }
    if (tape.pos >= tape.len) tape.pos = 0;
    tape.frac = 0; tape.playing = true;
}
static void time_str(char *out, int cap, uint32_t frames) {
    uint32_t r = tape_rate(), s = frames / r;
    snfmt(out, (size_t)cap, "%u:%02u.%u", s / 60, s % 60, frames % r * 10 / r);
}

static bool key(uint8_t code, bool down, uint64_t now) {
    if (!down || !tape.len) return false;
    struct tape_track *tr = &tape.tr[tape.cur];
    bool confirmed = confirm_erase && now - msg_ms < 3000;
    confirm_erase = false;
    uint32_t rate = tape_rate();
    char m[48];
    if (ui_shift) switch (code) {                                 /* Shift: the region, the track's routing */
    case 'q': tr->source = tr->source == TAPE_SRC_MIX ? TAPE_SRC_IN : TAPE_SRC_MIX; return true;
    case 'w': tr->solo = !tr->solo; return true;
    case 'i': tape.loop_in = tape.pos; if (tape.loop_out <= tape.loop_in) tape.loop_out = 0; return true;
    case 'o': tape.loop_out = tape.pos; if (tape.loop_out > tape.loop_in) tape.loop = true; return true;
    case '[': tr->pan = (int8_t)MAX(-100, tr->pan - 10); return true;
    case ']': tr->pan = (int8_t)MIN(100, tr->pan + 10); return true;
    case 'c':
        if (!has_region()) { say("set a region first: ⇧I and ⇧O", now); return true; }
        clip.track = tape.cur; clip.from = tape.loop_in; clip.to = tape.loop_out; clip.valid = true;
        time_str(m, sizeof m, clip.to - clip.from); say(m, now); return true;
    case 'v':
        if (!clip.valid) { say("copy a region first: ⇧C", now); return true; }
        save_spans(tape.cur, tape.pos, tape.pos + (clip.to - clip.from), "the paste", now);
        say(tape_copy(clip.track, clip.from, clip.to, tape.cur, tape.pos) ? "pasted at the head" : "out of tape", now); return true;
    case 'd':
        if (!has_region()) { say("set a region first: ⇧I and ⇧O", now); return true; }
        save_spans(tape.cur, tape.loop_in, tape.loop_out, "the erase", now);
        say(tape_erase_range(tape.cur, tape.loop_in, tape.loop_out) ? "region erased" : "not while it records", now); return true;
    case ',': if (tr->low > -12) tr->low--; return true;              /* the shelves: low on , . and high on ; ' */
    case '.': if (tr->low < 12) tr->low++; return true;
    case ';': if (tr->high > -12) tr->high--; return true;
    case '\'': if (tr->high < 12) tr->high++; return true;
    }
    switch (code) {
    case KEY_SPACE: transport_play(!tape.playing); return true;
    case 'r': case KEY_ENTER:                                   /* record: start rolling if stopped, punch in/out while rolling */
        if (tape.recording) { tape.recording = false; return true; }
        if (!any_armed()) { say("arm a track first (Q)", now); return true; }
        if (!tape.playing) transport_play(true);
        tape_take_begin(now);
        tape.frac = 0; tape.recording = true;
        return true;
    case KEY_UP:   tape.cur = (tape.cur + TAPE_TRACKS - 1) % TAPE_TRACKS; return true;
    case KEY_DOWN: case KEY_TAB: tape.cur = (tape.cur + 1) % TAPE_TRACKS; return true;
    case 'q': tr->arm = !tr->arm; return true;
    case 'w': tr->mute = !tr->mute; return true;
    case 'e':
        if (confirmed) { save_spans(tape.cur, 0, tr->used, "the erase", now); say(tape_erase(tape.cur) ? "track erased" : "not while it records", now); }
        else { confirm_erase = true; say("press E again to erase the track", now); }
        return true;
    case 't': tape.bounce = !tape.bounce; return true;
    case 'y': tape.hiss = !tape.hiss; return true;
    case 'u': tape.wow = tape.wow == 0 ? 25 : tape.wow == 25 ? 60 : tape.wow == 60 ? 100 : 0; return true;
    case 'i': if (tape.speed_q12 > 2048) tape.speed_q12 -= 256; return true;
    case 'o': if (tape.speed_q12 < 8192) tape.speed_q12 += 256; return true;
    case 'p': tape.speed_q12 = 4096; return true;
    case '`': tape.loop = !tape.loop; return true;
    case '[': tr->level = (uint8_t)(tr->level >= 5 ? tr->level - 5 : 0); return true;
    case ']': tr->level = (uint8_t)MIN(100, tr->level + 5); return true;
    case '-': if (zoom < (int)ARRAY_LEN(zooms) - 1) zoom++; return true;
    case '=': if (zoom > 0) zoom--; return true;
    case KEY_LEFT:  tape_seek(-(int64_t)rate * 2); return true;
    case KEY_RIGHT: tape_seek((int64_t)rate * 2); return true;
    case KEY_PGUP:  tape_seek(-(int64_t)rate * 15); return true;
    case KEY_PGDN:  tape_seek((int64_t)rate * 15); return true;
    case KEY_HOME:  tape_seek_to(tape.loop && has_region() ? tape.loop_in : 0); return true;
    case KEY_END:   tape_seek_to(tape.used); return true;
    }
    return false;
}

static uint32_t frame_at(struct rect r, int x) { return view_start + (uint32_t)((uint64_t)CLAMP(x - r.x, 0, r.w) * view_len() / (uint32_t)MAX(1, r.w)); }

static void pointer(uint64_t now) {
    (void)now;
    if (!tape.len) return;
    if (!ptr.down) {
        if (drag == 0 && has_region()) tape.loop = true;
        drag = -1; return;
    }
    if (ptr.pressed) {
        drag = -1;
        if (ui_in(ruler_px, ptr.x, ptr.y)) { drag = 0; drag_from = frame_at(ruler_px, ptr.x); tape.loop_in = drag_from; tape.loop_out = 0; return; }
        if (ui_in(over_px, ptr.x, ptr.y) && !tape.recording) { tape_seek_to((uint32_t)((uint64_t)(ptr.x - over_px.x) * over_song / (uint32_t)over_px.w)); return; }
        for (int t = 0; t < TAPE_TRACKS; t++) {
            struct tape_track *tr = &tape.tr[t];
            if (ui_in(arm_px[t], ptr.x, ptr.y)) { tr->arm = !tr->arm; tape.cur = t; return; }
            if (ui_in(mute_px[t], ptr.x, ptr.y)) { tr->mute = !tr->mute; tape.cur = t; return; }
            if (ui_in(solo_px[t], ptr.x, ptr.y)) { tr->solo = !tr->solo; tape.cur = t; return; }
            if (ui_in(src_px[t], ptr.x, ptr.y)) { tr->source = tr->source == TAPE_SRC_MIX ? TAPE_SRC_IN : TAPE_SRC_MIX; tape.cur = t; return; }
            if (ui_in(level_px[t], ptr.x, ptr.y)) { drag = 10 + t; tape.cur = t; }
            if (ui_in(head_px[t], ptr.x, ptr.y)) tape.cur = t;
            if (ui_in(lane_px[t], ptr.x, ptr.y)) { tape.cur = t; if (!tape.recording) tape_seek_to(frame_at(lane_px[t], ptr.x)); return; }
        }
    }
    if (drag == 0) {
        uint32_t f = frame_at(ruler_px, ptr.x);
        tape.loop_in = MIN(f, drag_from); tape.loop_out = MAX(f, drag_from);
    } else if (drag >= 10) {
        struct rect r = level_px[drag - 10];
        tape.tr[drag - 10].level = (uint8_t)CLAMP((ptr.x - r.x) * 100 / MAX(1, r.w - 1), 0, 100);
    }
}

/* ---- the cassette, small, at the top right: the reels turn with the tape ---- */
static void reel(int cx, int cy, int R, int fill_pct, uint32_t angle, uint8_t pack) {
    int hub = MAX(3, R / 4), r_pack = hub + 2 + (R - 3 - hub - 2) * fill_pct / 100;
    gfx_disc(cx, cy, R, ramp(R_PANEL, 3));
    gfx_disc(cx, cy, r_pack, pack);
    gfx_ring(cx, cy, r_pack, ramp(R_AMBER, 6));
    gfx_disc(cx, cy, hub + 1, ramp(R_PANEL, 2));
    for (int k = 0; k < 3; k++) {                                       /* three spokes turn with the reel */
        uint32_t a = angle + (uint32_t)k * 0x55555555u;
        int32_t c = sine_q15_8192[((a >> 19) + 2048) & 8191], s = sine_q15_8192[a >> 19];
        gfx_line(cx + c * (hub / 3) / 32768, cy + s * (hub / 3) / 32768, cx + c * (r_pack - 2) / 32768, cy + s * (r_pack - 2) / 32768, ramp(R_AMBER, 9));
    }
}
static void cassette(struct rect r, uint64_t now) {
    uint32_t dt = last_frame ? (uint32_t)(now - last_frame) : 0; last_frame = now;
    if (dt > 100) dt = 100;
    int R = MAX(5, MIN(r.h / 2 - 2, r.w / 6));
    int cy = r.y + r.h / 2, xl = r.x + r.w / 4, xr = r.x + r.w * 3 / 4;
    uint32_t span = MAX(tape.used, tape.pos) + tape_rate() * 30;             /* the reels as full as the song is long */
    int fill_r = (int)((uint64_t)tape.pos * 100 / MAX(1u, span)), fill_l = 100 - fill_r;
    uint32_t turn = (uint32_t)(((uint64_t)tape.speed_q12 * dt << 32) / (4096ull * 1000));
    uint32_t rl = 40 + (uint32_t)fill_l, rr = 40 + (uint32_t)fill_r;
    if (tape.playing) { ang_l += turn / rl * 90; ang_r += turn / rr * 90; }
    if (tape.spin) { uint32_t f = turn * 20; if (tape.spin < 0) { ang_l -= f / rl * 90; ang_r -= f / rr * 90; tape.spin++; } else { ang_l += f / rl * 90; ang_r += f / rr * 90; tape.spin--; } }
    gfx_round(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 6, ramp(R_PANEL, 2), ramp(R_PANEL, 6));
    uint8_t pack = tape.recording ? ramp(R_RED, 5) : ramp(R_AMBER, 4);
    reel(xl, cy, R, fill_l, ang_l, pack);
    reel(xr, cy, R, fill_r, ang_r, pack);
    gfx_round((xl + xr) / 2 - 6, r.y + r.h - 5, 12, 3, 1, tape.recording ? ramp(R_RED, 12) : ramp(R_GRAY, 8), -1);   /* the head */
}

/* ---- lanes: each column the loudest in its stretch of time, cached until the track or the view changes ---- */
static struct { uint32_t gen, start, len; int w; uint8_t pk[4096]; int head_x; } lane[TAPE_TRACKS];

static uint8_t lane_color(int t) {
    const struct tape_track *tr = &tape.tr[t];
    if (tape.recording && tr->arm) return ramp(R_RED, 12);
    if (tr->mute) return ramp(R_GRAY, 5);
    return t == tape.cur ? ramp(R_AMBER, 12) : ramp(R_GREEN, 10);
}
/* the region's edges as lane columns (computed once a frame: a 64-bit division is a slow loop on i386) */
static int loop_c0 = -1, loop_c1 = -1;
static int col_of(uint32_t f, int w) { return f < view_start ? -1 : (int)(((uint64_t)(f - view_start) * (uint32_t)w) / view_len()); }
static void lane_column(int t, struct rect r, int c) {
    int x = r.x + c, mid = r.y + r.h / 2, amp = r.h / 2 - 2;
    bool in_loop = c >= loop_c0 && c < loop_c1;
    gfx_vline(x, r.y + 1, r.h - 1, in_loop ? ramp(R_CYAN, 1) : C_BG);
    int a = lane[t].pk[c] * amp / 255;
    if (a) gfx_vline(x, mid - a, 2 * a + 1, lane_color(t)); else gfx_pixel(x, mid, ramp(R_PANEL, 5));
    if (loop_c1 > loop_c0 && (c == loop_c0 || c == loop_c1)) for (int yy = r.y + 1; yy < r.y + r.h; yy += 2) gfx_pixel(x, yy, ramp(R_CYAN, 10));
}
static void draw_lane(int t, struct rect r, bool fresh) {
    int w = MIN(r.w, 4096);
    if (fresh) {
        uint32_t vl = view_len();
        bool same_view = lane[t].start == view_start && lane[t].len == vl && lane[t].w == w;
        if (lane[t].gen != tape.tr[t].gen || !same_view) {
            /* a track being recorded changes only near the head: those columns, not the lane */
            int c0 = 0, c1 = w;
            if (same_view && tape.recording && tape.tr[t].arm) {
                int hc = (int)(((int64_t)tape.pos - view_start) * w / (int64_t)vl);
                c0 = CLAMP(hc - w / 8 - 2, 0, w); c1 = CLAMP(hc + 2, 0, w);
            }
            lane[t].gen = tape.tr[t].gen; lane[t].start = view_start; lane[t].len = vl; lane[t].w = w;
            uint64_t step = ((uint64_t)vl << 16) / (uint32_t)w;                          /* frames a column, Q16 */
            for (int c = c0; c < c1; c++)
                lane[t].pk[c] = tape_peak(t, view_start + (uint32_t)(c * step >> 16), view_start + (uint32_t)((c + 1) * step >> 16));
        }
        gfx_hline(r.x, r.y, r.w, ramp(R_PANEL, 4));
        for (int c = 0; c < w; c++) lane_column(t, r, c);
        lane[t].head_x = -1;
    }
    int hx = col_of(tape.pos, r.w), h = hx >= 0 && hx < w ? hx : -1;
    if (h == lane[t].head_x) return;
    if (lane[t].head_x >= 0) lane_column(t, r, lane[t].head_x);
    if (h >= 0) gfx_vline(r.x + h, r.y + 1, r.h - 1, tape.recording ? ramp(R_RED, 14) : C_BRIGHT);
    lane[t].head_x = h;
}

static void draw_ruler(struct rect r) {
    uint32_t rate = tape_rate(), vl = view_len();
    static const uint16_t steps[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300 };
    uint32_t step = steps[ARRAY_LEN(steps) - 1];
    for (unsigned i = 0; i < ARRAY_LEN(steps); i++) if ((uint32_t)steps[i] * rate * (uint32_t)r.w / vl >= 70) { step = steps[i]; break; }
    const struct font *f = text_font();
    if (has_region()) {
        int xi = r.x + (int)(((int64_t)tape.loop_in - view_start) * r.w / (int64_t)vl), xo = r.x + (int)(((int64_t)tape.loop_out - view_start) * r.w / (int64_t)vl);
        xi = CLAMP(xi, r.x, r.x + r.w); xo = CLAMP(xo, r.x, r.x + r.w);
        if (xo > xi) gfx_fill(xi, r.y + r.h - 5, xo - xi, 4, tape.loop ? ramp(R_CYAN, 10) : ramp(R_CYAN, 4));
    }
    for (uint32_t s = (view_start / rate / step) * step; (uint64_t)s * rate < (uint64_t)view_start + vl; s += step) {
        int64_t fx = (int64_t)s * rate;
        if (fx < view_start) continue;
        int x = r.x + (int)((fx - view_start) * r.w / (int64_t)vl);
        gfx_vline(x, r.y + r.h / 2, r.h / 2, ramp(R_PANEL, 9));
        char lab[12]; snfmt(lab, sizeof lab, "%u:%02u", s / 60, s % 60);
        gfx_text(x + 3, r.y + (r.h - f->height) / 2 - 1, lab, f, C_DIM, -1, 1);
    }
    int64_t hx = ((int64_t)tape.pos - view_start) * r.w / (int64_t)vl;
    if (hx >= 0 && hx < r.w) {                                           /* the head, as a marker on the ruler */
        int x = r.x + (int)hx;
        for (int k = 0; k < 5; k++) gfx_hline(x - k, r.y + r.h - 1 - k, 2 * k + 1, tape.recording ? ramp(R_RED, 14) : C_BRIGHT);
    }
}

/* the whole song: every track's peaks summed into one strip, the view as a frame, the head as a line */
static struct { uint8_t pk[4096]; int vx0, vx1, head_x; } ov;
static void ov_column(struct rect r, int c) {
    int mid = r.y + r.h / 2, amp = r.h / 2 - 1, y = ov.pk[c] * amp / 255;
    gfx_vline(r.x + c, r.y, r.h, c >= ov.vx0 && c < ov.vx1 ? ramp(R_AMBER, 2) : C_BG);
    if (y) gfx_vline(r.x + c, mid - y, 2 * y + 1, ramp(R_GREEN, 7)); else gfx_pixel(r.x + c, mid, ramp(R_PANEL, 5));
}
static void draw_overview(struct rect r, uint32_t song, bool fresh) {
    int w = MIN(r.w, 4096);
    if (fresh) {
        uint64_t step = ((uint64_t)song << 16) / (uint32_t)w;
        for (int c = 0; c < w; c++) {
            uint32_t a = (uint32_t)(c * step >> 16), z = (uint32_t)((c + 1) * step >> 16);
            int m = 0;
            for (int t = 0; t < TAPE_TRACKS; t++) if (!tape.tr[t].mute) m = MAX(m, tape_peak_coarse(t, a, z));
            ov.pk[c] = (uint8_t)m;
        }
        ov.vx0 = (int)((uint64_t)view_start * (uint32_t)w / song); ov.vx1 = ov.vx0 + MAX(3, (int)((uint64_t)view_len() * (uint32_t)w / song));
        for (int c = 0; c < w; c++) ov_column(r, c);
        ov.head_x = -1;
    }
    int h = MIN(w - 1, (int)((uint64_t)tape.pos * (uint32_t)w / song));
    if (h == ov.head_x) return;
    if (ov.head_x >= 0) ov_column(r, ov.head_x);
    gfx_vline(r.x + h, r.y, r.h, tape.recording ? ramp(R_RED, 14) : C_BRIGHT);
    ov.head_x = h;
}

static void header(int t, int x, int y, int w, int h) {
    struct tape_track *tr = &tape.tr[t];
    bool sel = t == tape.cur;
    uint8_t bg = sel ? C_BORDER : C_PANEL;
    text_fill(x, y, w, h, ' ', C_TEXT, bg);
    head_px[t] = text_rect(x, y, w, h);
    char b[16]; snfmt(b, sizeof b, "%d", t + 1);
    text_str(x + 1, y, b, sel ? C_AMBER : C_DIM, bg);
    text_put(x + 3, y, tr->arm ? G_DISC : G_CIRCLE, tr->arm ? C_RED : C_DIM, bg); arm_px[t] = text_rect(x + 3, y, 1, 1);
    text_str(x + 5, y, "M", tr->mute ? C_BLACK : C_DIM, tr->mute ? C_RED : bg); mute_px[t] = text_rect(x + 5, y, 1, 1);
    text_str(x + 7, y, "S", tr->solo ? C_BLACK : C_DIM, tr->solo ? C_AMBER : bg); solo_px[t] = text_rect(x + 7, y, 1, 1);
    const char *src = tr->source == TAPE_SRC_IN ? "IN" : "MIX";
    text_str(x + 9, y, src, tr->source == TAPE_SRC_IN ? C_CYAN : C_DIM, bg); src_px[t] = text_rect(x + 9, y, 3, 1);
    if (w >= 20) { uint32_t s = tr->used / tape_rate(); snfmt(b, sizeof b, "%u:%02u", s / 60, s % 60); text_str(x + w - 1 - ui_cells(b), y, b, tr->used ? C_DIM : C_BORDER, bg); }
    if (h < 2) return;
    level_px[t] = text_rect(x + 1, y + 1, w - 6, 1);
    ui_bar(x + 1, y + 1, w - 6, tr->level, 100, tr->mute ? C_DIM : C_GREEN, bg);
    snfmt(b, sizeof b, "%3d", tr->level); text_str(x + w - 4, y + 1, b, C_TEXT, bg);
    if (h < 3) return;
    if (tr->pan) snfmt(b, sizeof b, "%c%d", tr->pan < 0 ? 'L' : 'R', tr->pan < 0 ? -tr->pan : tr->pan); else snfmt(b, sizeof b, "C");
    ui_label(x + 1, y + 2, "PAN", b, tr->pan ? C_CYAN : C_TEXT, bg);
    if (w >= 20) {
        struct rect v = ui_canvas(x + 10, y + 2, w - 11, 1, bg);
        ui_meter_px((struct rect){ v.x, v.y + v.h / 3, v.w, MAX(2, v.h / 3) }, tr->vu, 32767, false);
    }
}

static void draw(uint64_t now) {
    int cols = text_cols(), rows = text_rows();
    int x = 1, w = cols - 2, th = rows >= 45 ? 6 : 4;
    ui_panel(x, 1, w, th, "TAPE · 8 TRACK", C_AMBER);
    if (!tape.len) {
        text_str(x + 2, 2, "no memory for a tape on this machine", C_RED, C_PANEL);
        FOOTER("", "the tape needs a few MiB of RAM", "F1", "play page");
        return;
    }
    uint32_t rate = tape_rate();
    char buf[64];
    /* transport: state, the time big, bar and beat, what is left */
    const char *state = tape.recording ? "● REC" : tape.playing ? "▶ PLAY" : "■ STOP";
    text_str(x + 2, 2, state, tape.recording ? C_RED : tape.playing ? C_GREEN : C_DIM, C_PANEL);
    struct rect tc = ui_canvas(x + 10, 2, 13, 2, C_PANEL);
    uint32_t sec = tape.pos / rate;
    snfmt(buf, sizeof buf, "%02u:%02u.%u", sec / 60, sec % 60, tape.pos % rate * 10 / rate);
    gfx_text(tc.x, tc.y + (tc.h - 24) / 2, buf, &font_t12x24, tape.recording ? C_RED : C_BRIGHT, -1, 1);
    uint32_t beat = (uint32_t)((uint64_t)tape.pos * (seq.bpm ? seq.bpm : 120) / (60ull * rate));
    snfmt(buf, sizeof buf, "bar %u.%u  %u bpm", beat / 4 + 1, beat % 4 + 1, seq.bpm);
    text_str(x + 25, 2, buf, C_TEXT, C_PANEL);
    uint32_t fs = tape_free_seconds();
    snfmt(buf, sizeof buf, "%u:%02u of tape left", fs / 60, fs % 60);
    text_str(x + 25, 3, buf, tape.full || fs < 20 ? C_RED : C_DIM, C_PANEL);
    /* the loop, then the tape's character, as far as they fit left of the cassette */
    int ix = x + 47, room = x + w - 28 - ix;
    if (room >= 22) {
        ui_led(ix, 2, tape.loop, C_CYAN, "LOOP", C_PANEL);
        if (has_region()) { char a[16], z[16]; time_str(a, sizeof a, tape.loop_in); time_str(z, sizeof z, tape.loop_out); snfmt(buf, sizeof buf, "%s-%s", a, z); text_str_n(ix + 7, 2, buf, room - 7, C_CYAN, C_PANEL); }
        ui_led(ix, 3, tape.bounce, C_AMBER, "BOUNCE", C_PANEL);
        ui_led(ix + 10, 3, tape.hiss, C_GREEN, "HISS", C_PANEL);
    }
    if (room >= 44) {
        snfmt(buf, sizeof buf, "%u%%", (uint32_t)tape.speed_q12 * 100 / 4096);
        ui_label(ix + 24, 2, "speed", buf, tape.speed_q12 != 4096 ? C_AMBER : C_TEXT, C_PANEL);
        snfmt(buf, sizeof buf, "%u", tape.wow); ui_label(ix + 24, 3, "wow", buf, C_TEXT, C_PANEL);
    }
    if (tape.full) say("the tape is full", now);
    if (msg[0] && now - msg_ms < 3000) text_str(x + 2, th, msg, C_GREEN, C_PANEL);
    /* the cassette */
    struct rect cr;
    uint32_t ck = ui_hash_int(ui_hash_int(UI_HASH0, (int32_t)(ang_l >> 24) | (int32_t)(ang_r >> 24) << 8 | tape.recording << 16), (int32_t)(tape.pos / (rate / 2 + 1)));
    bool moving = tape.playing || tape.spin;
    if (ui_canvas_keyed(&cr, x + w - 26, 2, 24, th - 2, C_PANEL, moving ? ck ^ (uint32_t)now : ck)) cassette(cr, now);
    else last_frame = now;
    /* follow the head */
    uint32_t vl = view_len();
    if (tape.pos < view_start || tape.pos >= view_start + vl) view_start = tape.pos > vl / 20 ? tape.pos - vl / 20 : 0;
    /* ruler and lanes */
    int hw = cols >= 140 ? 24 : 18, ly = th + 2, lh = MAX(1, (rows - 2 - ly) / TAPE_TRACKS), lx = x + hw;
    text_str(x + 1, th + 1, "TRACK", C_DIM, C_BG);
    uint32_t rk = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, (int32_t)view_start), zoom | tape.loop << 8 | tape.recording << 9),
                              (int32_t)(((int64_t)tape.pos - view_start) * 4096 / (int64_t)vl));
    rk = ui_hash_int(ui_hash_int(rk, (int32_t)tape.loop_in), (int32_t)tape.loop_out);
    if (ui_canvas_keyed(&ruler_px, lx, th + 1, x + w - lx, 1, C_BG, rk)) draw_ruler(ruler_px);
    int lw = text_rect(lx, ly, x + w - lx, 1).w;                                         /* the lanes' width in pixels */
    loop_c0 = loop_c1 = -1;
    if (has_region()) { loop_c0 = tape.loop_in < view_start ? 0 : col_of(tape.loop_in, lw); loop_c1 = tape.loop_out < view_start ? -1 : col_of(tape.loop_out, lw); }
    for (int t = 0; t < TAPE_TRACKS; t++) {
        int y = ly + t * lh;
        header(t, x, y, hw - 1, lh);
        uint32_t k = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, (int32_t)tape.tr[t].gen), (int32_t)view_start), zoom);
        k = ui_hash_int(ui_hash_int(ui_hash_int(k, (int32_t)tape.loop_in), (int32_t)tape.loop_out), lane_color(t));
        bool fresh = ui_canvas_keyed(&lane_px[t], lx, y, x + w - lx, lh, C_BG, k);
        draw_lane(t, lane_px[t], fresh);
    }
    /* under the lanes, where there is room: the selected track's settings, and the whole song with the view on it */
    int by = ly + TAPE_TRACKS * lh, left = rows - 1 - by;
    if (left >= 1) {
        const struct tape_track *tr = &tape.tr[tape.cur];
        snfmt(buf, sizeof buf, "TRACK %d  %s  low %+d dB  high %+d dB", tape.cur + 1, tr->source == TAPE_SRC_IN ? "input" : "mix", tr->low, tr->high);
        text_str_n(x + 1, by, buf, hw + 30, C_DIM, C_BG);
    }
    over_px = (struct rect){ 0, 0, 0, 0 };
    if (left >= 3) {
        uint32_t song = MAX(tape.used, tape.pos) + tape_rate() * 5;
        song = (song + tape_rate() * 10 - 1) / (tape_rate() * 10) * (tape_rate() * 10);       /* in 10 s steps: fewer redraws */
        uint32_t ok = ui_hash_int(ui_hash_int(ui_hash_int(UI_HASH0, (int32_t)song), (int32_t)view_start), zoom);
        for (int t = 0; t < TAPE_TRACKS; t++) ok = ui_hash_int(ok, (int32_t)(tape.tr[t].gen >> 6) | tape.tr[t].mute << 30);
        draw_overview(over_px, song, ui_canvas_keyed(&over_px, lx, by + 1, x + w - lx, left - 2, C_BG, ok));
        over_song = song;
    }
    if (ui_in(ruler_px, ptr.x, ptr.y) || ui_in(over_px, ptr.x, ptr.y)) ptr.shape = PTR_CROSS;
    FOOTER("SPACE", "play", "R", "record", "↑ ↓", "track", "Q", "arm", "W", "mute", "⇧W", "solo", "⇧Q", "mix/input",
           "← →", "±2 s", "- =", "zoom", "[ ]", "level", "⇧[ ]", "pan", "⇧I ⇧O", "region", "`", "loop", "⇧C ⇧V", "copy/paste",
           "⇧D", "erase region", "E E", "erase track", "⇧, ⇧.", "low", "⇧; ⇧'", "high", "T", "bounce", "Y", "hiss", "U", "wow", "I O P", "speed");
}

const struct page page_tape = { "TAPE", "F6", KEY_F6, true, key, 0, pointer, 0, draw };
