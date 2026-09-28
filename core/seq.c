#include "seq.h"
#include "link.h"
#include "synth.h"
#include "libc.h"
#include "platform.h"
#include "tables.h"
#include "midi.h"

struct seq_state seq;
struct seq_pattern seq_pat[SEQ_PATTERNS];
static uint32_t rate = 48000;
static uint32_t row_q16;                   /* frames a row, Q16 */
static uint32_t row_bpm_q16; static uint8_t row_lpb;   /* what row_q16 was computed for */
uint32_t seq_link_q16;                              /* Link's exact tempo, BPM × 65536; 0: seq.bpm */
static uint32_t rows_done;                          /* rows since the start (Link's phase) */
static uint32_t row_left_q16;              /* until the next row, while playing */
static int      next_tick;                 /* the tick due next in this row, 1..SEQ_TICKS */
static int8_t   jump_ord = -1, break_row = -1;     /* Bxx / Dxx: where the next row comes from */
static uint32_t audition_left[SEQ_TRACKS];         /* frames until an auditioned note is let go */
static uint8_t  audition_on;

#define TAG_SEQ 0x300
#define DEFAULT_VEL 110

/* what each channel is doing between rows: its note, and the effects that move it tick by tick */
static struct chan {
    uint8_t note, preset, vol; bool on;
    int32_t bend, sent_bend;               /* 1/256 semitone from the note: slides and glides; what the voice has */
    int32_t slide, glide_to, glide_speed;
    uint8_t arp_x, arp_y, vib_speed, vib_depth, vib_phase;
    int8_t  vol_slide, cut, delay, retrig; /* -1: none this row */
    int     pan;
    uint8_t sent_vol;
    struct seq_cell pending;               /* a delayed cell (EDx) */
    uint8_t mem[16];                       /* an effect's last parameter, for xx = 00 */
} ch[SEQ_TRACKS];

/* ---- demo songs: bars of 16 tokens ("E2", "." rest, "-" tie), NULL repeats the bar before ---- */
struct demo_track { const char *name; uint8_t preset; uint8_t gate; const char *bars[4]; };
struct demo { const char *title; uint16_t bpm; struct demo_track tracks[SEQ_TRACKS]; };

/* BARE METAL: an original riff for the demo — a galloping E with a tritone turn, then C and D — and stabs over it */
#define RIFF_E "E2 . E2 E2 . E2 G2 . E2 . E2 E2 . E2 A#2 A2"
#define RIFF_C "C2 . C2 C2 . C2 E2 . D2 . D2 D2 . D2 F#2 G2"
#define TOP_E  ". . B3 . . . G3 . . . E4 . D4 . B3 ."
#define TOP_C  ". . G3 . . . E3 . . . A3 . B3 . D4 ."
#define FOUR   "C2 . . . C2 . . . C2 . . . C2 . . ."
#define BACKBEAT ". . . . C3 . . . . . . . C3 . . ."
#define EIGHTHS "C4 . C4 . C4 . C4 . C4 . C4 . C4 . C4 ."

static const struct demo demos[] = {
    { "BARE METAL", 175, {
        { "BASS",  P_GRIND,   60, { RIFF_E, RIFF_E, RIFF_C, RIFF_E } },
        { "LEAD",  P_CHIP,   70, { TOP_E, TOP_E, TOP_C, TOP_E } },
        { "KICK",  P_KICK,   50, { FOUR, 0, 0, 0 } },
        { "SNARE", P_SNARE,  50, { BACKBEAT, 0, 0, 0 } },
        { "HAT",   P_HAT,    50, { EIGHTHS, 0, 0, 0 } },
        { "FREE",  P_SQLEAD, 60, { 0, 0, 0, 0 } },
        { "FREE",  P_BELL,   60, { 0, 0, 0, 0 } },
        { "FREE",  P_ORGAN,  60, { 0, 0, 0, 0 } },
    } },
    { "HARBOR", 96, {
        { "CH1",   P_FATSAW, 100, { "C3 - - - - - - - - - - - - - - -", "A2 - - - - - - - - - - - - - - -", "F2 - - - - - - - - - - - - - - -", "G2 - - - - - - - - - - - - - - -" } },
        { "CH2",   P_FATSAW, 100, { "E3 - - - - - - - - - - - - - - -", "C3 - - - - - - - - - - - - - - -", "A2 - - - - - - - - - - - - - - -", "B2 - - - - - - - - - - - - - - -" } },
        { "CH3",   P_FATSAW, 100, { "G3 - - - - - - - - - - - - - - -", "E3 - - - - - - - - - - - - - - -", "C3 - - - - - - - - - - - - - - -", "D3 - - - - - - - - - - - - - - -" } },
        { "BASS",  P_SAWBASS, 55, { "C2 . . . . . C2 . . . C2 . . . . .", "A1 . . . . . A1 . . . A1 . . . . .", "F1 . . . . . F1 . . . F1 . . . . .", "G1 . . . . . G1 . . . G1 . . . G2 ." } },
        { "ARP",   P_BELL,    30, { "C4 E4 G4 C5 E4 G4 C5 E5 G4 E4 C5 G4 E4 C4 G4 E4", "A3 C4 E4 A4 C4 E4 A4 C5 E4 C4 A4 E4 C4 A3 E4 C4", "F3 A3 C4 F4 A3 C4 F4 A4 C4 A3 F4 C4 A3 F3 C4 A3", "G3 B3 D4 G4 B3 D4 G4 B4 D4 B3 G4 D4 B3 G3 D4 B3" } },
        { "KICK",  P_KICK,    50, { "C2 . . . . . . . C2 . . . . . C2 .", 0, 0, 0 } },
        { "SNARE", P_SNARE,   50, { BACKBEAT, 0, 0, 0 } },
        { "HAT",   P_HAT,     50, { ". . C4 . . . C4 . . . C4 . . . C4 C4", 0, 0, 0 } },
    } },
    { "EMPTY", 120, {
        { "BASS",  P_SAWBASS, 60, { 0, 0, 0, 0 } }, { "LEAD", P_SQLEAD, 60, { 0, 0, 0, 0 } },
        { "CH1",   P_ORGAN,  100, { 0, 0, 0, 0 } }, { "CH2",  P_ORGAN, 100, { 0, 0, 0, 0 } },
        { "ARP",   P_BELL,    40, { 0, 0, 0, 0 } }, { "KICK", P_KICK,   50, { 0, 0, 0, 0 } },
        { "SNARE", P_SNARE,   50, { 0, 0, 0, 0 } }, { "HAT",  P_HAT,    50, { 0, 0, 0, 0 } },
    } },
};
int seq_demo_count(void) { return (int)ARRAY_LEN(demos); }
const char *seq_demo_title(int n) { return demos[n].title; }

#define STEP_REST 0
#define STEP_TIE  255
static int parse_token(const char *t, int len) {
    if (len == 1 && t[0] == '.') return STEP_REST;
    if (len == 1 && t[0] == '-') return STEP_TIE;
    static const int8_t pc[7] = { 9, 11, 0, 2, 4, 5, 7 };      /* A B C D E F G */
    if (t[0] < 'A' || t[0] > 'G') return STEP_REST;
    int n = pc[t[0] - 'A'], i = 1;
    if (t[i] == '#') { n++; i++; } else if (t[i] == 'b') { n--; i++; }
    return (t[i] - '0' + 1) * 12 + n;
}
static void fill_bar(uint8_t *note, int bar, const char *pat) {
    int step = bar * 16;
    while (*pat && step < (bar + 1) * 16) {
        while (*pat == ' ') pat++;
        int len = 0;
        while (pat[len] && pat[len] != ' ') len++;
        if (!len) break;
        note[step++] = (uint8_t)parse_token(pat, len);
        pat += len;
    }
}

/* Steps with ties and a gate (the 1.0 sequencer, and the demos) as tracker rows: each note on its row, held through
   its ties, then let go where the gate said — a cut (ECx) inside its last row, or a note-off on the rest after it. */
static void steps_to_rows(struct seq_pattern *pt, int c, uint8_t preset, uint8_t gate, const uint8_t *note, const uint8_t *vel) {
    for (int s = 0; s < SEQ_ROWS; s++) {
        uint8_t n = note[s];
        if (n == STEP_REST || n == STEP_TIE || n > 127) continue;
        struct seq_cell *cell = &pt->cell[s][c];
        cell->note = n; cell->inst = (uint8_t)(preset + 1); cell->vol = vel && vel[s] && vel[s] != DEFAULT_VEL ? (uint8_t)(MIN(vel[s], 127) + 1) : VOL_NONE;
        int hold = 1;
        while (s + hold < SEQ_ROWS && note[s + hold] == STEP_TIE) hold++;
        if (gate < 100) { struct seq_cell *last = &pt->cell[s + hold - 1][c]; last->fx = 0xE; last->param = (uint8_t)(0xC0 | MAX(1, gate * SEQ_TICKS / 100)); }
        else if (s + hold < SEQ_ROWS && note[s + hold] == STEP_REST) pt->cell[s + hold][c].note = NOTE_OFF;
    }
}

/* a channel's note ends: the voice, and MIDI out */
static void chan_off(int c) {
    synth_note_off_tag(TAG_SEQ | c);
    if (ch[c].on) midi_out_note(c, ch[c].note, 0);
    ch[c].on = false;
}
static void stop_all(void) {
    for (int c = 0; c < SEQ_TRACKS; c++) chan_off(c);
    audition_on = 0;
}

static void reset_song(const char *title, uint16_t bpm) {
    memset(seq_pat, 0, sizeof seq_pat);
    for (int p = 0; p < SEQ_PATTERNS; p++) seq_pat[p].rows = SEQ_ROWS;
    snfmt(seq.title, sizeof seq.title, "%s", title);
    seq.bpm = bpm; seq.lpb = 4; seq.song_len = 1; memset(seq.order, 0, sizeof seq.order);
    seq.ord = 0; seq.pos = -1; seq.edit_pat = 0;
}

void seq_load_demo(int n) {
    if (n < 0 || n >= seq_demo_count()) return;
    const struct demo *d = &demos[n];
    uint32_t st = plat_irq_save();
    bool was_playing = seq.playing;
    seq.playing = false;
    stop_all();
    reset_song(d->title, d->bpm);
    for (int c = 0; c < SEQ_TRACKS; c++) {
        const struct demo_track *dt = &d->tracks[c];
        snfmt(seq.ch[c].name, sizeof seq.ch[c].name, "%s", dt->name ? dt->name : "");
        seq.ch[c].inst = (uint8_t)(dt->preset + 1); seq.ch[c].mute = false;
        uint8_t note[SEQ_ROWS] = { 0 };
        const char *last = 0;
        for (int b = 0; b < 4; b++) { const char *bar = dt->bars[b] ? dt->bars[b] : last; if (bar) fill_bar(note, b, bar); last = bar; }
        steps_to_rows(&seq_pat[0], c, dt->preset, dt->gate ? dt->gate : 60, note, 0);
    }
    seq.playing = was_playing;
    plat_irq_restore(st);
}

void seq_begin(const char *title, uint16_t bpm) {
    uint32_t st = plat_irq_save();
    seq.playing = false; seq.pattern_only = false;
    stop_all();
    reset_song(title, bpm);
    plat_irq_restore(st);
}

void seq_import_steps(const char *title, uint16_t bpm, int t, const char *name, uint8_t preset, uint8_t gate, bool mute,
                      const uint8_t *note, const uint8_t *vel) {
    if (t == 0) reset_song(title, bpm);
    snfmt(seq.ch[t].name, sizeof seq.ch[t].name, "%s", name);
    seq.ch[t].inst = (uint8_t)(preset % P_COUNT + 1); seq.ch[t].mute = mute;
    steps_to_rows(&seq_pat[0], t, preset % P_COUNT, gate, note, vel);
}

void seq_init(void) {
    memset(&seq, 0, sizeof seq);
    memset(ch, 0, sizeof ch);
    seq_load_demo(0);
}

int seq_pattern_used(int p) {
    int n = 0;
    for (int r = 0; r < SEQ_ROWS; r++) for (int c = 0; c < SEQ_TRACKS; c++) {
        const struct seq_cell *x = &seq_pat[p].cell[r][c];
        if (x->note || x->inst || x->vol != VOL_NONE || x->fx || x->param) n++;
    }
    return n;
}
bool seq_channel_sounding(int c) { return ch[c].on && !seq.ch[c].mute; }

static int preset_of(int c, uint8_t inst) {                /* an instrument number (or the channel's) as a preset */
    int i = inst ? inst : seq.ch[c].inst;
    return CLAMP(i - 1, 0, P_COUNT - 1);
}

static void start(bool pattern_only) {
    uint32_t st = plat_irq_save();
    stop_all();
    seq.playing = true; seq.pattern_only = pattern_only;
    if (seq.ord >= seq.song_len) seq.ord = 0;
    if (!pattern_only) seq.edit_pat = seq.order[seq.ord];
    seq.pos = -1; row_left_q16 = 0; next_tick = SEQ_TICKS; jump_ord = break_row = -1; rows_done = 0;
    for (int c = 0; c < SEQ_TRACKS; c++) { ch[c].pan = 0; ch[c].vol = DEFAULT_VEL; ch[c].preset = (uint8_t)preset_of(c, 0); ch[c].bend = 0; }
    plat_irq_restore(st);
}
void seq_play(bool on) {
    if (on) { if (!link_start_request(false)) { start(false); midi_transport(true); } return; }   /* Link: on its next bar */
    uint32_t st = plat_irq_save();
    bool was = seq.playing;
    seq.playing = false; stop_all();
    plat_irq_restore(st);
    if (was) { midi_transport(false); link_stop_request(); }
}
void seq_play_pattern(bool on) { if (on) { if (!link_start_request(true)) { start(true); midi_transport(true); } } else seq_play(false); }
/* ---- Link (the audio side) ---- */
void seq_start_in(uint32_t frames, bool pattern_only) { start(pattern_only); row_left_q16 = frames << 16; }
void seq_stop_quietly(void) { uint32_t st = plat_irq_save(); seq.playing = false; stop_all(); plat_irq_restore(st); }
int64_t seq_rows_q16(void) { return (int64_t)rows_done * 65536 - (row_q16 ? (int64_t)(((uint64_t)row_left_q16 << 16) / row_q16) : 0); }
uint32_t seq_row_q16(void) { return row_q16; }
void seq_nudge(int32_t q16) { row_left_q16 = q16 < 0 && (uint32_t)-q16 > row_left_q16 ? 0 : row_left_q16 + (uint32_t)q16; }
void seq_init_rate(uint32_t r) { rate = r ? r : 48000; row_bpm_q16 = 0; }

/* ---- a row ---- */
static void trigger(int c, const struct seq_cell *cell) {
    struct chan *k = &ch[c];
    if (cell->inst) k->preset = (uint8_t)preset_of(c, cell->inst);
    bool glide = cell->fx == 0x3 && k->on;
    if (cell->note == NOTE_OFF) { chan_off(c); return; }
    if (cell->note >= 1 && cell->note <= 127 && !glide) {
        chan_off(c);
        k->vol = cell->vol != VOL_NONE ? (uint8_t)MIN(cell->vol - 1, 127) : DEFAULT_VEL;
        k->note = cell->note; k->bend = 0; k->sent_bend = 0; k->sent_vol = k->vol;
        if (!seq.ch[c].mute) { synth_note_on_pan(k->note, (uint8_t)MAX(1, k->vol), k->preset, TAG_SEQ | c, k->pan); k->on = true; midi_out_note(c, k->note, (uint8_t)MAX(1, k->vol)); }
        return;
    }
    if (cell->vol != VOL_NONE) k->vol = (uint8_t)MIN(cell->vol - 1, 127);
}

/* this row's effect: set up what the ticks will do, or do it now */
static void effect(int c, const struct seq_cell *cell) {
    struct chan *k = &ch[c];
    uint8_t fx = cell->fx, p = cell->param;
    if (!fx && !p) return;
    if (!p && fx != 0 && fx != 0xE && fx != 0xC && fx != 0x8 && fx != 0xB && fx != 0xD) p = k->mem[fx];   /* 00: as before */
    else k->mem[fx] = p;
    switch (fx) {
    case 0x0: k->arp_x = p >> 4; k->arp_y = p & 15; break;
    case 0x1: k->slide = p * 4; break;                            /* 1/64 semitone a tick per step of xx */
    case 0x2: k->slide = -(int32_t)p * 4; break;
    case 0x3: if (cell->note >= 1 && cell->note <= 127 && k->on) k->glide_to = ((int32_t)cell->note - k->note) * 256;
              k->glide_speed = p * 4; break;
    case 0x4: k->vib_speed = p >> 4; k->vib_depth = p & 15; break;
    case 0x8: k->pan = ((int)p - 128) * 100 / 128; if (k->on) synth_tag_pan(TAG_SEQ | c, k->pan); break;
    case 0xA: k->vol_slide = (int8_t)((p >> 4) ? (p >> 4) : -(p & 15)); break;
    case 0xB: jump_ord = (int8_t)MIN(p, SEQ_ORDER - 1); break;
    case 0xC: k->vol = MIN(p, 127); break;
    case 0xD: break_row = (int8_t)MIN(p, SEQ_ROWS - 1); break;
    case 0xE:
        if ((p >> 4) == 0xC) k->cut = (int8_t)(p & 15);
        else if ((p >> 4) == 0x9 && (p & 15)) k->retrig = (int8_t)(p & 15);
        break;
    case 0xF: if (p >= 0x20) seq.bpm = p; else if (p) seq.lpb = p; break;
    }
}

/* where the voice's pitch and level should be now, sent only when they change */
static void update(int c, int t) {
    struct chan *k = &ch[c];
    if (!k->on) return;
    int32_t b = k->bend;
    if (k->arp_x || k->arp_y) { int step = t % 3; b += (step == 1 ? k->arp_x : step == 2 ? k->arp_y : 0) * 256; }
    if (k->vib_depth) b += sine_q15[k->vib_phase] * k->vib_depth / 2048;          /* depth F: about ±1 semitone */
    if (b != k->sent_bend) { synth_tag_bend(TAG_SEQ | c, b); k->sent_bend = b; }
    if (k->vol != k->sent_vol) { synth_tag_velocity(TAG_SEQ | c, (uint8_t)MAX(1, k->vol)); k->sent_vol = k->vol; }
}

static void row(void) {
    /* where this row is: the next one, unless the last row broke or jumped */
    struct seq_pattern *pt = &seq_pat[seq.pattern_only ? seq.edit_pat : seq.order[seq.ord] % SEQ_PATTERNS];
    int pos = seq.pos + 1;
    if (jump_ord >= 0 || break_row >= 0 || pos >= pt->rows) {
        if (!seq.pattern_only) {
            if (jump_ord >= 0) seq.ord = (uint8_t)(jump_ord % MAX(1, seq.song_len));
            else seq.ord = (uint8_t)((seq.ord + 1) % MAX(1, seq.song_len));
            pt = &seq_pat[seq.order[seq.ord] % SEQ_PATTERNS];
            seq.edit_pat = seq.order[seq.ord];
        }
        pos = break_row >= 0 ? MIN(break_row, pt->rows - 1) : 0;
        jump_ord = break_row = -1;
    }
    seq.pos = (int16_t)pos;
    for (int c = 0; c < SEQ_TRACKS; c++) {
        struct chan *k = &ch[c];
        const struct seq_cell *cell = &pt->cell[pos][c];
        k->slide = 0; k->arp_x = k->arp_y = 0; k->vib_depth = 0; k->vol_slide = 0; k->cut = k->delay = k->retrig = -1;
        if (cell->fx != 0x3) k->glide_speed = 0;
        if (cell->fx == 0xE && (cell->param >> 4) == 0xD && (cell->param & 15)) {   /* EDx: the whole cell, later */
            k->pending = *cell; k->delay = (int8_t)(cell->param & 15);
            continue;
        }
        trigger(c, cell);
        effect(c, cell);
        update(c, 0);
    }
    next_tick = 1;
}

static void tick(void) {
    int t = next_tick;
    for (int c = 0; c < SEQ_TRACKS; c++) {
        struct chan *k = &ch[c];
        if (k->delay == t) { trigger(c, &k->pending); effect(c, &k->pending); k->delay = -1; }
        if (!k->on) continue;
        if (k->cut == t) { chan_off(c); continue; }
        if (k->retrig > 0 && t % k->retrig == 0) {
            chan_off(c); k->on = true;
            synth_note_on_pan(k->note, (uint8_t)MAX(1, k->vol), k->preset, TAG_SEQ | c, k->pan);
            midi_out_note(c, k->note, (uint8_t)MAX(1, k->vol));
            k->sent_bend = 0; k->sent_vol = k->vol;
        }
        k->bend = CLAMP(k->bend + k->slide, -96 * 256, 96 * 256);
        if (k->glide_speed) {
            if (k->bend < k->glide_to) k->bend = MIN(k->bend + k->glide_speed, k->glide_to);
            else if (k->bend > k->glide_to) k->bend = MAX(k->bend - k->glide_speed, k->glide_to);
        }
        if (k->vib_depth) k->vib_phase = (uint8_t)(k->vib_phase + k->vib_speed * 4);
        if (k->vol_slide) k->vol = (uint8_t)CLAMP((int)k->vol + k->vol_slide, 0, 127);
        update(c, t);
    }
    next_tick++;
}

/* ---- the clock: a row, then SEQ_TICKS - 1 ticks spread through it (the last tick lands exactly on the row) ---- */
static uint32_t tick_at(int t) { return row_q16 - (uint32_t)(((uint64_t)row_q16 * (uint32_t)t) / SEQ_TICKS); }   /* row_left when tick t is due */

void seq_run_events(void) {
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    if (bpm_q16 != row_bpm_q16 || seq.lpb != row_lpb) {         /* tempo changed: a new row length */
        row_bpm_q16 = bpm_q16; row_lpb = seq.lpb ? seq.lpb : 4;
        row_q16 = (uint32_t)(((uint64_t)rate * 60 << 32) / ((uint64_t)row_bpm_q16 * row_lpb));
    }
    for (int c = 0; c < SEQ_TRACKS; c++)
        if ((audition_on >> c & 1) && audition_left[c] == 0) { synth_note_off_tag(TAG_SEQ | c); audition_on &= (uint8_t)~(1u << c); }
    if (!seq.playing) return;
    if (row_left_q16 < 65536) { row(); rows_done++; row_left_q16 += row_q16; return; }
    if (next_tick < SEQ_TICKS && row_left_q16 < tick_at(next_tick) + 65536) tick();
}

uint32_t seq_step_left_q16(void) { return seq.playing ? row_left_q16 : 0; }
uint32_t seq_next_event(void) {
    uint32_t n = 0xFFFFFFFFu;
    if (seq.playing) {
        n = row_left_q16 >> 16;
        if (next_tick < SEQ_TICKS) { uint32_t t = tick_at(next_tick); n = row_left_q16 > t ? MIN(n, (row_left_q16 - t) >> 16) : 0; }
    }
    for (int c = 0; c < SEQ_TRACKS; c++) if ((audition_on >> c & 1) && audition_left[c] < n) n = audition_left[c];
    return n;
}

void seq_advance(uint32_t frames) {
    for (int c = 0; c < SEQ_TRACKS; c++) if (audition_on >> c & 1) audition_left[c] = audition_left[c] > frames ? audition_left[c] - frames : 0;
    if (seq.playing) row_left_q16 = row_left_q16 > (frames << 16) ? row_left_q16 - (frames << 16) : 0;
}

void seq_audition(int c, uint8_t note, uint8_t inst) {
    uint32_t st = plat_irq_save();
    synth_note_off_tag(TAG_SEQ | c);
    synth_note_on(note, DEFAULT_VEL, (uint8_t)preset_of(c, inst), TAG_SEQ | c);
    audition_left[c] = rate * 3 / 10; audition_on |= (uint8_t)(1u << c);          /* 300 ms */
    plat_irq_restore(st);
}

void seq_mute(int c, bool mute) {
    seq.ch[c].mute = mute;
    if (mute) chan_off(c);
}
