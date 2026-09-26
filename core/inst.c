/* Instruments from text files; see inst.h and INSTRUMENTS.md. The reader goes line by line: "key: value" (or "="),
   [sections], # or ; comments; every mistake is noted with its line and the rest still read, values clamped into
   range. The playing side (inst_block, from the audio loop) turns the notes held into voices: one each (tags 0xA00 |
   slot), one gliding voice for a strip or a mono instrument (0xA40), or an arpeggio on the tempo (0xA50). Just tuning
   moves each note on the semitone grid to its ratio from the tonic as it is played (inst_note, inst_strip); a toggle
   instrument's keys open and close notes; the bellows' air leaks out as it plays and sets the voices' level. */
#include "inst.h"
#include "disk.h"
#include "fat.h"
#include "seq.h"
#include "sieve.h"
#include "platform.h"
#include "libc.h"
#include "log.h"

struct inst insts[INSTS];
int inst_count;

const char *const inst_knob_names[IK_COUNT] = { "wave", "width", "detune", "attack", "decay", "sustain", "release", "cutoff",
                                                "resonance", "envelope", "vibrato", "level", "glide", "rate", "octaves" };
const char *const inst_scale_names[SC_COUNT] = { "chromatic", "major", "minor", "dorian", "phrygian", "lydian", "mixolydian",
                                                 "pentatonic", "minorpenta", "blues", "wholetone", "harmonic", "S1", "S2", "S3", "S4" };
static const uint16_t scale_mask[SC_S1] = {                /* the scale's notes in an octave, a bit each from C */
    0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x295, 0x4A9, 0x4E9, 0x555, 0x9AD };
#define WAVES 23
const char *const inst_wave_names[WAVES] = { "pulse", "square", "saw", "tri", "sine", "noise", "drawn", "morph", "scan",
    "gendy1", "gendy2", "gendy3", "gendy4", "fm1", "fm2", "fm3", "fm4", "sample1", "sample2", "sample3", "sample4", "sample5",
    "sample6" };                                            /* (samples 7 and 8 by number too: "sample7", "sample8" below) */
static const char *const more_waves[2] = { "sample7", "sample8" };

/* ---- reading ---- */
static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static bool same(const char *a, int n, const char *b) {    /* a[0..n) is b, any case */
    int i = 0; for (; i < n && b[i]; i++) if (lc(a[i]) != lc(b[i])) return false;
    return i == n && !b[i];
}
static int word(const char **p, const char *end) {         /* the next word's length, *p at its start */
    while (*p < end && (**p == ' ' || **p == '\t' || **p == ',')) (*p)++;
    int n = 0; while (*p + n < end && (*p)[n] != ' ' && (*p)[n] != '\t' && (*p)[n] != ',') n++;
    return n;
}
static int number(const char *s, int n, bool *ok) {       /* digits, a sign; "ms" or "%" after is fine */
    int i = 0, v = 0, sg = 1;
    if (i < n && (s[i] == '-' || s[i] == '+')) { if (s[i] == '-') sg = -1; i++; }
    if (i >= n || s[i] < '0' || s[i] > '9') { *ok = false; return 0; }
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0'), v = MIN(v, 100000);
    *ok = i == n || same(s + i, n - i, "ms") || same(s + i, n - i, "%") || same(s + i, n - i, "s");
    return sg * v;
}
static int note(const char *s, int n) {                    /* "C3", "F#2", "Bb4", or a MIDI number: 0..127, else -1 */
    bool ok; int v = number(s, n, &ok);
    if (ok) return v >= 0 && v <= 127 ? v : -1;
    static const int8_t pc[7] = { 9, 11, 0, 2, 4, 5, 7 };
    int c = lc(s[0]); if (n < 2 || c < 'a' || c > 'g') return -1;
    int k = pc[c - 'a'], i = 1;
    if (s[i] == '#') { k++; i++; } else if (s[i] == 'b' && i + 1 < n) { k--; i++; }
    int oct = number(s + i, n - i, &ok);
    if (!ok || oct < -1 || oct > 9) return -1;
    int m = (oct + 1) * 12 + k;
    return m >= 0 && m <= 127 ? m : -1;
}
static int wave_index(const char *s, int n) {
    for (int i = 0; i < WAVES; i++) if (same(s, n, inst_wave_names[i])) return i;
    for (int i = 0; i < 2; i++) if (same(s, n, more_waves[i])) return WAVES + i;
    if (same(s, n, "triangle")) return 3;
    return -1;
}
static int yes(const char *s, int n) {
    if (same(s, n, "yes") || same(s, n, "on") || same(s, n, "true") || same(s, n, "1")) return 1;
    if (same(s, n, "no") || same(s, n, "off") || same(s, n, "false") || same(s, n, "0")) return 0;
    return -1;
}

static void wave_to_sound(struct inst *in) {               /* the wave's name into the preset's wave, width and source */
    int w = in->wave; struct preset *p = &in->snd;
    p->src = 0;
    if (w <= 1) { p->wave = WAVE_PULSE; if (w == 1) p->duty = 50; }
    else if (w == 2) p->wave = WAVE_SAW;
    else if (w == 3) p->wave = WAVE_TRI;
    else if (w == 4) p->wave = WAVE_SINE;
    else if (w == 5) p->wave = WAVE_NOISE;
    else if (w <= 8) { p->wave = WAVE_TABLE; p->src = (uint8_t)(w == 6 ? SRC_SLOT : w == 7 ? SRC_MORPH : SRC_SCAN); }
    else if (w <= 12) { p->wave = WAVE_GENDY; p->src = (uint8_t)(w - 9); }
    else if (w <= 16) { p->wave = WAVE_FM; p->src = (uint8_t)(w - 13); }
    else { p->wave = WAVE_SAMPLE; p->src = (uint8_t)(w - 17); }
}

static void defaults(struct inst *in) {
    memset(in, 0, sizeof *in);
    in->wave = 0;
    in->snd = (struct preset){ 0, WAVE_PULSE, 50, 5, 200, 150, 70, 70, 127, 0, 0, 0, 0, 0, 0, false, 0 };
    in->keys = KEYS_CHROMATIC; in->keys_root = 48; in->scale = SC_CHROMATIC; in->steps = STEPS_SEMI;
    in->rate = 4; in->octaves = 1;
    static const uint8_t k[5] = { IK_CUTOFF, IK_RESO, IK_ATTACK, IK_RELEASE, IK_LEVEL };
    memcpy(in->knob, k, sizeof k); in->nknobs = 5;
}

static void mistake(struct inst *in, int line, const char *what, const char *s, int n) {
    if (!in->errors++) {
        char w[24]; int m = MIN(n, 20); memcpy(w, s, (size_t)m); w[m] = 0;
        snfmt(in->err, sizeof in->err, "line %d: %s%s%s%s", line, what, m ? " '" : "", w, m ? "'" : "");
    }
}

bool inst_parse(const char *text, int len, struct inst *in) {
    defaults(in);
    enum { S_TOP, S_SOUND, S_PLAY, S_KNOBS } sec = S_TOP;
    int line = 0; bool keys_said = false, tonic_said = false;
    for (int at = 0; at < len; ) {
        int e = at; while (e < len && text[e] != '\n') e++;
        const char *s = text + at, *end = text + e;
        at = e + 1; line++;
        /* a comment runs from # or ; at the start or after a space (F#2 is a note) */
        for (const char *c = s; c < end; c++) if ((*c == '#' || *c == ';') && (c == s || c[-1] == ' ' || c[-1] == '\t')) { end = c; break; }
        while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
        while (s < end && (*s == ' ' || *s == '\t')) s++;
        if (s == end) continue;
        if (*s == '[') {
            const char *c = s + 1; int n = 0; while (c + n < end && c[n] != ']') n++;
            if (same(c, n, "sound")) sec = S_SOUND; else if (same(c, n, "play")) sec = S_PLAY; else if (same(c, n, "knobs")) sec = S_KNOBS;
            else mistake(in, line, "no section called", c, n);
            continue;
        }
        const char *k = s; int kn = 0; while (k + kn < end && k[kn] != ':' && k[kn] != '=') kn++;
        if (k + kn >= end) { mistake(in, line, "a line without ':' —", s, (int)(end - s)); continue; }
        const char *v = k + kn + 1; while (kn && (k[kn - 1] == ' ' || k[kn - 1] == '\t')) kn--;
        while (v < end && (*v == ' ' || *v == '\t')) v++;
        int vn = (int)(end - v); bool ok = true;
        const char *w = v; int wn = word(&w, end);
        if (same(k, kn, "name")) { int m = 0; for (int i = 0; i < vn && m < 8; i++) if (v[i] > ' ' && v[i] < 0x7F) in->name[m++] = (char)(v[i] >= 'a' && v[i] <= 'z' ? v[i] - 32 : v[i]); in->name[m] = 0; continue; }
        if (same(k, kn, "by")) { int m = MIN(vn, (int)sizeof in->by - 1); memcpy(in->by, v, (size_t)m); in->by[m] = 0; continue; }
        if (same(k, kn, "about")) { int m = MIN(vn, (int)sizeof in->about - 1); memcpy(in->about, v, (size_t)m); in->about[m] = 0; continue; }
        struct preset *p = &in->snd;
        if (sec == S_SOUND) {
            int x = number(w, wn, &ok);
            if (same(k, kn, "wave")) { int i = wave_index(w, wn); if (i < 0) mistake(in, line, "no wave called", w, wn); else in->wave = (uint8_t)i; }
            else if (!ok) mistake(in, line, "a number, not", w, wn);
            else if (same(k, kn, "width")) p->duty = (uint8_t)CLAMP(x, 5, 95);
            else if (same(k, kn, "detune")) p->detune = (uint8_t)CLAMP(x, 0, 100);
            else if (same(k, kn, "attack")) p->attack_ms = (uint16_t)CLAMP(x, 1, 4000);
            else if (same(k, kn, "decay")) p->decay_ms = (uint16_t)CLAMP(x, 1, 8000);
            else if (same(k, kn, "sustain")) p->sustain = (uint8_t)CLAMP(x, 0, 100);
            else if (same(k, kn, "release")) p->release_ms = (uint16_t)CLAMP(x, 1, 8000);
            else if (same(k, kn, "cutoff")) p->cutoff = (uint8_t)CLAMP(x, 0, 127);
            else if (same(k, kn, "resonance") || same(k, kn, "reso")) p->reso = (uint8_t)CLAMP(x, 0, 100);
            else if (same(k, kn, "envelope")) p->fenv = (uint8_t)CLAMP(x, 0, 100);
            else if (same(k, kn, "vibrato")) p->vib = (uint8_t)CLAMP(x, 0, 100);
            else if (same(k, kn, "level")) p->volume = (uint8_t)CLAMP(x, 0, 100);
            else mistake(in, line, "no sound setting called", k, kn);
        } else if (sec == S_PLAY) {
            if (same(k, kn, "strip")) {
                const char *w2 = w + wn; int n2 = word(&w2, end);
                int a = note(w, wn), b = note(w2, n2);
                if (same(w, wn, "off")) in->strip_hi = 0;
                else if (a < 0 || b <= a) mistake(in, line, "a strip is two notes, low then high:", v, vn);
                else { in->strip_lo = (uint8_t)a; in->strip_hi = (uint8_t)b; }
            } else if (same(k, kn, "steps")) {
                if (same(w, wn, "free")) in->steps = STEPS_FREE; else if (same(w, wn, "semitones")) in->steps = STEPS_SEMI;
                else if (same(w, wn, "scale")) in->steps = STEPS_SCALE; else mistake(in, line, "steps are free, semitones or scale, not", w, wn);
            } else if (same(k, kn, "scale")) {
                int i = 0; while (i < SC_COUNT && !same(w, wn, inst_scale_names[i])) i++;
                if (i == SC_COUNT) mistake(in, line, "no scale called", w, wn); else in->scale = (uint8_t)i;
            } else if (same(k, kn, "keys")) {
                const char *w2 = w + wn; int n2 = word(&w2, end), r = n2 ? note(w2, n2) : 48;
                keys_said = true;
                if (same(w, wn, "off")) in->keys = KEYS_OFF;
                else if ((same(w, wn, "chromatic") || same(w, wn, "scale")) && r >= 0) { in->keys = same(w, wn, "scale") ? KEYS_SCALE : KEYS_CHROMATIC; in->keys_root = (uint8_t)r; }
                else mistake(in, line, "keys are off, chromatic NOTE or scale NOTE, not", v, vn);
            } else if (same(k, kn, "pads")) {
                if (same(w, wn, "off")) { in->pads_w = 0; continue; }
                int c = 0, r = 0, i = 0; bool okp = true;
                while (i < wn && w[i] >= '0' && w[i] <= '9') c = c * 10 + (w[i++] - '0');
                if (i < wn && lc(w[i]) == 'x') { i++; while (i < wn && w[i] >= '0' && w[i] <= '9') r = r * 10 + (w[i++] - '0'); } else okp = false;
                const char *w2 = w + wn; int n2 = word(&w2, end), root = n2 ? note(w2, n2) : 48;
                if (!okp || i != wn || c < 1 || c > 8 || r < 1 || r > 8 || root < 0) mistake(in, line, "pads are COLUMNSxROWS NOTE (up to 8x8), not", v, vn);
                else { in->pads_w = (uint8_t)c; in->pads_h = (uint8_t)r; in->pads_root = (uint8_t)root; if (!keys_said) in->keys = KEYS_OFF; }
            } else if (same(k, kn, "hold") && same(w, wn, "toggle")) {
                in->hold = HOLD_TOGGLE;
            } else if (same(k, kn, "mono") || same(k, kn, "hold") || same(k, kn, "bellows")) {
                int y = yes(w, wn);
                if (y < 0) mistake(in, line, same(k, kn, "hold") ? "yes, no or toggle, not" : "yes or no, not", w, wn);
                else if (same(k, kn, "mono")) in->mono = y; else if (same(k, kn, "bellows")) in->bellows = y; else in->hold = (uint8_t)y;
            } else if (same(k, kn, "tuning")) {
                const char *w2 = w + wn; int n2 = word(&w2, end), t = n2 ? note(w2, n2) : -1;
                if (same(w, wn, "equal")) in->tuning = TUNING_EQUAL;
                else if (same(w, wn, "just") && (!n2 || t >= 0)) { in->tuning = TUNING_JUST; if (n2) { in->tonic = (uint8_t)(t % 12); tonic_said = true; } }
                else mistake(in, line, "tuning is equal, or just (and a tonic), not", v, vn);
            } else if (same(k, kn, "arp")) {
                static const char *const a[5] = { "off", "up", "down", "updown", "random" };
                int i = 0; while (i < 5 && !same(w, wn, a[i])) i++;
                if (i == 5) mistake(in, line, "arp is off, up, down, updown or random, not", w, wn); else in->arp = (uint8_t)i;
            } else if (same(k, kn, "glide") || same(k, kn, "rate") || same(k, kn, "octaves")) {
                int x = number(w, wn, &ok);
                if (!ok) mistake(in, line, "a number, not", w, wn);
                else if (same(k, kn, "glide")) in->glide_ms = (uint16_t)CLAMP(x, 0, 2000);
                else if (same(k, kn, "rate")) in->rate = (uint8_t)CLAMP(x, 1, 8);
                else in->octaves = (uint8_t)CLAMP(x, 1, 4);
            } else mistake(in, line, "no play setting called", k, kn);
        } else if (sec == S_KNOBS && same(k, kn, "knobs")) {
            in->nknobs = 0;
            for (const char *c = v; ; ) {
                int n = word(&c, end); if (!n) break;
                int i = 0; while (i < IK_COUNT && !same(c, n, inst_knob_names[i])) i++;
                if (i == IK_COUNT) mistake(in, line, "no knob called", c, n);
                else if (in->nknobs < INST_KNOBS) in->knob[in->nknobs++] = (uint8_t)i;
                c += n;
            }
        } else mistake(in, line, sec == S_TOP ? "only name, by and about come before [sound]:" : "no setting called", k, kn);
    }
    wave_to_sound(in);
    if (in->strip_hi && in->keys == KEYS_CHROMATIC && !keys_said) in->keys = KEYS_OFF;   /* a strip instrument: its own surface */
    if (!tonic_said) in->tonic = (uint8_t)((in->keys != KEYS_OFF ? in->keys_root : in->pads_w ? in->pads_root : in->strip_lo) % 12);
    return in->name[0] != 0;
}

/* ---- the stick and the built-in ones ---- */
static void add(const char *file, const char *text, int len) {
    struct inst t;
    bool ok = inst_parse(text, len, &t);
    snfmt(t.file, sizeof t.file, "%s", file);
    if (!ok) { logf("instr: %s: no name, so no instrument%s%s", file, t.errors ? "; " : "", t.errors ? t.err : ""); return; }
    if (t.errors) logf("instr: %s: %d mistake%s, the first %s", file, t.errors, t.errors > 1 ? "s" : "", t.err);
    int i = 0; while (i < inst_count && memcmp(insts[i].name, t.name, sizeof t.name)) i++;
    if (i == inst_count && inst_count == INSTS) { logf("instr: %s: already %d instruments", file, INSTS); return; }
    insts[i] = t; insts[i].snd.name = insts[i].name;
    if (i == inst_count) inst_count++;
}
void inst_load_all(void) {
    inst_select(-1);
    inst_count = 0;
    for (int i = 0; i < inst_builtin_count; i++) add(inst_builtin[i].file, inst_builtin[i].text, (int)strlen(inst_builtin[i].text));
    if (disk.have_boot_fat) {
        static struct fat_entry e[16]; static char buf[8192];
        int n = fat_list(&disk.fat, "INSTR", "TXT", e, 16);
        for (int k = 0; k < n; k++) {
            uint32_t len = MIN(e[k].size, (uint32_t)sizeof buf - 1);
            if (fat_read(&disk.fat, &e[k].file, 0, buf, len)) { buf[len] = 0; add(e[k].name, buf, (int)len); }
        }
        if (n > 0) logf("instr: %d file%s in INSTR/", n, n > 1 ? "s" : "");
    }
    for (int i = 0; i < inst_count; i++) inst_apply(i);
    for (int i = inst_count; i < INSTS; i++) synth_user_preset(i, 0);
    logf("instr: %d instruments", inst_count);
}

/* ---- knobs ---- */
int inst_knob_get(const struct inst *in, int k) {
    const struct preset *p = &in->snd;
    switch (k) {
    case IK_WAVE: return in->wave; case IK_WIDTH: return p->duty; case IK_DETUNE: return p->detune;
    case IK_ATTACK: return p->attack_ms; case IK_DECAY: return p->decay_ms; case IK_SUSTAIN: return p->sustain;
    case IK_RELEASE: return p->release_ms; case IK_CUTOFF: return p->cutoff; case IK_RESO: return p->reso;
    case IK_ENVELOPE: return p->fenv; case IK_VIBRATO: return p->vib; case IK_LEVEL: return p->volume;
    case IK_GLIDE: return in->glide_ms; case IK_RATE: return in->rate; default: return in->octaves;
    }
}
void inst_knob_set(struct inst *in, int k, int v) {
    struct preset *p = &in->snd;
    switch (k) {
    case IK_WAVE: in->wave = (uint8_t)((v + WAVES + 2) % (WAVES + 2)); wave_to_sound(in); break;
    case IK_WIDTH: p->duty = (uint8_t)CLAMP(v, 5, 95); break; case IK_DETUNE: p->detune = (uint8_t)CLAMP(v, 0, 100); break;
    case IK_ATTACK: p->attack_ms = (uint16_t)CLAMP(v, 1, 4000); break; case IK_DECAY: p->decay_ms = (uint16_t)CLAMP(v, 1, 8000); break;
    case IK_SUSTAIN: p->sustain = (uint8_t)CLAMP(v, 0, 100); break; case IK_RELEASE: p->release_ms = (uint16_t)CLAMP(v, 1, 8000); break;
    case IK_CUTOFF: p->cutoff = (uint8_t)CLAMP(v, 0, 127); break; case IK_RESO: p->reso = (uint8_t)CLAMP(v, 0, 100); break;
    case IK_ENVELOPE: p->fenv = (uint8_t)CLAMP(v, 0, 100); break; case IK_VIBRATO: p->vib = (uint8_t)CLAMP(v, 0, 100); break;
    case IK_LEVEL: p->volume = (uint8_t)CLAMP(v, 0, 100); break; case IK_GLIDE: in->glide_ms = (uint16_t)CLAMP(v, 0, 2000); break;
    case IK_RATE: in->rate = (uint8_t)CLAMP(v, 1, 8); break; default: in->octaves = (uint8_t)CLAMP(v, 1, 4); break;
    }
}
void inst_knob_text(const struct inst *in, int k, char *out, int cap) {
    int v = inst_knob_get(in, k);
    switch (k) {
    case IK_WAVE: snfmt(out, cap, "%s", v < WAVES ? inst_wave_names[v] : more_waves[v - WAVES]); break;
    case IK_ATTACK: case IK_DECAY: case IK_RELEASE: case IK_GLIDE: snfmt(out, cap, "%d ms", v); break;
    case IK_WIDTH: case IK_SUSTAIN: case IK_LEVEL: snfmt(out, cap, "%d%%", v); break;
    case IK_CUTOFF: if (v >= 127) snfmt(out, cap, "open"); else snfmt(out, cap, "%d", v); break;
    case IK_RATE: snfmt(out, cap, "%d a beat", v); break;
    default: snfmt(out, cap, "%d", v);
    }
}
void inst_apply(int i) { if (i >= 0 && i < inst_count) synth_user_preset(i, &insts[i].snd); }

int inst_step(const struct inst *in, int root, int k) {    /* the scale's k-th note from root, in 1/256 semitones */
    if (in->scale >= SC_S1) {
        const struct sieve *s = &sieves[in->scale - SC_S1];
        int u = s->unit ? s->unit : 1; int32_t x = root * u;
        for (int i = 0; i <= k; i++) { int32_t m = sieve_next(s, x, 128 * u); if (m == SIEVE_NONE || m >= 128 * u) return -1; if (i == k) return m * 256 / u; x = m + 1; }
        return -1;
    }
    uint16_t mask = scale_mask[in->scale]; int n = root;
    for (int i = 0; ; n++) {                                /* up from the root, counting the scale's notes */
        if (n > 127) return -1;
        if (mask >> (n % 12) & 1) { if (i == k) return n * 256; i++; }
    }
}

/* ---- playing (main loop: what is held; audio side: what sounds) ---- */
#define HELD 16
#define TAG_POLY 0xA00
#define TAG_MONO 0xA40
#define TAG_ARP  0xA50
static volatile int cur = -1;
static struct { bool on; uint16_t id; int32_t pitch; uint8_t vel; uint32_t order; } held[HELD];
static struct { bool on; int32_t pitch; uint8_t vel; } strip;
static uint32_t order_n; static int keys_down; static bool latch, latch_new;
static bool sounding[HELD]; static int32_t sound_pitch[HELD]; static uint8_t sound_base[HELD];
static bool mono_on; static int32_t mono_pitch, mono_to, mono_step; static uint8_t mono_base; static int32_t mono_bent;
static bool arp_on; static int32_t arp_pitch; static uint8_t arp_base; static uint32_t arp_left, arp_gate, arp_len_q16, arp_frac_q16;
static volatile int32_t air;                                 /* the bellows: 0 .. 32767 */
static int32_t air_level = -1; static int air_blocks;        /* the level the voices were last given */
static uint32_t arp_bpm, arp_rate; static int arp_i = -1, arp_dir = 1; static uint32_t rng = 0x41525021u;

static void all_off(void) {
    for (int s = 0; s < HELD; s++) if (sounding[s]) { synth_note_off_tag((uint16_t)(TAG_POLY | s)); sounding[s] = false; }
    if (mono_on) { synth_note_off_tag(TAG_MONO); mono_on = false; }
    if (arp_on) { synth_note_off_tag(TAG_ARP); arp_on = false; }
    arp_i = -1;
}
void inst_select(int i) {
    uint32_t st = plat_irq_save();
    all_off();
    memset(held, 0, sizeof held); strip.on = false; keys_down = 0; latch_new = false;
    cur = i >= 0 && i < inst_count ? i : -1;
    latch = cur >= 0 && insts[cur].hold == HOLD_YES;
    air = 0; air_level = -1;
    plat_irq_restore(st);
}
void inst_clear(void) {
    uint32_t st = plat_irq_save();
    memset(held, 0, sizeof held); keys_down = 0; latch_new = false;
    plat_irq_restore(st);
}
void inst_pump(int amount) {
    uint32_t st = plat_irq_save();
    air = CLAMP(air + amount, 0, 32767);
    plat_irq_restore(st);
}
int inst_air(void) { return air; }

/* just intonation: the semitones from the tonic as 5-limit ratios (16/15, 9/8, 6/5, 5/4, 4/3, 45/32, 3/2, 8/5, 5/3,
   16/9, 15/8), their difference from equal temperament in 1/256 semitones */
static const int8_t just_q8[12] = { 0, 30, 10, 40, -35, -5, -25, 5, 35, -40, -10, -30 };
static int32_t tuned(int32_t p) {
    if (cur < 0 || insts[cur].tuning != TUNING_JUST || (p & 255)) return p;   /* off the semitone grid: as it is */
    int k = ((p >> 8) - insts[cur].tonic) % 12;
    return p + just_q8[k < 0 ? k + 12 : k];
}
int  inst_selected(void) { return cur; }
bool inst_latched(void) { return latch; }
void inst_latch(bool on) {
    uint32_t st = plat_irq_save();
    latch = on;
    if (!on) for (int s = 0; s < HELD; s++) if (held[s].on && held[s].id == 0xFFFF) held[s].on = false;   /* latched ones let go */
    plat_irq_restore(st);
}
void inst_note(int id, int32_t pitch, uint8_t vel, bool on) {
    uint32_t st = plat_irq_save();
    pitch = tuned(CLAMP(pitch, 0, 127 * 256));
    int s = -1;
    if (cur >= 0 && insts[cur].hold == HOLD_TOGGLE) {         /* a key opens its note; pressed again, closes it */
        if (on) {
            for (int i = 0; i < HELD; i++) if (held[i].on && held[i].pitch == pitch) s = i;
            if (s >= 0) held[s].on = false;
            else {
                for (int i = 0; i < HELD && s < 0; i++) if (!held[i].on) s = i;
                if (s < 0) { uint32_t least = 0xFFFFFFFFu; for (int i = 0; i < HELD; i++) if (held[i].order < least) { least = held[i].order; s = i; } }
                held[s].on = true; held[s].id = 0xFFFF; held[s].pitch = pitch; held[s].vel = vel; held[s].order = ++order_n;
            }
        }
        plat_irq_restore(st);
        return;
    }
    for (int i = 0; i < HELD; i++) if (held[i].on && held[i].id == (uint16_t)id) s = i;
    if (on) {
        if (latch && latch_new) { memset(held, 0, sizeof held); latch_new = false; s = -1; }   /* a new set after all were let go */
        if (s < 0) for (int i = 0; i < HELD && s < 0; i++) if (held[i].on && held[i].id == 0xFFFF && held[i].pitch == pitch) s = i;   /* latched: the same note again */
        if (s < 0) for (int i = 0; i < HELD && s < 0; i++) if (!held[i].on) s = i;
        if (s < 0) { uint32_t least = 0xFFFFFFFFu; for (int i = 0; i < HELD; i++) if (held[i].order < least) { least = held[i].order; s = i; } }
        held[s].on = true; held[s].id = (uint16_t)id; held[s].pitch = CLAMP(pitch, 0, 127 * 256); held[s].vel = vel; held[s].order = ++order_n;
        keys_down++;
    } else if (s >= 0) {
        if (latch) held[s].id = 0xFFFF; else held[s].on = false;             /* latched: it stays, no longer held by this key */
        if (keys_down > 0 && --keys_down == 0 && latch) latch_new = true;
    }
    plat_irq_restore(st);
}
void inst_strip(bool touching, int32_t pitch, uint8_t vel) {
    uint32_t st = plat_irq_save();
    strip.on = touching; if (touching) { strip.pitch = tuned(CLAMP(pitch, 0, 127 * 256)); strip.vel = vel; }
    plat_irq_restore(st);
}

static void voice_on(uint16_t tag, int32_t pitch, uint8_t vel, uint8_t preset, uint8_t *base) {
    int note = CLAMP((pitch + 128) >> 8, 0, 127);
    synth_note_on((uint8_t)note, vel, preset, tag);
    *base = (uint8_t)note;
    if (pitch != note * 256) synth_tag_bend(tag, pitch - note * 256);
}

/* The bellows: the air leaks out (to a third in about 2.7 s); the reeds speak from a little pressure up, louder and
   up to pitch as it rises (a few cents flat when it is low). Every other block, the sounding voices get the level. */
static void bellows(uint32_t n, bool started) {
    int32_t leak = (air * (int32_t)n) >> 17;
    air -= leak ? leak : air > 0;
    int32_t lvl = MIN(air < 2500 ? 0 : (air - 2500) * 32 / 945, 1024);   /* the reeds' level, 0 .. 1024 */
    if (!started && (lvl == air_level || ++air_blocks < 2)) return;
    air_blocks = 0; air_level = lvl;
    int32_t sag = (1024 - lvl) * 20 / 1024;                           /* up to 20/256 of a semitone flat, about 8 cents */
    for (int s = 0; s < HELD; s++) if (sounding[s]) {
        uint16_t tag = (uint16_t)(TAG_POLY | s);
        synth_tag_level(tag, (int32_t)held[s].vel * 258 * lvl >> 10);
        synth_tag_bend(tag, sound_pitch[s] - sound_base[s] * 256 - sag);
    }
}

void inst_block(uint32_t n) {
    int c = cur;
    if (c < 0) return;
    const struct inst *in = &insts[c];
    uint8_t preset = (uint8_t)(P_INST1 + c);
    uint32_t rate = synth_rate();
    if (in->arp) {                                          /* the arpeggio: the held notes, sorted, on the tempo's steps */
        int32_t list[HELD * 4]; int nl = 0;
        for (int s = 0; s < HELD; s++) if (held[s].on) {
            int32_t p = held[s].pitch; int j = nl;
            while (j > 0 && list[j - 1] > p) { list[j] = list[j - 1]; j--; }
            list[j] = p; nl++;
        }
        for (int o = 1, base_n = nl; o < in->octaves; o++) for (int j = 0; j < base_n && nl < HELD * 4; j++) list[nl++] = list[j] + o * 12 * 256;
        uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
        if (bpm_q16 != arp_bpm || in->rate != arp_rate) {  /* a step: a beat over rate */
            arp_bpm = bpm_q16; arp_rate = in->rate;
            arp_len_q16 = (uint32_t)(((uint64_t)rate * 60 << 32) / ((uint64_t)bpm_q16 * MAX(1u, in->rate)));
        }
        if (arp_on && arp_gate <= n) { synth_note_off_tag(TAG_ARP); arp_on = false; }
        else if (arp_on) arp_gate -= n;
        if (!nl) { arp_i = -1; arp_left = 0; return; }
        if (arp_left > n) { arp_left -= n; return; }
        int k;                                               /* the next note */
        switch (in->arp) {
        case ARP_UP: k = arp_i + 1 >= nl ? 0 : arp_i + 1; break;
        case ARP_DOWN: k = arp_i <= 0 || arp_i >= nl ? nl - 1 : arp_i - 1; break;
        case ARP_UPDOWN:
            if (nl == 1) k = 0;
            else { k = arp_i + arp_dir; if (k >= nl) { arp_dir = -1; k = nl - 2; } else if (k < 0) { arp_dir = 1; k = 1; } }
            break;
        default: rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; k = (int)(rng % (uint32_t)nl); break;
        }
        arp_i = k;
        if (arp_on) synth_note_off_tag(TAG_ARP);
        voice_on(TAG_ARP, list[k], 100, preset, &arp_base); arp_on = true; arp_pitch = list[k];
        arp_frac_q16 += arp_len_q16;                         /* the steps' fractions carried, so the beat holds */
        arp_left = arp_frac_q16 >> 16; arp_frac_q16 &= 0xFFFF;
        arp_gate = arp_left * 3 / 5;
        return;
    }
    if (arp_on) { synth_note_off_tag(TAG_ARP); arp_on = false; }
    /* one gliding voice: the strip, or (mono) the newest note held */
    int32_t target = -1; uint8_t vel = 100;
    if (strip.on) { target = strip.pitch; vel = strip.vel; }
    else if (in->mono) { uint32_t best = 0; for (int s = 0; s < HELD; s++) if (held[s].on && held[s].order >= best) { best = held[s].order; target = held[s].pitch; vel = held[s].vel; } }
    if (target < 0) { if (mono_on) { synth_note_off_tag(TAG_MONO); mono_on = false; } }
    else if (!mono_on) { voice_on(TAG_MONO, target, vel, preset, &mono_base); mono_on = true; mono_pitch = mono_to = target; mono_bent = target - mono_base * 256; }
    else if (target != mono_to) {
        bool free_strip = strip.on && in->steps == STEPS_FREE;
        if (in->glide_ms || free_strip) {                    /* glide there (a free strip follows the finger, smoothed) */
            uint32_t frames = MAX(1u, (uint32_t)MAX(in->glide_ms, 12) * (rate / 1000));
            mono_to = target; mono_step = (mono_to - mono_pitch) * (int32_t)SYNTH_BLOCK / (int32_t)MIN(frames, 1000000u) ;
            if (!mono_step) mono_step = mono_to > mono_pitch ? 1 : -1;
        } else {                                              /* a new note: struck again, as a stylus on a new key */
            synth_note_off_tag(TAG_MONO);
            voice_on(TAG_MONO, target, vel, preset, &mono_base); mono_pitch = mono_to = target; mono_bent = target - mono_base * 256;
        }
    }
    if (mono_on && mono_pitch != mono_to) {
        int32_t d = mono_step * (int32_t)n / SYNTH_BLOCK; if (!d) d = mono_step > 0 ? 1 : -1;
        mono_pitch += d;
        if ((d > 0 && mono_pitch > mono_to) || (d < 0 && mono_pitch < mono_to)) mono_pitch = mono_to;
        int32_t b = CLAMP(mono_pitch - mono_base * 256, -96 * 256, 96 * 256);
        if (b != mono_bent) { synth_tag_bend(TAG_MONO, b); mono_bent = b; }
    }
    /* one voice a held note (not mono) */
    bool started = false;
    for (int s = 0; s < HELD; s++) {
        bool want = !in->mono && held[s].on;
        uint16_t tag = (uint16_t)(TAG_POLY | s);
        if (want && !sounding[s]) { voice_on(tag, held[s].pitch, held[s].vel, preset, &sound_base[s]); sounding[s] = true; sound_pitch[s] = held[s].pitch; started = true; }
        else if (!want && sounding[s]) { synth_note_off_tag(tag); sounding[s] = false; }
        else if (want && held[s].pitch != sound_pitch[s]) { synth_tag_bend(tag, held[s].pitch - sound_base[s] * 256); sound_pitch[s] = held[s].pitch; }
    }
    if (in->bellows) bellows(n, started);
}

int inst_sounding(int32_t *out, int max) {
    int n = 0;
    uint32_t st = plat_irq_save();
    for (int s = 0; s < HELD && n < max; s++) if (sounding[s]) out[n++] = sound_pitch[s];
    if (mono_on && n < max) out[n++] = mono_pitch;
    if (arp_on && n < max) out[n++] = arp_pitch;
    plat_irq_restore(st);
    return n;
}
