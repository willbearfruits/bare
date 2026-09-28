#include "midi.h"
#include "omni.h"
#include "seq.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

struct midi_state midi;
static uint32_t rate = 48000;

/* out: a ring the audio interrupt (tracker notes, clock) and the main loop fill, the main loop drains to the port */
#define OUT_RING 1024
static uint8_t out_ring[OUT_RING];
static volatile uint32_t out_w, out_r;
static void out_bytes(const uint8_t *b, int n) {
    uint32_t st = plat_irq_save();
    for (int i = 0; i < n && out_w - out_r < OUT_RING; i++) out_ring[out_w++ & (OUT_RING - 1)] = b[i];
    plat_irq_restore(st);
}

/* in: channel messages, parsed, waiting for the app */
#define MSGS 64
static struct midi_msg msgs[MSGS];
static uint32_t msg_w, msg_r;
static uint8_t run_status, data[2]; static int ndata; static bool in_sysex;

/* the clock: out, counted on the sample clock; in, the time of the last 25 clocks for the tempo */
static uint32_t clock_q16, clock_left_q16; static uint16_t clock_bpm;
static uint64_t clock_ms[25]; static uint32_t clock_n; static uint64_t last_clock;

void midi_init(uint32_t r) {
    rate = r ? r : 48000;
    memset(&midi, 0, sizeof midi);
    midi.port = -1;
    out_w = out_r = 0; msg_w = msg_r = 0; run_status = 0; ndata = 0; in_sysex = false; clock_n = 0;
}

bool midi_open(int port) {
    if (!plat_midi_open(port)) { logf("midi: port %d did not open", port); return false; }
    midi.port = (int8_t)(port < 0 ? -1 : port);
    run_status = 0; ndata = 0; in_sysex = false; clock_n = 0; midi.ext_bpm = 0;
    return true;
}

static const char *const note_names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static void monitor(const char *line) {
    memmove(midi.monitor[1], midi.monitor[0], sizeof midi.monitor - sizeof midi.monitor[0]);
    snfmt(midi.monitor[0], sizeof midi.monitor[0], "%s", line);
}

static void message(uint8_t st, uint8_t d1, uint8_t d2) {
    uint8_t ch = (uint8_t)((st & 15) + 1), type = st & 0xF0;
    char line[32];
    switch (type) {
    case 0x80: case 0x90: snfmt(line, sizeof line, "%2u %s %s%d %u", ch, type == 0x90 && d2 ? "ON " : "OFF", note_names[d1 % 12], d1 / 12 - 1, d2); break;
    case 0xB0: snfmt(line, sizeof line, "%2u CC  %u = %u", ch, d1, d2); break;
    case 0xC0: snfmt(line, sizeof line, "%2u PROGRAM %u", ch, d1 + 1); break;
    case 0xE0: snfmt(line, sizeof line, "%2u BEND %d", ch, (int)((d2 << 7) | d1) - 8192); break;
    default:   snfmt(line, sizeof line, "%2u %02x %02x %02x", ch, st, d1, d2); break;
    }
    monitor(line);
    midi.in_msgs++;
    if (midi.thru) { uint8_t b[3] = { st, d1, d2 }; out_bytes(b, (type == 0xC0 || type == 0xD0) ? 2 : 3); }
    if (midi.in_channel && ch != midi.in_channel) return;
    if (msg_w - msg_r < MSGS) msgs[msg_w++ % MSGS] = (struct midi_msg){ st, d1, d2 };
}

/* clock, start, continue, stop: they may come between any two bytes of anything else */
static void realtime(uint8_t b, uint64_t now) {
    if (!midi.clock_in) return;
    if (b == 0xF8) {
        clock_ms[clock_n % 25] = now; clock_n++; last_clock = now;
        if (clock_n >= 25) {                                   /* 24 clocks are a beat */
            uint64_t beat = now - clock_ms[(clock_n - 25) % 25];
            if (beat) { midi.ext_bpm = (uint16_t)CLAMP((60000 + beat / 2) / beat, 20, 300); seq.bpm = midi.ext_bpm; }
        }
    } else if (b == 0xFA || b == 0xFB) { if (b == 0xFA) seq.ord = 0; seq_play(true); monitor(b == 0xFA ? "   START" : "   CONTINUE"); }
    else if (b == 0xFC) { seq_play(false); monitor("   STOP"); }
}

static void byte_in(uint8_t b, uint64_t now) {
    if (b >= 0xF8) { realtime(b, now); return; }
    if (b == 0xF0) { in_sysex = true; return; }
    if (b == 0xF7) { in_sysex = false; return; }
    if (in_sysex && !(b & 0x80)) return;
    in_sysex = false;
    if (b >= 0xF0) { run_status = 0; return; }                 /* system common: not for us, and no running status */
    if (b & 0x80) { run_status = b; ndata = 0; return; }
    if (!run_status) return;
    data[ndata++] = b;
    int need = (run_status & 0xE0) == 0xC0 ? 1 : 2;              /* program change and channel pressure have one */
    if (ndata < need) return;
    ndata = 0;
    message(run_status, data[0], need > 1 ? data[1] : 0);
}

bool midi_next(struct midi_msg *m) {
    if (msg_r == msg_w) return false;
    *m = msgs[msg_r++ % MSGS];
    return true;
}

void midi_work(uint64_t now) {
    if (midi.port < 0) return;
    uint8_t buf[64]; int n;
    while ((n = plat_midi_read(buf, sizeof buf)) > 0) for (int i = 0; i < n; i++) byte_in(buf[i], now);
    if (midi.ext_bpm && now - last_clock > 1000) midi.ext_bpm = 0;        /* the clock stopped coming */
    while (out_r != out_w) {                                   /* as much as the port takes now */
        uint32_t at = out_r & (OUT_RING - 1), len = MIN(out_w - out_r, OUT_RING - at);
        int sent = plat_midi_write(out_ring + at, (int)len);
        if (sent <= 0) break;
        out_r += (uint32_t)sent; midi.out_bytes += (uint32_t)sent;
    }
}

void midi_out_note(int ch, uint8_t note, uint8_t vel) {
    if (!midi.notes_out || midi.port < 0) return;
    uint8_t b[3] = { (uint8_t)((vel ? 0x90 : 0x80) | (ch & 15)), (uint8_t)(note & 127), vel ? (uint8_t)MIN(vel, 127) : 64 };
    out_bytes(b, 3);
}

static void omni_msg(int ch, uint8_t note, int vel) {
    if (!midi.omni_out || midi.port < 0) return;
    uint8_t b[3] = { (uint8_t)((vel ? 0x90 : 0x80) | ch), (uint8_t)(note & 127), vel ? (uint8_t)CLAMP(vel, 1, 127) : 64 };
    out_bytes(b, 3);
}
void midi_omni_string(int note, int main_vel, int sub_vel) {
    if (note < 0 || note > 127) return;
    omni_msg(0, (uint8_t)note, main_vel);
    if (sub_vel || !main_vel) omni_msg(3, (uint8_t)note, sub_vel);
}
void midi_omni_key(int note, int vel) { if (note >= 0 && note <= 127) omni_msg(0, (uint8_t)note, vel); }
void midi_omni_chord(const uint8_t *notes, int n) {
    static uint8_t last[3]; static int nlast;
    uint32_t st = plat_irq_save();
    for (int i = 0; i < nlast; i++) omni_msg(1, last[i], 0);
    nlast = MIN(n, 3);
    for (int i = 0; i < nlast; i++) { last[i] = notes[i]; omni_msg(1, notes[i], omni.pad_level); }
    plat_irq_restore(st);
}
void midi_omni_bass(int note, int vel) { if (note >= 0 && note <= 127) omni_msg(2, (uint8_t)note, vel); }
void midi_omni_drum(int gm, int vel) { omni_msg(9, (uint8_t)gm, vel); omni_msg(9, (uint8_t)gm, 0); }

void midi_clock_run(uint32_t frames) {
    if (!midi.clock_out || midi.port < 0 || midi.clock_in) return;
    if (seq.bpm != clock_bpm) {
        clock_bpm = seq.bpm ? seq.bpm : 120;
        clock_q16 = (uint32_t)(((uint64_t)rate * 60 << 16) / ((uint32_t)clock_bpm * 24));
        if (clock_left_q16 > clock_q16) clock_left_q16 = clock_q16;
    }
    uint32_t f = frames << 16;
    while (f >= clock_left_q16) { f -= clock_left_q16; clock_left_q16 = clock_q16; uint8_t b = 0xF8; out_bytes(&b, 1); }
    clock_left_q16 -= f;
}

void midi_transport(bool start) {
    if (!midi.clock_out || midi.port < 0 || midi.clock_in) return;
    uint8_t b = start ? 0xFA : 0xFC;
    if (start) clock_left_q16 = 0;                              /* the first clock with the start */
    out_bytes(&b, 1);
}
