/* Doom's music into the tracker, as breakcore (see doomtrack.h). The song is read by Doom's own reader
   (doom_song_read), paired into notes, fitted to a 16th-note grid and written into patterns 1…; then pattern 0 is an
   intro break, one pattern a breakdown half way, one the tape stop, and every pattern gets its drums. */
#include "doomtrack.h"
#include "doomhost.h"
#include "seq.h"
#include "synth.h"
#include "fat.h"
#include "disk.h"
#include "log.h"
#include "libc.h"

#define SONG_MAX  (96 * 1024)                 /* a music lump: E1M1's is 17 KB */
#define NOTES_MAX 4096
#define BPM       172
#define FIRST     1                           /* the song's patterns start here: 0 is the intro */
enum { GTR, GTR2, GTRB, BASS, KICK, SNARE, HATS, JUNK };   /* the tracker's channels */

static uint8_t song[SONG_MAX];
struct note { uint32_t start, end; uint8_t ch, key, vel, pad; };
static struct note nt[NOTES_MAX]; static int nn;
static int16_t open_at[16][128];              /* the note sounding on a channel and key, -1 none */
static uint8_t program[16]; static bool prog_seen[16];
static uint32_t song_end;
static int rows_max;
static uint32_t rng;

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- the song's notes: each note-on paired with its off ---- */
static bool sink(void *ctx, uint32_t tick, uint8_t st, uint8_t a, uint8_t b, uint8_t c) {
    (void)ctx; (void)c;
    if (st == 0xFD) { memset(open_at, 0xFF, sizeof open_at); return true; }   /* a track begins: nothing open */
    if (st >= 0xF0) return true;
    if (tick > song_end) song_end = tick;
    uint8_t kind = st & 0xF0, ch = st & 15;
    if (kind == 0xC0) { if (!prog_seen[ch]) { program[ch] = a; prog_seen[ch] = true; } return true; }
    if (kind == 0x90 && b) {
        if (open_at[ch][a] >= 0) nt[open_at[ch][a]].end = tick;             /* struck again: the last one ends */
        if (nn >= NOTES_MAX) return true;                                  /* a very long song: the rest left out */
        nt[nn] = (struct note){ tick, 0xFFFFFFFFu, ch, a, b, 0 };
        open_at[ch][a] = (int16_t)nn++;
    } else if ((kind == 0x80 || kind == 0x90) && open_at[ch][a] >= 0) {
        nt[open_at[ch][a]].end = tick; open_at[ch][a] = -1;
    }
    return true;
}

/* ticks a 16th, × 100. A MIDI file says (a beat's ticks); MUS doesn't (140 ticks a second): the commonest gap between
   a channel's notes (12 to 40 ticks), refined to the grid the note-ons fit best */
static uint32_t grid100(uint32_t division) {
    if (division) return division * 25;
    uint16_t hist[41] = { 0 }; uint32_t last[16];
    for (int c = 0; c < 16; c++) last[c] = 0xFFFFFFFFu;
    for (int i = 0; i < nn; i++) {
        if (nt[i].ch == 9) continue;
        uint32_t l = last[nt[i].ch];
        if (l != 0xFFFFFFFFu && nt[i].start - l >= 12 && nt[i].start - l <= 40) hist[nt[i].start - l]++;
        last[nt[i].ch] = nt[i].start;
    }
    int peak = 19;
    for (int d = 12; d <= 40; d++) if (hist[d] > hist[peak]) peak = d;
    uint64_t best = ~0ull; uint32_t bg = (uint32_t)peak * 100;
    for (uint32_t g = (uint32_t)peak * 100 - 150; g <= (uint32_t)peak * 100 + 150; g++) {
        uint64_t err = 0;
        for (int i = 0; i < nn && err < best; i++) {
            if (nt[i].ch == 9) continue;
            uint32_t r = nt[i].start * 100 % g, d = MIN(r, g - r);
            err += (uint64_t)d * d;
        }
        if (err < best) { best = err; bg = g; }
    }
    return bg;
}
static uint32_t pos16(uint32_t tick, uint32_t g100) { return (uint32_t)(((uint64_t)tick * 1600 + g100 / 2) / g100); }   /* 16ths of a row */

/* ---- writing cells ---- */
static struct seq_cell *song_cell(int row, int c) { return &seq_pat[FIRST + row / SEQ_ROWS].cell[row % SEQ_ROWS][c]; }
static void put_note(int c, uint32_t p16, int key, int vel) {
    int row = (int)(p16 >> 4), sub = (int)(p16 & 15);
    if (sub > 12) { row++; sub = 0; }
    if (row >= rows_max) return;
    struct seq_cell *x = song_cell(row, c);
    if (x->note && x->note != NOTE_OFF) return;                              /* two on one row: the first stays */
    x->note = (uint8_t)CLAMP(key, 1, 127); x->vol = (uint8_t)(CLAMP(vel * 7 / 8, 1, 127) + 1); x->inst = 0;
    if (sub >= 3) { x->fx = 0xE; x->param = (uint8_t)(0xD0 | sub); } else { x->fx = 0; x->param = 0; }   /* between rows: EDx */
}
static void put_off(int c, uint32_t end16, uint32_t start16) {
    int row = (int)((end16 + 8) >> 4);
    row = MAX(row, (int)(start16 >> 4) + 1);
    if (row >= rows_max) return;
    struct seq_cell *x = song_cell(row, c);
    if (!x->note) x->note = NOTE_OFF;                                        /* never over a note */
}
static void hit(int p, int row, int c, uint8_t preset, int note, int vel, uint8_t fx, uint8_t param) {
    struct seq_cell *x = &seq_pat[p].cell[row][c];
    x->note = (uint8_t)note; x->inst = (uint8_t)(preset + 1); x->vol = (uint8_t)(CLAMP(vel, 1, 127) + 1); x->fx = fx; x->param = param;
}
static void fx_if_free(struct seq_cell *x, uint8_t fx, uint8_t param) { if (x->note && x->note != NOTE_OFF && !x->fx && !x->param) { x->fx = fx; x->param = param; } }

/* ---- the breaks: this file's own ---- */
static const char *const kicks[4]  = { "K.K.......K.....", "K.....K...K..K..", "K..K......K.K...", "K.K...K.K.....K." };
static const char *const snares[4] = { "....S..s.s..S..s", "....S.s...s.S.ss", "..s.S..s.S..S.s.", "....S...s.s.S..S" };
static const char *const hatp[3]   = { "H.H.H.H.H.H.H.H.", "HhHhHhHhHhHhHhHh", "H.hOH.hOH.hOH.hO" };
enum { BAR_BREAK, BAR_HALF, BAR_BUILD, BAR_END };

/* a bar of drums at a pattern's row; `bar` counts through the whole arrangement (fills every 4th, bigger every 8th) */
static void drums(int p, int row0, int bar, int kind) {
    const char *kp = kicks[rnd() % 4], *sp = snares[rnd() % 4], *hp = hatp[rnd() % 3];
    bool fill = bar % 4 == 3, big = bar % 8 == 7, chop = !fill && rnd() % 5 == 0;
    if (kind == BAR_HALF) { kp = "K.........K....."; sp = "........S......."; hp = hatp[0]; chop = false; }
    if (kind == BAR_BUILD) { kp = "K.......K.......";  sp = "S.S.S.S.RRRRRRRR"; hp = "................"; fill = big = chop = false; }
    if (kind == BAR_END) { kp = "K.......K......."; sp = "....S.......RRRR"; hp = hatp[0]; fill = big = chop = false; }
    for (int i = 0; i < 16; i++) {
        int r = row0 + i, j = chop && i >= 8 ? (i - 8) % 4 : i;          /* chopped: the first beat again, twice */
        char k = kp[j], s = sp[j], h = hp[j];
        if (fill && i >= 12) { k = '.'; s = 'R'; h = '.'; }
        if (big && i >= 8) { k = i == 8 ? 'K' : '.'; s = 'R'; h = i == 8 ? 'O' : '.'; }
        if (k == 'K') hit(p, r, KICK, i == 0 && bar % 2 == 0 ? P_KICK808 : P_KICK, 36, 120, 0, 0);
        if (s == 'S') hit(p, r, SNARE, P_SNARE, 48, 112, 0, 0);
        else if (s == 's') hit(p, r, SNARE, P_SNARE, 48, 42, 0, 0);
        else if (s == 'R') {                                               /* a roll: retriggered faster, louder */
            int n = kind == BAR_BUILD ? i - 8 : i - 12;
            hit(p, r, SNARE, P_SNARE, 48, 64 + n * (kind == BAR_BUILD ? 8 : 14), 0xE, (uint8_t)(0x90 | (i >= 14 ? 1 : i >= 12 ? 2 : 3)));
        }
        if (h == 'H') hit(p, r, HATS, P_HAT, 60, 70, 0, 0);
        else if (h == 'h') hit(p, r, HATS, P_HAT, 60, 38, 0, 0);
        else if (h == 'O') hit(p, r, HATS, P_OHAT, 60, 84, 0, 0);
        if (chop && i == 12) fx_if_free(&seq_pat[p].cell[r][SNARE], 0xE, 0x93);   /* the chop stutters */
    }
    if (kind == BAR_BUILD) return;
    if (bar % 4 == 0) hit(p, row0, JUNK, P_METAL, 40 + (int)(rnd() % 12), 112, 0, 0);   /* junk on each phrase */
    if (bar % 8 == 0) hit(p, row0, HATS, P_CRASH, 60, 100, 0, 0);
    if (bar % 2 == 1 && kind == BAR_BREAK) hit(p, row0 + 12, JUNK, P_CLAP, 48, 84, 0, 0);
    if (big) { static const uint8_t tom[4] = { P_TOMHI, P_TOMHI, P_TOMLO, P_FTOM }; static const uint8_t tn[4] = { 50, 50, 45, 41 };
               for (int i = 0; i < 4; i++) hit(p, row0 + 12 + i, JUNK, tom[i], tn[i], 100, 0, 0); }
}

/* the guitars and bass of a stretch of the song copied into another pattern (the breakdown, the tape stop) */
static void copy_rows(int dst, int drow, int srow, int n) {
    for (int i = 0; i < n; i++) for (int c = GTR; c <= BASS; c++) seq_pat[dst].cell[drow + i][c] = *song_cell(srow + i, c);
}

bool doomtrack_arrange(const uint8_t *data, uint32_t len, const char *title, char *msg, int msg_len) {
    nn = 0; song_end = 0; memset(open_at, 0xFF, sizeof open_at); memset(program, 0, sizeof program); memset(prog_seen, 0, sizeof prog_seen);
    uint32_t division = 0;
    if (!doom_song_read(data, len, sink, 0, &division) || !nn) { snfmt(msg, (size_t)msg_len, "not a song it can read"); return false; }
    for (int i = 0; i < nn; i++) if (nt[i].end == 0xFFFFFFFFu || nt[i].end < nt[i].start) nt[i].end = MAX(song_end, nt[i].start);
    for (int i = 1; i < nn; i++) {                                         /* in order of time (tracks come one by one) */
        struct note x = nt[i]; int j = i - 1;
        while (j >= 0 && nt[j].start > x.start) { nt[j + 1] = nt[j]; j--; }
        nt[j + 1] = x;
    }
    /* who plays what: the bass (a bass program, else the lowest line), the busiest other line on two channels, the next */
    int count[16] = { 0 }; int32_t sum[16] = { 0 };
    for (int i = 0; i < nn; i++) if (nt[i].ch != 9) { count[nt[i].ch]++; sum[nt[i].ch] += nt[i].key; }
    int bass = -1, g1 = -1, g2 = -1;
    for (int c = 0; c < 16; c++) if (count[c] && prog_seen[c] && program[c] >= 32 && program[c] < 40 && (bass < 0 || count[c] > count[bass])) bass = c;
    if (bass < 0) for (int c = 0; c < 16; c++) if (count[c] && sum[c] / count[c] < 52 && (bass < 0 || sum[c] / count[c] < sum[bass] / count[bass])) bass = c;
    for (int c = 0; c < 16; c++) if (c != bass && count[c] && (g1 < 0 || count[c] > count[g1])) g1 = c;
    for (int c = 0; c < 16; c++) if (c != bass && c != g1 && count[c] && (g2 < 0 || count[c] > count[g2])) g2 = c;
    if (g1 < 0) { g1 = bass; bass = -1; }
    if (g1 < 0) { snfmt(msg, (size_t)msg_len, "the song has no notes but drums"); return false; }
    uint32_t g = grid100(division);

    /* the tracker, stopped and cleared; the song's notes into patterns FIRST … */
    seq_begin(title, BPM);
    rows_max = (SEQ_PATTERNS - FIRST - 2) * SEQ_ROWS;
    uint32_t end16[4] = { 0 };
    for (int i = 0; i < nn; i++) {
        const struct note *x = &nt[i];
        int c = x->ch == g1 ? GTR : x->ch == g2 ? GTRB : x->ch == bass ? BASS : -1;
        if (c < 0) continue;                                               /* the drums, and lines past three */
        uint32_t s = pos16(x->start, g), e = pos16(x->end, g);
        if (c == GTR && s + 8 < end16[GTR]) c = GTR2;                      /* the first still sounding: a chord's note */
        put_note(c, s, x->key, x->vel);
        put_off(c, e, s);
        end16[c] = e;
    }
    int song_rows = (int)((pos16(song_end, g) + 15) >> 4);
    song_rows = CLAMP((song_rows + 15) / 16 * 16, 16, rows_max);           /* whole bars */
    int npat = (song_rows + SEQ_ROWS - 1) / SEQ_ROWS, bars = song_rows / 16;
    seq_pat[FIRST + npat - 1].rows = (uint8_t)(song_rows - (npat - 1) * SEQ_ROWS);
    int bd = FIRST + npat, out = bd + 1;                                   /* the breakdown and the tape stop */

    /* the channels */
    static const char *const names[8] = { "GTR", "GTR2", "GTR B", "BASS", "KICK", "SNARE", "HATS", "JUNK" };
    uint8_t sounds[8] = { doom_gm_preset(program[g1]), doom_gm_preset(program[g1]), g2 >= 0 ? doom_gm_preset(program[g2]) : P_GRIND,
                          bass >= 0 ? doom_gm_preset(program[bass]) : P_SAWBASS, P_KICK, P_SNARE, P_HAT, P_METAL };
    for (int c = 0; c < SEQ_TRACKS; c++) { snfmt(seq.ch[c].name, sizeof seq.ch[c].name, "%s", names[c]); seq.ch[c].inst = (uint8_t)(sounds[c] + 1); seq.ch[c].mute = false; }

    /* the order: the intro break, the first half, the breakdown, the second half, the tape stop */
    int half = npat / 2, k = 0;
    seq.order[k++] = 0;
    for (int p = 0; p < half; p++) seq.order[k++] = (uint8_t)(FIRST + p);
    seq.order[k++] = (uint8_t)bd;
    for (int p = half; p < npat; p++) seq.order[k++] = (uint8_t)(FIRST + p);
    seq.order[k++] = (uint8_t)out;
    seq.song_len = (uint8_t)k;

    /* the breakdown: the song's first bar twice, stuttering; its second bar twice, gated; the bass sliding down */
    copy_rows(bd, 0, 0, 16); copy_rows(bd, 16, 0, 16); copy_rows(bd, 32, 16, 16); copy_rows(bd, 48, 16, 16);
    for (int r = 0; r < 64; r++) {
        if (r < 32 && r % 8 == 4) fx_if_free(&seq_pat[bd].cell[r][GTR], 0xE, 0x93);
        if (r >= 32) { fx_if_free(&seq_pat[bd].cell[r][GTR], 0xE, 0xC2); fx_if_free(&seq_pat[bd].cell[r][GTR2], 0xE, 0xC2); }
        if (r >= 56) fx_if_free(&seq_pat[bd].cell[r][BASS], 0x2, 0x18);
    }
    /* the tape stop: the first bar again, every line sliding down faster and faster; then all of it cut off */
    seq_pat[out].rows = 32;
    copy_rows(out, 0, 0, 16);
    for (int r = 0; r < 16; r++) for (int c = GTR; c <= BASS; c++) {
        struct seq_cell *x = &seq_pat[out].cell[r][c];
        if (x->fx == 0xE && (x->param & 0xF0) == 0xD0) x->fx = x->param = 0;   /* no waiting: it slides */
        fx_if_free(x, 0x2, (uint8_t)(0x06 + r * 5));
    }
    for (int c = GTR; c <= BASS; c++) seq_pat[out].cell[16][c] = (struct seq_cell){ NOTE_OFF, 0, 0, 0, 0 };

    /* the melody's glitches in the song itself: the last beat of every 8th bar stutters, one bar in four is gated */
    rng = 0x45314D31u;                                                     /* the same song every time */
    for (int b = 0; b < bars; b++) {
        bool gate = rnd() % 4 == 0;
        for (int i = 0; i < 16; i++) {
            int r = b * 16 + i;
            if (b % 8 == 7 && i >= 12) fx_if_free(song_cell(r, GTR), 0xE, (uint8_t)(0x90 | (i >= 14 ? 2 : 3)));
            if (gate) { fx_if_free(song_cell(r, GTR), 0xE, 0xC6); fx_if_free(song_cell(r, GTR2), 0xE, 0xC6); }   /* half a row: a gate */
            if (b % 8 == 7 && i == 12) fx_if_free(song_cell(r, BASS), 0x2, 0x10);
        }
    }
    /* drums everywhere, bar by bar in the order they play */
    int bar = 0;
    for (int b = 0; b < 4; b++, bar++) drums(0, b * 16, bar, b == 3 ? BAR_BUILD : BAR_BREAK);   /* the intro: the break alone */
    hit(0, 0, JUNK, P_METAL, 45, 120, 0, 0);
    for (int o = 1; o < seq.song_len; o++) {
        int p = seq.order[o], nb = seq_pat[p].rows / 16;
        for (int b = 0; b < nb; b++, bar++) {
            int kind = p == bd ? (b == 3 ? BAR_BREAK : BAR_HALF) : p == out ? (b == 0 ? BAR_END : -1) : BAR_BREAK;
            if (kind >= 0) drums(p, b * 16, bar, kind);
        }
    }
    hit(FIRST, 0, HATS, P_CRASH, 60, 110, 0, 0);                           /* the song comes in on a crash */
    hit(out, 16, HATS, P_CRASH, 60, 110, 0, 0); hit(out, 16, KICK, P_KICK808, 36, 120, 0, 0); hit(out, 16, JUNK, P_METAL, 36, 127, 0, 0);

    int n_g = 0; for (int i = 0; i < nn; i++) n_g += nt[i].ch != 9;
    snfmt(msg, (size_t)msg_len, "%s: %d bars of the WAD's song as breakcore, %d BPM", title, bars, BPM);
    logf("doomtrack: %s, %d notes (%d drums left out), grid %u.%02u ticks a 16th, %d bars, patterns %d, lines g1 %d g2 %d bass %d",
         title, nn, nn - n_g, g / 100, g % 100, bars, npat, g1, g2, bass);
    return true;
}

/* ---- from the stick: the IWAD's music lump ---- */
static int32_t read_lump(const char *wad, const char *name, uint8_t *buf, uint32_t cap) {
    struct fat_file fi; uint8_t h[12], e[64 * 16];
    if (!fat_find(&disk.fat, wad, &fi) || fi.size < 12 || !fat_read(&disk.fat, &fi, 0, h, 12)) return -1;
    uint32_t n = rd32(h + 4), dir = rd32(h + 8), nl = (uint32_t)strlen(name);
    if (n > 65536 || dir >= fi.size || (uint64_t)dir + n * 16ull > fi.size) return -1;
    for (uint32_t i = 0; i < n; i += 64) {
        uint32_t k = MIN(64u, n - i);
        if (!fat_read(&disk.fat, &fi, dir + i * 16, e, k * 16)) return -1;
        for (uint32_t j = 0; j < k; j++) {
            const uint8_t *x = e + j * 16;
            if (memcmp(x + 8, name, nl) || (nl < 8 && x[8 + nl])) continue;
            uint32_t pos = rd32(x), size = rd32(x + 4);
            if (!size || size > cap || (uint64_t)pos + size > fi.size) return -1;
            return fat_read(&disk.fat, &fi, pos, buf, size) ? (int32_t)size : -1;
        }
    }
    return -1;
}

bool doomtrack_load(char *msg, int msg_len) {
    char wad[48]; uint32_t size;
    if (!doom_wad_path(wad, sizeof wad, &size)) { snfmt(msg, (size_t)msg_len, "no Doom WAD on the stick (put one in its DOOM folder)"); return false; }
    const char *title = "E1M1 BREAKCORE";
    int32_t len = read_lump(wad, "D_E1M1", song, sizeof song);
    if (len < 0) { title = "MAP01 BREAKCORE"; len = read_lump(wad, "D_RUNNIN", song, sizeof song); }
    if (len < 0) { snfmt(msg, (size_t)msg_len, "%s has no E1M1 or MAP01 music", wad); return false; }
    logf("doomtrack: %s from %s (%d bytes)", title, wad, (int)len);
    return doomtrack_arrange(song, (uint32_t)len, title, msg, msg_len);
}
