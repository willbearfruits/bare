#include "song.h"
#include "seq.h"
#include "tape.h"
#include "rhythm.h"
#include "sampler.h"
#include "synth.h"
#include "audio.h"
#include "disk.h"
#include "libc.h"
#include "platform.h"
#include "log.h"
#include "undo.h"

struct song_io song;
#define CHUNK_BYTES 32768                               /* a write: what one BIOS call carries */
static uint8_t io[CHUNK_BYTES];
static struct fat_file file; static struct fat_cursor cur;
static bool tape_loop, header_done;

static bool tracker_has_notes(void) {
    for (int o = 0; o < seq.song_len; o++) {
        const struct seq_pattern *p = &seq_pat[seq.order[o] % SEQ_PATTERNS];
        for (int r = 0; r < p->rows; r++) for (int c = 0; c < SEQ_TRACKS; c++) if (p->cell[r][c].note) return true;
    }
    return false;
}

uint32_t song_length(void) {
    uint32_t rate = audio_rate(), n = 0;
    if (tracker_has_notes()) {                          /* one pass of the order list at the tempo (Fxx changes aside) */
        uint32_t row = (uint32_t)((uint64_t)rate * 60 / ((uint32_t)(seq.bpm ? seq.bpm : 120) * (seq.lpb ? seq.lpb : 4)));
        for (int o = 0; o < seq.song_len; o++) n += seq_pat[seq.order[o] % SEQ_PATTERNS].rows * row;
    }
    n = MAX(n, tape.used);
    return n ? n + rate * 2 : 0;                        /* and two seconds for the echoes and the reverb to fade */
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static void wav_header(uint8_t *h, uint32_t frames) {
    uint32_t rate = audio_rate(), data = frames * 4;
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + data); memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 16);
    put16(h + 20, 1); put16(h + 22, 2); put32(h + 24, rate); put32(h + 28, rate * 4); put16(h + 32, 4); put16(h + 34, 16);
    memcpy(h + 36, "data", 4); put32(h + 40, data);
}

static void say(const char *m) { snfmt(song.status, sizeof song.status, "%s", m); logf("song: %s", m); }

int song_wavs(struct fat_entry *out, int max) { return disk.have_boot_fat ? fat_list(&disk.fat, "", "WAV", out, max) : 0; }

bool song_export_start(void) {
    if (song.exporting) return false;
    if (!disk.have_boot_fat) { say("no stick to write to (boot from the stick, or plug it in and rescan)"); return false; }
    uint32_t total = song_length();
    if (!total) { say("nothing to export: the tracker and the tape are empty"); return false; }
    /* SONG0001.WAV, or the first number free */
    static struct fat_entry have[64]; int n = song_wavs(have, 64);
    for (int k = 1; k < 10000; k++) {
        snfmt(song.name, sizeof song.name, "SONG%04d.WAV", k);
        bool taken = false;
        for (int i = 0; i < n; i++) if (!memcmp(have[i].name, song.name, 13)) taken = true;
        if (!taken) break;
    }
    uint32_t bytes = 44 + total * 4;
    if (fat_free_bytes(&disk.fat) < bytes + 65536) { say("not enough room on the stick for the song"); return false; }
    if (!fat_create(&disk.fat, "", song.name, bytes, &file)) { say("could not create the file"); return false; }
    fat_rewind(&file, &cur);
    /* the engine plays the song from the top, into the file instead of the speakers */
    plat_audio_hold(true);
    synth_all_off(); rhythm_play(false);
    seq.ord = 0;
    if (tracker_has_notes()) seq_play(true); else seq_play(false);
    tape.recording = false; tape_loop = tape.loop; tape.loop = false;
    tape.pos = 0; tape.frac = 0; tape.playing = tape.used > 0;
    song.done = 0; song.total = total; header_done = false; song.exporting = true;
    snfmt(song.status, sizeof song.status, "exporting %s", song.name);
    return true;
}

static void finish(const char *m) {
    song.changes++;
    seq_play(false); tape.playing = false; tape.pos = 0; tape.loop = tape_loop;
    synth_all_off();
    plat_audio_hold(false);
    song.exporting = false;
    say(m);
}
void song_export_cancel(void) { if (song.exporting) finish("export stopped"); }

void song_work(void) {
    if (!song.exporting) return;
    uint32_t head = header_done ? 0 : 44;
    if (head) wav_header(io, song.total);
    uint32_t frames = MIN((CHUNK_BYTES - head) / 4, song.total - song.done);
    audio_render((int16_t *)(io + head), frames);
    uint32_t bytes = head + frames * 4;
    memset(io + bytes, 0, ((bytes + 511) & ~511u) - bytes);
    if (!fat_seq(&disk.fat, &cur, io, bytes, true)) { finish("the stick would not take the song (write failed)"); return; }
    header_done = true;
    song.done += frames;
    if (song.done >= song.total) {
        char m[64]; snfmt(m, sizeof m, "%s written, %u:%02u", song.name, song.total / audio_rate() / 60, song.total / audio_rate() % 60);
        finish(m);
    }
}

/* ---- import: a PCM WAV, decoded a byte at a time into frames, resampled, mixed to mono, handed on in pieces ---- */
struct wav { uint16_t ch, bits; uint32_t rate, off, len; };
static bool wav_parse(const struct fat_file *fi, struct wav *w) {
    uint8_t h[512];
    if (fi->size < 44 || !fat_read(&disk.fat, fi, 0, h, MIN(512u, fi->size))) return false;
    if (memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
    uint32_t at = 12; bool fmt = false;
    while (at + 8 <= fi->size) {
        uint8_t c[24];
        if (!fat_read(&disk.fat, fi, at, c, MIN(24u, fi->size - at))) return false;
        uint32_t sz = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4) && sz >= 16) {
            uint16_t format = (uint16_t)(c[8] | c[9] << 8);
            w->ch = (uint16_t)(c[10] | c[11] << 8); w->rate = c[12] | c[13] << 8 | c[14] << 16 | (uint32_t)c[15] << 24;
            w->bits = (uint16_t)(c[22] | c[23] << 8);
            fmt = (format == 1 || format == 0xFFFE) && w->ch >= 1 && w->ch <= 2 && (w->bits == 8 || w->bits == 16 || w->bits == 24) && w->rate >= 4000 && w->rate <= 192000;
            if (!fmt) return false;
        } else if (!memcmp(c, "data", 4)) {
            w->off = at + 8; w->len = MIN(sz, fi->size - w->off);
            return fmt;
        }
        at += 8 + sz + (sz & 1);
    }
    return false;
}

bool song_import(const struct fat_entry *e, bool to_tape, int idx) {
    struct wav w;
    if (!disk.have_boot_fat || !wav_parse(&e->file, &w)) { say("not a WAV this can read (PCM, 8/16/24 bit, mono or stereo)"); return false; }
    uint32_t out_rate = to_tape ? tape_rate() : sampler_rate_hz(samples[idx].rate_i), bps = w.bits / 8, fb = bps * w.ch;
    uint64_t step = ((uint64_t)w.rate << 16) / out_rate, next = 0;     /* input frames per output frame, Q16 */
    char name[12]; int k = 0; for (; k < 8 && e->name[k] && e->name[k] != '.'; k++) name[k] = e->name[k]; name[k] = 0;
    if (to_tape) {
        if (!tape.len) { say("no tape on this machine"); return false; }
        uint32_t frames = (uint32_t)((uint64_t)(w.len / fb) * out_rate / w.rate);
        undo_begin(U_TAPE, idx, "the import", plat_ms());
        for (uint32_t s = tape.pos >> TAPE_BLOCK_SHIFT; s <= (tape.pos + frames) >> TAPE_BLOCK_SHIFT && s < TAPE_SPANS; s++) undo_save(U_TAPE, idx << 12 | (int)s);
        undo_end();
    } else {
        undo_one(U_SAMPLE, idx, "the import", plat_ms());
        if (!sampler_begin_write(idx)) { say("no sample memory"); return false; }
    }
    static int16_t outbuf[1024]; uint32_t on = 0, written = 0, at = tape.pos;
    uint8_t frame[6]; uint32_t fpos = 0; int32_t prev = 0; uint64_t kin = 0; bool full = false;
    struct fat_cursor c; fat_rewind(&e->file, &c);
    for (uint32_t base = 0; base < w.off + w.len && !full; base += CHUNK_BYTES) {
        uint32_t n = MIN((uint32_t)CHUNK_BYTES, e->file.size - base);
        if (!fat_seq(&disk.fat, &c, io, n, false)) { say("the stick would not give the file (read failed)"); break; }
        for (uint32_t i = 0; i < n && !full; i++) {
            uint32_t pos = base + i;
            if (pos < w.off || pos >= w.off + w.len) continue;
            frame[fpos++] = io[i];
            if (fpos < fb) continue;
            fpos = 0;
            int32_t x = 0;
            for (uint32_t ch = 0; ch < w.ch; ch++) {                        /* each channel as 16 bit, then their mean */
                const uint8_t *s = frame + ch * bps;
                x += w.bits == 8 ? ((int32_t)s[0] - 128) * 256 : w.bits == 16 ? (int16_t)(s[0] | s[1] << 8) : (int16_t)(s[1] | s[2] << 8);
            }
            x /= (int32_t)w.ch;
            while (next <= kin << 16) {                                     /* the output frames up to this input frame */
                int32_t frac = kin ? (int32_t)(next - ((kin - 1) << 16)) : 0;
                outbuf[on++] = (int16_t)(kin ? prev + (int32_t)(((int64_t)(x - prev) * frac) >> 16) : x);
                next += step;
                if (on == 1024) {
                    bool ok = to_tape ? tape_write(idx, at + written, outbuf, on) : sampler_write(idx, outbuf, on);
                    written += on; on = 0;
                    if (!ok) { full = true; break; }
                }
            }
            prev = x; kin++;
        }
    }
    if (on && !full) { if (to_tape) tape_write(idx, at + written, outbuf, on); else sampler_write(idx, outbuf, on); written += on; }
    if (!to_tape) sampler_end_write(idx, name);
    char m[64]; uint32_t sec = written / out_rate;
    snfmt(m, sizeof m, "%s: %u:%02u.%u %s%s", e->name, sec / 60, sec % 60, written % out_rate * 10 / out_rate,
          to_tape ? "on the tape" : "in the sampler", full ? " (the rest did not fit)" : "");
    say(m);
    return written > 0;
}
