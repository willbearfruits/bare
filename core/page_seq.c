/* SEQ: the tracker. Rows run down the page, a column per channel: note, instrument, volume, effect. The song is the
   order list along the top; `\` moves between the pattern and the order list. It keeps playing on the other pages. */
#include "ui.h"
#include "gfx.h"
#include "seq.h"
#include "synth.h"
#include "omni.h"
#include "keys.h"
#include "undo.h"

enum { F_NOTE, F_INST1, F_INST2, F_VOL1, F_VOL2, F_FX, F_PAR1, F_PAR2, FIELDS };
static int cur_row, cur_ch, cur_field, octave = 3, inst = 1, ch0;       /* ch0: the first channel on screen */
static bool order_focus; static int ord_cur;
static uint64_t cursor_moved;
static char msg[40]; static uint64_t msg_ms;
static struct seq_pattern clip; static bool clip_valid;
static struct rect grid_px, order_px; static int grid_top, grid_rows, grid_cw, grid_x0;   /* for the pointer */

/* two rows of a piano, 17 semitones each: the bottom letter row an octave below the top one */
static const char keys_low[]  = "zsxdcvgbhnjm,l.;/";
static const char keys_high[] = "q2w3er5t6y7ui9o0p";
static const int8_t field_x[FIELDS] = { 0, 4, 5, 7, 8, 10, 11, 12 };     /* where each field sits in a channel's column */
#define COL_W 14

static void say(const char *m, uint64_t now) { snfmt(msg, sizeof msg, "%s", m); msg_ms = now; }
static void save_pat(uint64_t now) { char w[24]; snfmt(w, sizeof w, "pattern %02u", seq.edit_pat); undo_one(U_PATTERN, seq.edit_pat, w, now); }
static void save_song(const char *what, uint64_t now) { undo_one(U_SONG, 0, what, now); }
static struct seq_pattern *pat(void) { return &seq_pat[seq.edit_pat % SEQ_PATTERNS]; }
static int hexval(uint8_t code) { return code >= '0' && code <= '9' ? code - '0' : code >= 'a' && code <= 'f' ? code - 'a' + 10 : -1; }

static void move(int dr, int dc, uint64_t now) {
    int rows = pat()->rows;
    cur_row = ((cur_row + dr) % rows + rows) % rows;
    cur_ch = CLAMP(cur_ch + dc, 0, SEQ_TRACKS - 1);
    cursor_moved = now;
}

static void set_nibble(uint8_t *v, bool high, int d, uint8_t none, uint8_t max) {
    uint8_t x = *v == none ? 0 : *v;
    x = high ? (uint8_t)((x & 0x0F) | d << 4) : (uint8_t)((x & 0xF0) | d);
    *v = MIN(x, max);
}

static bool order_key(uint8_t code, uint64_t now) {
    if (code == KEY_UP || code == KEY_DOWN || code == KEY_INSERT || code == KEY_DELETE || code == KEY_BACKSPACE) save_song("the order list", now);
    switch (code) {
    case KEY_LEFT:  ord_cur = MAX(0, ord_cur - 1); break;
    case KEY_RIGHT: ord_cur = MIN(seq.song_len - 1, ord_cur + 1); break;
    case KEY_UP:    seq.order[ord_cur] = (uint8_t)((seq.order[ord_cur] + 1) % SEQ_PATTERNS); break;
    case KEY_DOWN:  seq.order[ord_cur] = (uint8_t)((seq.order[ord_cur] + SEQ_PATTERNS - 1) % SEQ_PATTERNS); break;
    case KEY_INSERT:                                        /* a copy of this entry after it */
        if (seq.song_len >= SEQ_ORDER) { say("the song is full", now); break; }
        memmove(&seq.order[ord_cur + 2], &seq.order[ord_cur + 1], (size_t)(seq.song_len - ord_cur - 1));
        seq.order[ord_cur + 1] = seq.order[ord_cur]; seq.song_len++; ord_cur++;
        break;
    case KEY_DELETE: case KEY_BACKSPACE:
        if (seq.song_len <= 1) break;
        memmove(&seq.order[ord_cur], &seq.order[ord_cur + 1], (size_t)(seq.song_len - ord_cur - 1));
        seq.song_len--; ord_cur = MIN(ord_cur, seq.song_len - 1);
        break;
    case KEY_ENTER: case '\\': order_focus = false; break;
    default: return false;
    }
    seq.edit_pat = seq.order[ord_cur];
    if (!seq.playing) seq.ord = (uint8_t)ord_cur;
    cur_row = MIN(cur_row, pat()->rows - 1);
    return true;
}

static bool key(uint8_t code, bool down, uint64_t now) {
    if (!down) return true;
    if (ui_shift) switch (code) {
    case '1': case '2': case '3':                           /* the demo songs (unshifted, 2 3 … are notes) */
        if (code - '1' < seq_demo_count()) {
            undo_begin(U_SONG, 0, "loading a demo", now); undo_save(U_SONG, 0);
            for (int p = 0; p < SEQ_PATTERNS; p++) undo_save(U_PATTERN, p);
            undo_end();
            seq_load_demo(code - '1'); ord_cur = 0; cur_row = 0; if (!seq.playing) seq_play(true);
        }
        return true;
    case KEY_SPACE: seq_play_pattern(!(seq.playing && seq.pattern_only)); return true;
    case '[': inst = inst > 1 ? inst - 1 : P_COUNT; return true;
    case ']': inst = inst < P_COUNT ? inst + 1 : 1; return true;
    case KEY_PGUP: seq.edit_pat = (uint8_t)((seq.edit_pat + SEQ_PATTERNS - 1) % SEQ_PATTERNS); cur_row = MIN(cur_row, pat()->rows - 1); return true;
    case KEY_PGDN: seq.edit_pat = (uint8_t)((seq.edit_pat + 1) % SEQ_PATTERNS); cur_row = MIN(cur_row, pat()->rows - 1); return true;
    case 'c': clip = *pat(); clip_valid = true; say("pattern copied", now); return true;
    case 'v': if (clip_valid) { save_pat(now); *pat() = clip; say("pattern pasted", now); } return true;
    case 'l': save_pat(now); pat()->rows = (uint8_t)(pat()->rows >= SEQ_ROWS ? 16 : pat()->rows + 16); cur_row = MIN(cur_row, pat()->rows - 1); return true;
    case 'p': { save_song("the rows a beat", now); static const uint8_t lpbs[] = { 2, 3, 4, 6, 8 }; int i = 0; while (i < 4 && lpbs[i] != seq.lpb) i++; seq.lpb = lpbs[(i + 1) % 5]; return true; }
    }
    if (code == KEY_SPACE) { if (seq.playing) seq_play(false); else { seq.ord = (uint8_t)ord_cur; seq_play(true); } return true; }
    if (code == '\\' && !order_focus) { order_focus = true; ord_cur = MIN(seq.ord, seq.song_len - 1); return true; }
    if (order_focus) return order_key(code, now);
    struct seq_cell *cell = &pat()->cell[cur_row][cur_ch];
    /* the note column: a piano on two rows; ` lets the note go */
    if (cur_field == F_NOTE) {
        for (int i = 0; keys_low[i]; i++) {
            int n = code == (uint8_t)keys_low[i] ? (octave + 1) * 12 + i : code == (uint8_t)keys_high[i] ? (octave + 2) * 12 + i : -1;
            if (n < 0) continue;
            save_pat(now);
            cell->note = (uint8_t)CLAMP(n, 1, 127); cell->inst = (uint8_t)inst;
            if (!seq.playing) seq_audition(cur_ch, cell->note, cell->inst);
            move(1, 0, now);
            return true;
        }
        if (code == '`') { save_pat(now); cell->note = NOTE_OFF; cell->inst = INST_NONE; move(1, 0, now); return true; }
    } else {
        int d = hexval(code);
        if (d >= 0) {
            save_pat(now);
            switch (cur_field) {
            case F_INST1: case F_INST2: set_nibble(&cell->inst, cur_field == F_INST1, d, INST_NONE, P_COUNT); break;
            case F_VOL1: case F_VOL2: {                     /* stored as volume + 1: 0 is an empty cell */
                uint8_t v = cell->vol == VOL_NONE ? 0 : cell->vol - 1;
                set_nibble(&v, cur_field == F_VOL1, d, 0xFF, 127); cell->vol = (uint8_t)(v + 1); break; }
            case F_FX:                  cell->fx = (uint8_t)d; break;
            default:                    set_nibble(&cell->param, cur_field == F_PAR1, d, 0xFF, 0xFF); break;
            }
            move(1, 0, now);
            return true;
        }
    }
    switch (code) {
    case KEY_UP:    move(-1, 0, now); return true;
    case KEY_DOWN:  move(1, 0, now); return true;
    case KEY_PGUP:  move(-16, 0, now); return true;
    case KEY_PGDN:  move(16, 0, now); return true;
    case KEY_HOME:  cur_row = 0; cursor_moved = now; return true;
    case KEY_END:   cur_row = pat()->rows - 1; cursor_moved = now; return true;
    case KEY_LEFT:
        if (cur_field > 0) cur_field--; else if (cur_ch > 0) { cur_ch--; cur_field = FIELDS - 1; }
        cursor_moved = now; return true;
    case KEY_RIGHT:
        if (cur_field < FIELDS - 1) cur_field++; else if (cur_ch < SEQ_TRACKS - 1) { cur_ch++; cur_field = 0; }
        cursor_moved = now; return true;
    case KEY_TAB: cur_ch = (cur_ch + (ui_shift ? SEQ_TRACKS - 1 : 1)) % SEQ_TRACKS; cur_field = F_NOTE; cursor_moved = now; return true;
    case KEY_DELETE: case KEY_BACKSPACE:
        save_pat(now);
        if (cur_field == F_NOTE || cur_field == F_INST1 || cur_field == F_INST2) { cell->note = NOTE_NONE; cell->inst = INST_NONE; }
        else if (cur_field == F_VOL1 || cur_field == F_VOL2) cell->vol = VOL_NONE;
        else { cell->fx = 0; cell->param = 0; }
        move(1, 0, now); return true;
    case KEY_INSERT: {                                      /* push the column down a row from here */
        save_pat(now);
        struct seq_pattern *pt = pat();
        for (int r = pt->rows - 1; r > cur_row; r--) pt->cell[r][cur_ch] = pt->cell[r - 1][cur_ch];
        memset(&pt->cell[cur_row][cur_ch], 0, sizeof *cell);
        return true; }
    case '[': if (octave > 0) octave--; return true;
    case ']': if (octave < 8) octave++; return true;
    case '-': save_song("the tempo", now); if (seq.bpm > 32) seq.bpm--; return true;
    case '=': save_song("the tempo", now); if (seq.bpm < 300) seq.bpm++; return true;
    case KEY_ENTER: seq_mute(cur_ch, !seq.ch[cur_ch].mute); return true;
    }
    return false;
}

static void pointer(uint64_t now) {
    if (!ptr.pressed) return;
    if (ui_in(order_px, ptr.x, ptr.y)) {
        int i = (ptr.x - order_px.x) / (3 * text_font()->width);
        if (i < seq.song_len) { ord_cur = i; seq.edit_pat = seq.order[i]; if (!seq.playing) seq.ord = (uint8_t)i; cur_row = MIN(cur_row, pat()->rows - 1); }
        return;
    }
    if (!ui_in(grid_px, ptr.x, ptr.y)) return;
    const struct font *f = text_font();
    int r = grid_top + (ptr.y - grid_px.y) / f->height, cx = (ptr.x - grid_px.x) / f->width - grid_x0;
    if (r < 0 || r >= pat()->rows || cx < 0) return;
    cur_row = r; cur_ch = MIN(SEQ_TRACKS - 1, ch0 + cx / grid_cw);
    int fx = cx % grid_cw; cur_field = 0;
    for (int i = 0; i < FIELDS; i++) if (fx >= field_x[i]) cur_field = i;
    order_focus = false; cursor_moved = now;
}

/* ---- drawing ---- */
static void note_str(uint8_t n, char *out) {
    static const char *const names[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    if (n == NOTE_NONE) { out[0] = out[1] = out[2] = '.'; out[3] = 0; return; }
    if (n == NOTE_OFF) { out[0] = out[1] = out[2] = '='; out[3] = 0; return; }
    int o = n / 12 - 1;
    out[0] = names[n % 12][0]; out[1] = names[n % 12][1]; out[2] = (char)(o < 0 ? '-' : '0' + o % 10); out[3] = 0;
}
static void hex2(uint8_t v, bool none, char *out) { static const char h[] = "0123456789ABCDEF"; if (none) { out[0] = out[1] = '.'; } else { out[0] = h[v >> 4]; out[1] = h[v & 15]; } out[2] = 0; }

static void cell_text(int x, int y, const struct seq_cell *c, bool mute, uint8_t bg, int cursor_field) {
    char n[4], i[3], v[3], p[3];
    note_str(c->note, n); hex2(c->inst, c->inst == INST_NONE, i); hex2((uint8_t)(c->vol - 1), c->vol == VOL_NONE, v);
    bool fx_none = !c->fx && !c->param; hex2(c->param, fx_none, p);
    char f[2] = { fx_none ? '.' : "0123456789ABCDEF"[c->fx & 15], 0 };
    uint8_t nc = c->note == NOTE_NONE ? C_BORDER : mute ? C_DIM : c->note == NOTE_OFF ? C_DIM : C_BRIGHT;
    text_str(x, y, n, nc, bg);
    text_str(x + 4, y, i, c->inst == INST_NONE ? C_BORDER : mute ? C_DIM : C_AMBER, bg);
    text_str(x + 7, y, v, c->vol == VOL_NONE ? C_BORDER : mute ? C_DIM : C_GREEN, bg);
    text_str(x + 10, y, f, fx_none ? C_BORDER : mute ? C_DIM : C_CYAN, bg);
    text_str(x + 11, y, p, fx_none ? C_BORDER : mute ? C_DIM : C_CYAN, bg);
    if (cursor_field < 0) return;
    int fx = field_x[cursor_field], w = cursor_field == F_NOTE ? 3 : 1;         /* the field under the cursor, inverted */
    for (int k = 0; k < w; k++) {
        uint8_t g = text_peek(x + fx + k, y);
        text_put(x + fx + k, y, g, C_BLACK, order_focus ? C_DIM : C_BRIGHT);
    }
}

static const char *const fx_help[16] = {
    "arpeggio: +x, +y semitones", "slide up", "slide down", "glide to the note", "vibrato: speed x, depth y", "", "", "",
    "pan: 00 left, 80 centre, FF right", "", "volume: up x or down y a tick", "jump to order xx", "volume xx (00-7F)",
    "next pattern, at row xx", "EC cut · ED delay · E9 retrigger (x ticks)", "tempo (20-FF) or rows a beat (01-1F)" };

static void draw(uint64_t now) {
    int cols = text_cols(), rows = text_rows();
    bool wide = cols >= 150;
    int side_w = wide ? cols - 4 - 4 - SEQ_TRACKS * COL_W - 2 : 0;
    int x = 1, w = cols - 2 - (side_w ? side_w + 1 : 0), h = rows - 3;
    char title[48]; snfmt(title, sizeof title, "TRACKER · %s", seq.title);
    ui_panel(x, 1, w, h, title, C_PINK);
    char buf[64];
    /* the status line: transport, tempo, octave, instrument */
    int sx = x + 2;
    text_str(sx, 2, seq.playing ? (seq.pattern_only ? "▶ PATTERN" : "▶ SONG") : "■ STOP", seq.playing ? C_GREEN : C_DIM, C_PANEL); sx += 11;
    snfmt(buf, sizeof buf, "%u", seq.bpm); ui_label(sx, 2, "bpm", buf, C_AMBER, C_PANEL); sx += 9;
    snfmt(buf, sizeof buf, "%u", seq.lpb); ui_label(sx, 2, "lpb", buf, C_TEXT, C_PANEL); sx += 7;
    snfmt(buf, sizeof buf, "%d", octave); ui_label(sx, 2, "oct", buf, C_TEXT, C_PANEL); sx += 7;
    snfmt(buf, sizeof buf, "%02X %s", inst, synth_preset_name(inst - 1)); ui_label(sx, 2, "inst", buf, C_AMBER, C_PANEL); sx += 6 + ui_cells(buf) + 2;
    if (msg[0] && now - msg_ms < 3000) text_str_n(sx, 2, msg, x + w - 2 - sx, C_GREEN, C_PANEL);
    /* the order list: the song, the entry playing, the one being edited */
    text_str(x + 2, 3, "ORDER", order_focus ? C_BRIGHT : C_DIM, C_PANEL);
    int ox = x + 8, room = (x + w - 2 - ox - 16) / 3, first = MAX(0, ord_cur - room / 2);
    order_px = text_rect(ox, 3, room * 3, 1);
    for (int i = 0; i < room && first + i < seq.song_len; i++) {
        int o = first + i; bool play = seq.playing && !seq.pattern_only && o == seq.ord, sel = o == ord_cur;
        snfmt(buf, sizeof buf, "%02u", seq.order[o]);
        uint8_t bg = sel && order_focus ? C_BRIGHT : play ? ramp(R_GREEN, 5) : sel ? C_BORDER : C_PANEL;
        text_str(ox + i * 3, 3, buf, sel && order_focus ? C_BLACK : C_TEXT, bg);
    }
    snfmt(buf, sizeof buf, "PATTERN %02u · %u ROWS", seq.edit_pat, pat()->rows);
    text_str(x + w - 2 - ui_cells(buf), 3, buf, C_PINK, C_PANEL);
    /* channel headers, then the grid: as many channels as fit, the cursor's always among them */
    int nvis = MIN(SEQ_TRACKS, (w - 2 - 4) / COL_W);
    if (cur_ch < ch0) ch0 = cur_ch;
    if (cur_ch >= ch0 + nvis) ch0 = cur_ch - nvis + 1;
    int gx = x + 5, gy = 5;
    for (int c = 0; c < nvis; c++) {
        int ci = ch0 + c, cx = gx + c * COL_W;
        bool on = ci == cur_ch;
        snfmt(buf, sizeof buf, "%d %s", ci + 1, seq.ch[ci].name);
        text_str_n(cx, 4, buf, COL_W - 3, seq.ch[ci].mute ? C_RED : on ? C_BRIGHT : C_TEXT, C_PANEL);
        text_put(cx + COL_W - 3, 4, seq_channel_sounding(ci) ? G_DISC : G_CIRCLE, seq_channel_sounding(ci) ? C_GREEN : C_BORDER, C_PANEL);
    }
    int vis = rows - 3 - gy;                                             /* rows of the grid */
    bool follow = seq.playing && now - cursor_moved > 2500 && (seq.pattern_only || seq.order[seq.ord] == seq.edit_pat);
    /* editing: the cursor's row in the middle. Following the song: the page stays put and the play line moves down it,
       turning the page near the bottom — a scrolling grid would rewrite every row on screen at every step */
    static int page_top;
    int top;
    if (follow && seq.pos >= 0) {
        if (page_top < 0 || seq.pos < page_top || seq.pos >= page_top + vis - 2) page_top = MAX(0, MIN(seq.pos - 2, pat()->rows - vis + 2));
        top = page_top;
    } else top = page_top = cur_row - vis / 2;
    grid_px = text_rect(gx - 4, gy, 4 + nvis * COL_W, vis); grid_top = top; grid_rows = vis; grid_cw = COL_W; grid_x0 = 4;
    bool playing_here = seq.playing && (seq.pattern_only ? true : seq.order[seq.ord] == seq.edit_pat);
    for (int k = 0; k < vis; k++) {
        int r = top + k, y = gy + k;
        if (r < 0 || r >= pat()->rows) continue;
        bool beat = r % seq.lpb == 0, bar = r % (seq.lpb * 4) == 0, play = playing_here && r == seq.pos, cur = r == cur_row;
        uint8_t bg = play ? ramp(R_GREEN, 4) : cur ? C_BORDER : beat ? ramp(R_PANEL, 2) : C_PANEL;
        text_fill(gx - 4, y, 4 + nvis * COL_W, 1, ' ', C_TEXT, bg);
        snfmt(buf, sizeof buf, "%02d", r);
        text_str(gx - 4, y, buf, bar ? C_AMBER : beat ? C_TEXT : C_DIM, bg);
        for (int c = 0; c < nvis; c++) {
            int ci = ch0 + c;
            cell_text(gx + c * COL_W, y, &pat()->cell[r][ci], seq.ch[ci].mute, bg, cur && ci == cur_ch ? cur_field : -1);
        }
    }
    /* what the effect under the cursor does */
    const struct seq_cell *cc = &pat()->cell[cur_row][cur_ch];
    if (cur_field >= F_FX && (cc->fx || cc->param)) text_str_n(x + 2, rows - 3, fx_help[cc->fx & 15], w - 4, C_CYAN, C_PANEL);
    /* the instruments and the effects, where there is room beside the grid */
    if (side_w >= 24) {
        int px = x + w + 1;
        ui_panel(px, 1, side_w, h, "SOUNDS", C_AMBER);
        int n = MIN(P_COUNT, h - 20);
        int start = CLAMP(inst - 1 - n / 2, 0, MAX(0, P_COUNT - n));
        for (int i = 0; i < n; i++) {
            int id = start + i + 1;
            snfmt(buf, sizeof buf, "%02X %s", id, synth_preset_name(id - 1));
            text_str_n(px + 2, 2 + i, buf, side_w - 4, id == inst ? C_BLACK : C_TEXT, id == inst ? C_AMBER : C_PANEL);
        }
        int ey = 3 + n;
        text_str(px + 2, ey++, "EFFECTS", C_DIM, C_PANEL);
        static const char *const fx_short[] = { "0xy arpeggio", "1xx slide up", "2xx slide down", "3xx glide", "4xy vibrato", "8xx pan",
                                                "Axy volume slide", "Bxx jump", "Cxx volume", "Dxx break", "ECx cut", "EDx delay", "E9x retrigger", "Fxx tempo" };
        for (unsigned i = 0; i < ARRAY_LEN(fx_short) && ey < 1 + h - 1; i++) text_str_n(px + 2, ey++, fx_short[i], side_w - 4, C_CYAN, C_PANEL);
    }
    if (order_focus) FOOTER("← →", "position", "↑ ↓", "pattern", "INS", "repeat", "DEL", "remove", "\\ ENTER", "back to the pattern");
    else FOOTER("SPACE", "play song", "⇧SPACE", "loop pattern", "Z-/ Q-P", "notes", "`", "off", "0-F", "hex", "← → ↑ ↓", "move", "TAB", "channel",
                "DEL", "clear", "INS", "insert", "[ ]", "octave", "⇧[ ]", "sound", "- =", "tempo", "\\", "order", "⇧PGUP PGDN", "pattern",
                "ENTER", "mute", "⇧C ⇧V", "copy/paste", "⇧L", "length", "⇧P", "lpb", "⇧1-3", "demos");
}

/* MIDI: stopped, a note goes in at the cursor like a key would; playing, it just plays the sound */
static bool midi_note(uint8_t note, uint8_t vel, uint64_t now) {
    if (!vel || seq.playing || order_focus || cur_field != F_NOTE) return false;
    struct seq_cell *cell = &pat()->cell[cur_row][cur_ch];
    save_pat(now);
    cell->note = note ? note : 1; cell->inst = (uint8_t)inst; cell->vol = vel >= 100 ? VOL_NONE : (uint8_t)(vel + 1);
    seq_audition(cur_ch, cell->note, cell->inst);
    move(1, 0, now);
    return true;
}
static int strum_sound(void) { return inst - 1; }

const struct page page_seq = { "SEQ", "F2", KEY_F2, false, key, 0, pointer, strum_sound, draw, false, midi_note };
