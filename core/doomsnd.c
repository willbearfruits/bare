/* Doom's sound (core/doomhost.h): its effects mixed from the WAD's own 8-bit samples, and its music — MUS, or a MIDI file
   — played by the synth's voices: each MIDI program gets the nearest of BARE!'s sounds (the distorted guitars are
   GRIND), the drums KICK, SNARE and HAT. Both sound on the DOOM bus, the mixer's DOOM channel. The engine calls in
   from the main loop; the effects are mixed and the song's events played from the audio interrupt. */
#include "doomhost.h"
#include "synth.h"
#include "platform.h"
#include "libc.h"

static volatile bool paused, held;             /* Doom left (everything waits); the game paused (the song waits) */

/* ---- effects: DMX samples, unsigned 8-bit, 11025 Hz mostly; interpolated up to the output's rate ---- */
struct sfx { const uint8_t *pcm; uint32_t len, idx, frac, step; int32_t gl, gr; volatile bool on; };
static struct sfx fx[DOOM_SFX_CHANNELS];

static void gains(struct sfx *c, int vol, int sep) {
    vol = CLAMP(vol, 0, 127); sep = CLAMP(sep, 0, 254);
    c->gl = (254 - sep) * vol / 127; c->gr = sep * vol / 127;     /* as Chocolate Doom pans: 128 is half on each side */
}
void doomsnd_sfx_start(int ch, const uint8_t *pcm, uint32_t len, uint32_t rate, int vol, int sep) {
    if (ch < 0 || ch >= DOOM_SFX_CHANNELS || !pcm || len < 2 || !rate) return;
    struct sfx *c = &fx[ch];
    uint32_t st = plat_irq_save();
    c->pcm = pcm; c->len = len; c->idx = 0; c->frac = 0;
    c->step = (uint32_t)(((uint64_t)rate << 16) / synth_rate());
    gains(c, vol, sep);
    c->on = true;
    plat_irq_restore(st);
}
void doomsnd_sfx_update(int ch, int vol, int sep) {
    if (ch < 0 || ch >= DOOM_SFX_CHANNELS) return;
    uint32_t st = plat_irq_save(); gains(&fx[ch], vol, sep); plat_irq_restore(st);
}
void doomsnd_sfx_stop(int ch) { if (ch >= 0 && ch < DOOM_SFX_CHANNELS) fx[ch].on = false; }
bool doomsnd_sfx_playing(int ch) { return ch >= 0 && ch < DOOM_SFX_CHANNELS && fx[ch].on; }

bool doomsnd_render(int32_t *l, int32_t *r, uint32_t n, bool add) {
    if (paused) return add;
    bool any = add;
    for (int ch = 0; ch < DOOM_SFX_CHANNELS; ch++) {
        struct sfx *c = &fx[ch];
        if (!c->on) continue;
        if (!any) { memset(l, 0, n * sizeof *l); memset(r, 0, n * sizeof *r); any = true; }
        const uint8_t *p = c->pcm; uint32_t idx = c->idx, frac = c->frac, step = c->step; int32_t gl = c->gl, gr = c->gr;
        for (uint32_t i = 0; i < n; i++) {
            if (idx + 1 >= c->len) { c->on = false; break; }
            int32_t a = p[idx] - 128, b = p[idx + 1] - 128;
            int32_t s = (a << 8) + (((b - a) * (int32_t)frac) >> 8);
            l[i] += (s * gl) >> 8; r[i] += (s * gr) >> 8;
            frac += step; idx += frac >> 16; frac &= 0xFFFF;
        }
        c->idx = idx; c->frac = frac;
    }
    return any;
}

/* ---- music: a song is its events, MIDI-style, in tracks (MUS has one); the tracks are merged as it plays ---- */
struct mev { uint32_t tick; uint8_t st, a, b, c; };      /* st: a MIDI status; 0xFF a tempo (a b c: µs a beat); 0xFE a track's end */
#define TRACKS 32
static struct mev *ev; static uint32_t ev_cap, ev_n;
static struct { uint32_t first, n, next; } trk[TRACKS];
static int ntrk;
static uint32_t division;                                  /* MIDI: ticks a beat; MUS: 0 (140 ticks a second) */
static volatile bool playing, looping;
static uint64_t tick_q32, step_q32;                       /* where the song is, and ticks a frame, both << 32 */
static uint8_t prog[16], cvol[16], expr[16], cpan[16];
static bool sustain[16];
static int32_t bend[16];
static uint32_t on_bits[16][4], sus_bits[16][4];
static int music_vol = 64;

void doomsnd_set_memory(void *events, uint32_t bytes) { ev = events; ev_cap = bytes / sizeof(struct mev); ev_n = 0; ntrk = 0; }

/* GM programs to BARE!'s sounds, by family (also the tracker's import of a Doom song) */
uint8_t doom_gm_preset(int p) {
    if (p < 8) return P_PLUCK;                             /* pianos */
    if (p < 16) return P_BELL;                             /* chromatic percussion */
    if (p < 24) return P_ORGAN;
    if (p == 29 || p == 30) return P_GRIND;                /* overdriven, distorted guitar */
    if (p < 32) return p == 31 ? P_BELL : P_PLUCK;         /* guitars; harmonics */
    if (p < 40) return P_SAWBASS;
    if (p == 45 || p == 46) return P_PLUCK;                /* pizzicato, harp */
    if (p == 47) return P_KICK;                            /* timpani */
    if (p == 55) return P_GRIND;                           /* orchestra hit */
    if (p < 64) return P_FATSAW;                           /* strings, ensembles, brass */
    if (p < 72) return P_SQLEAD;                           /* reeds */
    if (p < 80) return P_ORGAN;                            /* pipes */
    if (p < 88) return p == 81 ? P_FATSAW : P_SQLEAD;      /* synth leads */
    if (p < 96) return P_FATSAW;                           /* pads */
    if (p < 104) return P_BELL;                            /* synth effects */
    if (p < 112) return P_PLUCK;                           /* ethnic */
    if (p < 115) return P_BELL;
    if (p < 119) return p == 115 ? P_SNARE : P_KICK;       /* woodblock; taiko, toms */
    return P_HAT;                                          /* reverse cymbal, sound effects */
}
static void drum(uint8_t note, uint8_t *preset, uint8_t *snote) {
    switch (note) {
    case 35: case 36: *preset = P_KICK; *snote = 36; return;
    case 41: case 43: case 45: case 47: case 48: case 50: *preset = P_KICK; *snote = (uint8_t)(note + 4); return;   /* toms */
    case 37: case 38: case 39: case 40: *preset = P_SNARE; *snote = 38; return;
    default: *preset = P_HAT; *snote = 42; return;
    }
}
static inline uint16_t tag_of(int ch, int note) { return (uint16_t)(DOOM_TAG | ch << 7 | note); }
static inline bool bit(const uint32_t *m, int n) { return m[n >> 5] >> (n & 31) & 1; }
static inline void setb(uint32_t *m, int n, bool on) { if (on) m[n >> 5] |= 1u << (n & 31); else m[n >> 5] &= ~(1u << (n & 31)); }

static void note_off(int ch, int note) {
    if (!bit(on_bits[ch], note)) return;
    setb(on_bits[ch], note, false);
    if (sustain[ch]) setb(sus_bits[ch], note, true); else synth_note_off_tag(tag_of(ch, note));
}
static void note_on(int ch, int note, int vel) {
    uint8_t preset = doom_gm_preset(prog[ch]), snote = (uint8_t)note;
    if (ch == 9) drum((uint8_t)note, &preset, &snote);
    int32_t v = vel * cvol[ch] / 127 * expr[ch] / 127 * music_vol / 127;
    if (v < 1) return;
    uint16_t tag = tag_of(ch, note);
    synth_note_on_pan(snote, (uint8_t)MIN(v, 127), preset, tag, (cpan[ch] - 64) * 100 / 64);
    if (bend[ch] != 8192 && ch != 9) synth_tag_bend(tag, (bend[ch] - 8192) / 16);
    setb(on_bits[ch], note, true); setb(sus_bits[ch], note, false);
}
static void channel_off(int ch) {
    for (int k = 0; k < 128; k++) if (bit(on_bits[ch], k) || bit(sus_bits[ch], k)) synth_note_off_tag(tag_of(ch, k));
    memset(on_bits[ch], 0, sizeof on_bits[ch]); memset(sus_bits[ch], 0, sizeof sus_bits[ch]);
}
static void notes_off(void) { for (int ch = 0; ch < 16; ch++) channel_off(ch); }
static void set_tempo(uint32_t us) {
    if (!us) us = 500000;
    step_q32 = division ? ((uint64_t)division << 32) / us * 1000000 / synth_rate() : ((uint64_t)140 << 32) / synth_rate();
}
static void reset(void) {
    for (int ch = 0; ch < 16; ch++) { prog[ch] = 0; cvol[ch] = 100; expr[ch] = 127; cpan[ch] = 64; bend[ch] = 8192; sustain[ch] = false; }
    for (int t = 0; t < ntrk; t++) trk[t].next = trk[t].first;
    tick_q32 = 0;
    set_tempo(500000);
}

static void event(const struct mev *e) {
    int ch = e->st & 15;
    switch (e->st & 0xF0) {
    case 0x90: if (e->b) note_on(ch, e->a, e->b); else note_off(ch, e->a); return;
    case 0x80: note_off(ch, e->a); return;
    case 0xC0: prog[ch] = e->a & 127; return;
    case 0xE0:
        bend[ch] = e->a | e->b << 7;
        if (ch != 9) for (int k = 0; k < 128; k++) if (bit(on_bits[ch], k)) synth_tag_bend(tag_of(ch, k), (bend[ch] - 8192) / 16);
        return;
    case 0xB0:
        switch (e->a) {
        case 7:  cvol[ch] = e->b; return;
        case 10: cpan[ch] = e->b; return;
        case 11: expr[ch] = e->b; return;
        case 64:
            sustain[ch] = e->b >= 64;
            if (!sustain[ch]) for (int k = 0; k < 128; k++) if (bit(sus_bits[ch], k)) { setb(sus_bits[ch], k, false); synth_note_off_tag(tag_of(ch, k)); }
            return;
        case 120: case 123: channel_off(ch); return;
        case 121: expr[ch] = 127; bend[ch] = 8192; sustain[ch] = false; return;
        }
        return;
    case 0xF0:
        if (e->st == 0xFF) set_tempo((uint32_t)e->a << 16 | (uint32_t)e->b << 8 | e->c);
        return;
    }
}

void doomsnd_block(uint32_t n) {
    if (!playing || paused || held) return;
    uint32_t now = (uint32_t)(tick_q32 >> 32);
    for (int guard = 0; guard < 4096; guard++) {
        int best = -1; uint32_t bt = ~0u;
        for (int t = 0; t < ntrk; t++)
            if (trk[t].next < trk[t].first + trk[t].n && ev[trk[t].next].tick < bt) { bt = ev[trk[t].next].tick; best = t; }
        if (best < 0) {                                        /* the song is over: again, or silence */
            notes_off();
            if (looping && now > 0) { reset(); now = 0; continue; }
            playing = false; return;
        }
        if (bt > now) break;
        event(&ev[trk[best].next++]);
    }
    tick_q32 += step_q32 * n;
}

/* ---- reading songs: MUS or a MIDI file, event by event into a sink (st 0xFD: a track begins) ---- */
static uint32_t vlq(const uint8_t *d, uint32_t len, uint32_t *p) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && *p < len; i++) { uint8_t b = d[(*p)++]; v = v << 7 | (b & 127); if (!(b & 128)) break; }
    return v;
}

/* MUS: Doom's own score format (140 ticks a second, 16 channels, the 16th the drums) */
static bool load_mus(const uint8_t *d, uint32_t len, doom_song_sink add, void *ctx, uint32_t *div) {
    if (len < 16) return false;
    uint32_t start = d[6] | d[7] << 8, p = start, tick = 0;
    uint8_t note_vel[16];
    memset(note_vel, 127, sizeof note_vel);
    static const uint8_t cc[10] = { 0, 0, 1, 7, 10, 11, 91, 93, 64, 67 };
    static const uint8_t sys[5] = { 120, 123, 126, 127, 121 };
    *div = 0;
    if (!add(ctx, 0, 0xFD, 0, 0, 0)) return false;
    while (p < len) {
        uint8_t e = d[p++], type = e >> 4 & 7, mch = e & 15, ch = mch == 15 ? 9 : mch >= 9 ? mch + 1 : mch;
        bool ok = true;
        switch (type) {
        case 0: if (p >= len) return false; ok = add(ctx, tick, 0x80 | ch, d[p++] & 127, 0, 0); break;
        case 1: {
            if (p >= len) return false;
            uint8_t k = d[p++];
            if (k & 128) { if (p >= len) return false; note_vel[mch] = d[p++] & 127; }
            ok = add(ctx, tick, 0x90 | ch, k & 127, note_vel[mch], 0); break; }
        case 2: { if (p >= len) return false; uint32_t b = d[p++] * 64u; ok = add(ctx, tick, 0xE0 | ch, b & 127, (uint8_t)(b >> 7), 0); break; }
        case 3: { if (p >= len) return false; uint8_t c = d[p++]; if (c >= 10 && c <= 14) ok = add(ctx, tick, 0xB0 | ch, sys[c - 10], 0, 0); break; }
        case 4: {
            if (p + 1 >= len) return false;
            uint8_t c = d[p++], v = d[p++]; if (v > 127) v = 127;
            if (c == 0) ok = add(ctx, tick, 0xC0 | ch, v, 0, 0); else if (c < 10) ok = add(ctx, tick, 0xB0 | ch, cc[c], v, 0);
            break; }
        case 5: break;                                         /* the end of a bar */
        case 6: ok = add(ctx, tick, 0xFE, 0, 0, 0); p = len; break;   /* the end of the score */
        default: p++; break;
        }
        if (!ok) return false;
        if (e & 128 && p < len) tick += vlq(d, len, &p);
    }
    return true;
}

/* a standard MIDI file, format 0 or 1 */
static bool load_midi(const uint8_t *d, uint32_t len, doom_song_sink add, void *ctx, uint32_t *div) {
    if (len < 14 || memcmp(d, "MThd", 4)) return false;
    uint32_t hlen = (uint32_t)d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7], tracks = d[10] << 8 | d[11];
    *div = d[12] << 8 | d[13];
    if (!*div || *div & 0x8000) return false;                  /* SMPTE time: not in Doom's music */
    uint32_t p = 8 + hlen;
    for (uint32_t t = 0; t < tracks && p + 8 <= len; t++) {
        uint32_t tl = (uint32_t)d[p + 4] << 24 | d[p + 5] << 16 | d[p + 6] << 8 | d[p + 7];
        bool is_track = memcmp(d + p, "MTrk", 4) == 0;
        p += 8;
        uint32_t end = tl > len - p ? len : p + tl;
        if (!is_track) { p = end; continue; }
        if (!add(ctx, 0, 0xFD, 0, 0, 0)) return false;
        uint32_t tick = 0; uint8_t run = 0;
        while (p < end) {
            tick += vlq(d, end, &p);
            if (p >= end) break;
            uint8_t st = d[p];
            if (st & 128) p++; else if (run) st = run; else break;
            if (st == 0xFF) {
                if (p >= end) break;
                uint8_t type = d[p++]; uint32_t l = vlq(d, end, &p);
                if (type == 0x51 && l == 3 && p + 3 <= end && !add(ctx, tick, 0xFF, d[p], d[p + 1], d[p + 2])) return false;
                if (type == 0x2F) { if (!add(ctx, tick, 0xFE, 0, 0, 0)) return false; p = end; break; }
                p += l; continue;
            }
            if (st == 0xF0 || st == 0xF7) { uint32_t l = vlq(d, end, &p); p += l; run = 0; continue; }
            run = st;
            uint8_t kind = st & 0xF0, a = p < end ? d[p] & 127 : 0, b = 0;
            if (kind == 0xC0 || kind == 0xD0) p++; else { b = p + 1 < end ? d[p + 1] & 127 : 0; p += 2; }
            if (kind != 0xA0 && kind != 0xD0 && !add(ctx, tick, st, a, b, 0)) return false;
        }
        p = end;
    }
    return true;
}

bool doom_song_read(const uint8_t *data, uint32_t len, doom_song_sink sink, void *ctx, uint32_t *div) {
    if (!data) return false;
    return len >= 4 && !memcmp(data, "MUS\x1a", 4) ? load_mus(data, len, sink, ctx, div) : load_midi(data, len, sink, ctx, div);
}

/* the player's sink: each track's events in a row of ev[], at most TRACKS tracks (a track with nothing is dropped) */
static int open_trk = -1;
static void close_track(void) {
    if (open_trk < 0) return;
    trk[open_trk].n = ev_n - trk[open_trk].first;
    if (trk[open_trk].n) ntrk++;
    open_trk = -1;
}
static bool add(void *ctx, uint32_t tick, uint8_t st, uint8_t a, uint8_t b, uint8_t c) {
    (void)ctx;
    if (st == 0xFD) { close_track(); if (ntrk < TRACKS) { open_trk = ntrk; trk[ntrk].first = ev_n; } return true; }
    if (open_trk < 0) return true;                           /* past the tracks it keeps */
    if (ev_n >= ev_cap) return false;
    ev[ev_n++] = (struct mev){ tick, st, a, b, c };
    return true;
}

bool doomsnd_music_load(const uint8_t *data, uint32_t len) {
    doomsnd_music_stop();
    if (!ev || !data) return false;
    ntrk = 0; ev_n = 0; open_trk = -1;
    bool ok = doom_song_read(data, len, add, 0, &division);
    close_track();
    if (!ok || !ntrk) { ntrk = 0; ev_n = 0; return false; }
    return true;
}
void doomsnd_music_play(bool loop) {
    if (!ntrk) return;
    uint32_t st = plat_irq_save();
    notes_off(); reset(); looping = loop; playing = true;
    plat_irq_restore(st);
}
void doomsnd_music_stop(void) {
    uint32_t st = plat_irq_save();
    playing = false; notes_off();
    plat_irq_restore(st);
}
void doomsnd_music_volume(int v) { music_vol = CLAMP(v, 0, 127); }
bool doomsnd_music_playing(void) { return playing; }

void doomsnd_pause(bool p) {
    uint32_t st = plat_irq_save();
    paused = p;
    if (p) notes_off();
    plat_irq_restore(st);
}
void doomsnd_music_hold(bool h) {
    uint32_t st = plat_irq_save();
    held = h;
    if (h) notes_off();
    plat_irq_restore(st);
}
void doomsnd_all_off(void) {
    doomsnd_music_stop();
    for (int ch = 0; ch < DOOM_SFX_CHANNELS; ch++) fx[ch].on = false;
    paused = held = false;
}
