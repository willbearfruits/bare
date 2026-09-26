/* Pass/fail checks of the whole instrument on the host. Usage: check [stick.img]   (exit status = failures)
   With an image (a copy of build/i386/bare.img), the project save/load round trip runs against it. */
#include "host.h"
#include "app.h"
#include "synth.h"
#include "audio.h"
#include "ui.h"
#include "rhythm.h"
#include "seq.h"
#include "omni.h"
#include "stretch.h"
#include "fm.h"
#include "wave.h"
#include "disk.h"
#include "project.h"
#include "sampler.h"
#include "mix.h"
#include "fx.h"
#include "midi.h"
#include "song.h"
#include "fat.h"
#include "undo.h"
#include "tape.h"
#include "keys.h"
#include "usbclass.h"
#include "synaptics.h"
#include "touch.h"
#include <math.h>
#include "install.h"
#include "net.h"
#include "link.h"
#include "splash.h"
#include "perf.h"
#include "ans.h"
#include "gendy.h"
#include "sieve.h"
#include "xen.h"
#include "cloud.h"
#include "upic.h"
#include "inst.h"
#include "doomhost.h"
#include "gfx.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (cond) { printf("  ok    "); } else { failures++; printf("  FAIL  "); } printf(__VA_ARGS__); printf("\n"); } while (0)

/* run ms through the app the way the HDA driver renders, so timing is what a listener would hear */
static void run_capture(int ms) {
    for (int i = 0; i < ms; i++) {
        host_now_ms++;
        host_audio_pump_ms();
        app_step(host_now_ms);
    }
}
static void run(int ms) { host_run(ms); }
/* PLAY with the omnichord showing (F1 on PLAY steps to the instruments): the queued keys first, then F1 if needed */
static void to_play(void) { host_run(1); if (app_page() != PAGE_PLAY) { host_tap(KEY_F1); host_run(1); } play_show(0); }

struct level { double peak, dc, rms; long clips; double hf; };   /* hf: rms of the first difference — what is audible */
static struct level measure(int ms) {
    static int16_t buf[128 * 2];
    double sum = 0, sq = 0, peak = 0, dsq = 0, prev = 0; long clips = 0, n = 0;
    for (int i = 0; i < ms * 48 / 128; i++) {
        host_now_ms += (i % 3 == 0);                 /* keep the main loop roughly in step */
        HOST_RENDER(buf, 128);
        app_step(host_now_ms);
        for (int k = 0; k < 256; k++) {
            double x = buf[k]; sum += x; sq += x * x; if (fabs(x) > peak) peak = fabs(x); if (fabs(x) >= 32767) clips++; n++;
            if (k & 1) { double l = buf[k - 1]; dsq += (l - prev) * (l - prev); prev = l; }
        }
    }
    return (struct level){ peak, sum / n, sqrt(sq / n), clips, sqrt(dsq / (n / 2)) };
}

static void audio_checks(void) {
    printf("audio\n");
    to_play(); run(50);
    /* the loudest thing the PLAY page does: a held chord with strums both rows, echo on */
    host_key('4', true); run(100);
    double peak = 0, dc = 0; long clips = 0;
    for (int rep = 0; rep < 3; rep++)
        for (const char *s = "asdfghjklzxcvbnm"; *s; s++) {
            host_tap((uint8_t)*s);
            struct level m = measure(60);
            if (m.peak > peak) peak = m.peak; dc += m.dc / 48; clips += m.clips;
        }
    host_key('4', false); run(500);
    CHECK(clips == 0, "chord + strums: no clipped samples (%ld)", clips);
    CHECK(fabs(dc) < 200, "chord + strums: DC offset %.0f (was ~ -5700)", dc);
    /* every preset alone */
    int worst_dc_p = -1; double worst_dc = 0;
    HOST_SET_ECHO(false);
    for (int p = 0; p < P_COUNT; p++) {
        if (p == P_SCAN || p == P_ROM) continue;
        synth_note_on(60, 100, (uint8_t)p, 0x7F0);
        struct level m = measure(400);
        synth_note_off_tag(0x7F0); measure(300);
        if (fabs(m.dc) > fabs(worst_dc)) { worst_dc = m.dc; worst_dc_p = p; }
    }
    CHECK(fabs(worst_dc) < 150, "every preset: DC under 150 (worst %.0f, %s)", worst_dc, worst_dc_p >= 0 ? synth_preset_name(worst_dc_p) : "-");
    HOST_SET_ECHO(true);
    /* voice stealing: far more notes than voices */
    for (int i = 0; i < 80; i++) synth_note_on((uint8_t)(36 + i % 48), 100, P_FATSAW, (uint16_t)(0x800 + i));
    struct level m = measure(200);
    CHECK(synth_active_voices() <= SYNTH_MAX_VOICES && m.clips == 0, "80 notes at once: %d voices, no clipping", synth_active_voices());
    host_tap(KEY_ESC); run(300);
    CHECK(synth_active_voices() == 0, "ESC: all voices off");
}

static void timing_checks(void) {
    printf("sequencer\n");
    host_tap(KEY_F2); run(20); HOST_DEMO(0); run(20); host_tap(KEY_SPACE); run(300);
    for (int t = 0; t < SEQ_TRACKS; t++) seq.ch[t].mute = t != 2;               /* the kick alone */
    HOST_SET_ECHO(false); run(500);
    host_wav_open("/tmp/hb-check-timing.wav");
    host_tap(KEY_SPACE); run_capture(6000);
    host_wav_close();
    host_tap(KEY_SPACE); run(300);
    for (int t = 0; t < SEQ_TRACKS; t++) seq.ch[t].mute = false;
    HOST_SET_ECHO(true);
    FILE *f = fopen("/tmp/hb-check-timing.wav", "rb"); fseek(f, 44, SEEK_SET);
    static int16_t d[48000 * 8 * 2]; long n = (long)fread(d, 4, 48000 * 8, f); fclose(f); remove("/tmp/hb-check-timing.wav");
    double on[64]; int no = 0; long quiet = 0;
    for (long i = 0; i < n && no < 64; i++) { int a = abs(d[2 * i]); if (a < 150) quiet++; else { if (a > 2000 && quiet > 48 * 30) on[no++] = i / 48000.0; if (a > 2000) quiet = 0; } }
    double beat = 60.0 / seq.bpm, worst = 0;
    for (int i = 1; i < no; i++) worst = fmax(worst, fabs(on[i] - on[i - 1] - beat));
    CHECK(no > 10 && worst < 0.0002, "kick every beat at %u BPM: %d onsets, worst deviation %.3f ms", seq.bpm, no, worst * 1000);
}

static void stretch_checks(void) {
    printf("stretcher\n");
    HOST_SET_ECHO(false);
    host_tap(KEY_F4); run(50);
    stretch.mix = 100;
    synth_note_on(69, 100, P_DRAWN, 0x7A0); run(1200);
    struct level dry = measure(300);
    host_tap(KEY_SPACE); run(1);
    CHECK(stretch.frozen && !omni.hold, "SPACE on the STRETCH page freezes, and leaves the chord HOLD alone");
    stretch.stay = true; stretch.pos = (stretch.region_start + stretch.region_len - stretch.win) % stretch.cap_len;
    synth_note_off_tag(0x7A0); run(1500);
    struct level wet = measure(2000);
    double db = 20 * log10(wet.rms / dry.rms);
    CHECK(fabs(db) < 3, "frozen sine comes out at the level it went in (%+.1f dB)", db);
    CHECK(fabs(wet.dc) < 100, "frozen output has no DC (%.0f)", wet.dc);
    host_tap(KEY_SPACE); run(50);
    stretch.stay = false; stretch.mix = 70;
    HOST_SET_ECHO(true);
}

static void key_checks(void) {
    printf("keys\n");
    host_tap(KEY_F3); run(50);
    host_key('z', true); run(30);
    CHECK(synth_active_voices() > 0, "WAVE page: Z plays the drawn wave (the bottom rows are a keyboard)");
    host_key('z', false); host_tap(KEY_ESC); run(300);
    host_tap(KEY_F2); run(50);
    struct seq_cell before = seq_pat[seq.edit_pat].cell[0][0];
    host_tap(KEY_HOME); host_tap('z'); run(30);
    struct seq_cell *c0 = &seq_pat[seq.edit_pat].cell[0][0];
    CHECK(c0->note == 48, "SEQ page: Z enters C3 at the cursor (%u)", c0->note);
    *c0 = before;
    to_play(); run(30);
    host_key(KEY_LCTRL, true); host_tap('5'); host_key(KEY_LCTRL, false); run(30);
    host_tap(KEY_ENTER); run(20);                                       /* OPERATOR: start renaming */
    host_tap('x'); to_play(); host_tap(KEY_ESC); run(20);         /* while typing, F1 must not leave the page */
    host_key('a', true); run(30);
    CHECK(synth_active_voices() > 0, "OPERATOR: typing a name then ESC; the strum plate still plays the patch");
    host_key('a', false); host_tap(KEY_ESC); run(300);
}

/* a v1 project whose FM patch uses TWO PAIR must keep op 3 silent; junk must load as nothing harmful */
static void project_checks(void) {
    printf("projects\n");
    static uint8_t blob[PROJECT_MAX];
    /* a project the way 1.0 wrote it: version 1, the FM bank in its old layout, patch 2 on TWO PAIR with op 3 up */
    struct { char name[12]; uint8_t algo, feedback; struct { uint8_t wave, ratio_x2, fine, level; uint16_t a_ms, d_ms, r_ms; uint8_t s_pct; } op[FM_OPS]; } v1[FM_PATCHES];
    memset(v1, 0, sizeof v1);
    for (int k = 0; k < FM_PATCHES; k++) { snprintf(v1[k].name, sizeof v1[k].name, "OLD %d", k); for (int o = 0; o < FM_OPS; o++) { v1[k].op[o].ratio_x2 = 2; v1[k].op[o].level = 50; v1[k].op[o].d_ms = 300; } }
    v1[1].algo = 2; v1[1].op[2].level = 60; v1[1].op[0].a_ms = 77;
    size_t n = 0; uint32_t l;
    memcpy(blob, "HBPJ", 4); l = 1; memcpy(blob + 4, &l, 4); blob[8] = '1'; n = 9;
    memcpy(blob + n, "FMBK", 4); l = sizeof v1; memcpy(blob + n + 4, &l, 4); memcpy(blob + n + 8, v1, sizeof v1); n += 8 + sizeof v1;
    memcpy(blob + n, "END ", 4); l = 0; memcpy(blob + n + 4, &l, 4); n += 8;
    project_load(blob, n);
    CHECK(fm_bank[1].algo == 2 && fm_bank[1].op[2].level == 0 && fm_bank[1].op[0].a_ms == 77 && !strcmp(fm_bank[1].name, "OLD 1") && fm_bank[1].op[0].vel == 0,
          "a 1.0 FM bank loads through the old layout; its TWO PAIR patch keeps op 3 silent, as it sounded");
    fm_bank[1].op[2].level = 60; fm_bank[1].op[1].vel = 70; fm_bank[1].lfo_pitch = 40;
    n = project_save(blob, sizeof blob, 0, true);
    CHECK(n > 0 && blob[8] == '2', "saves as version 2 (%zu bytes)", n);
    fm_init();
    project_load(blob, n);
    CHECK(fm_bank[1].op[2].level == 60 && fm_bank[1].op[1].vel == 70 && fm_bank[1].lfo_pitch == 40, "a version 2 patch keeps op 3, its velocity and its LFO");
    fm_init();
    /* corrupt every FM byte and the stretch settings: must come back in range */
    size_t i = 0;
    while (i + 8 < n && memcmp(blob + i, "FMB2", 4) != 0) { uint32_t l2; memcpy(&l2, blob + i + 4, 4); i += 8 + l2; }
    for (size_t k = i + 8; k < i + 8 + sizeof fm_bank && k < n; k++) blob[k] = (uint8_t)(k * 97 + 13);
    project_load(blob, n);
    bool sane = true;
    for (int p = 0; p < FM_PATCHES; p++) {
        if (fm_bank[p].algo >= FM_ALGOS || fm_bank[p].name[11]) sane = false;
        for (int o = 0; o < FM_OPS; o++) if (fm_bank[p].op[o].wave > 3 || !fm_bank[p].op[o].ratio_x2 || fm_bank[p].op[o].level > 100) sane = false;
    }
    CHECK(sane, "a corrupted FM bank loads clamped into range");
    synth_note_on(60, 100, P_FM1, 0x7C0); run(100); synth_note_off_tag(0x7C0); run(100);
    CHECK(true, "and plays without crashing");
    fm_init();
}

/* the pitch of what plays: rising zero crossings of the left channel */
static double measure_freq(int ms) {
    static int16_t buf[128 * 2];
    long n = 0, first = -1, last = 0, crossings = 0; int16_t prev = 0;
    for (int i = 0; i < ms * 48 / 128; i++) {
        host_now_ms += (i % 3 == 0);
        HOST_RENDER(buf, 128);
        app_step(host_now_ms);
        for (int k = 0; k < 128; k++, n++) {
            int16_t x = buf[2 * k];
            if (prev < 0 && x >= 0) { if (first < 0) first = n; else { last = n; crossings++; } }
            prev = x;
        }
    }
    return crossings && last > first ? crossings * 48000.0 / (double)(last - first) : 0;
}

static int32_t sample_peak(const struct sample *s) {
    int32_t p = 0;
    for (uint32_t i = 0; i < s->len; i++) { int32_t a = abs(sampler_frame(s, i)); if (a > p) p = a; }
    return p;
}
static uint32_t sample_sum(const struct sample *s) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < s->len; i++) h = (h ^ (uint32_t)sampler_frame(s, i)) * 16777619u;
    return h;
}

/* the sampler on WAVE's page 8: record, pitch, 8-bit and lower rates, loop, one-shot, grab, the stretcher */
static void sampler_checks(void) {
    printf("sampler\n");
    to_play(); host_tap(KEY_ESC); run(300);
    CHECK(sampler.slot_bytes >= 256 * 1024, "sample memory: %u KiB a slot", sampler.slot_bytes >> 10);
    HOST_SET_ECHO(false);
    int draw_slot = synth_draw_slot; synth_draw_slot = 0;              /* wave 1 is a sine */
    host_tap(KEY_F3); run(30);
    for (int i = 0; i < 3; i++) host_tap(KEY_TAB);
    run(30);
    host_tap('1'); host_tap('r'); run(200);
    CHECK(sampler.state == SMP_ARMED, "PAGE 8: R arms a recording, which waits for sound");
    synth_note_on(69, 110, P_DRAWN, 0x7D0); run(1000); synth_note_off_tag(0x7D0); run(500);
    host_tap('r'); run(50);
    struct sample *s = &samples[0];
    CHECK(s->len > 48000 * 9 / 10 && s->len < 48000 * 3 / 2 && !s->busy, "R again stops: %.2f s recorded (A4 held 1 s)", s->len / 48000.0);
    int32_t pk = sample_peak(s);
    CHECK(pk > 28000 && pk < 30000, "normalised to -1 dBFS (peak %d)", pk);
    uint32_t len16 = s->len;
    synth_note_on(60, 110, P_SMP1, 0x7D1); run(100);
    double f = measure_freq(400); synth_note_off_tag(0x7D1); run(300);
    CHECK(fabs(f - 440) < 2, "played at its root, C4, it keeps the pitch it was recorded at: %.1f Hz", f);
    synth_note_on(72, 110, P_SMP1, 0x7D1); run(100);
    f = measure_freq(400); synth_note_off_tag(0x7D1); run(300);
    CHECK(fabs(f - 880) < 4, "an octave up: %.1f Hz", f);
    host_tap('e'); host_tap('w'); host_tap('w'); host_tap('w'); run(30);
    CHECK(s->bits == 8 && s->rate == 16000 && abs((int)s->len - (int)(len16 / 3)) < 4, "E, W W W: 8 bit at 16 kHz, a third of the frames (%u → %u)", len16, s->len);
    bool steps = true; for (uint32_t i = 0; i < s->len; i++) if (sampler_frame(s, i) % 256) steps = false;
    CHECK(steps, "8-bit frames are 8-bit");
    synth_note_on(60, 110, P_SMP1, 0x7D1); run(100);
    f = measure_freq(400); synth_note_off_tag(0x7D1); run(300);
    CHECK(fabs(f - 440) < 3, "and still plays at 440 Hz: %.1f Hz", f);
    /* loop: hold a note three times as long as the sample */
    host_tap('k'); run(20);
    s->loop_start = s->end / 2;
    synth_note_on(60, 110, P_SMP1, 0x7D1); run(3000);
    struct level m = measure(200);
    CHECK(s->loop && m.rms > 1000, "K loops it: still sounding after 3 s (rms %.0f)", m.rms);
    synth_note_off_tag(0x7D1); run(500);
    CHECK(synth_active_voices() == 0, "and the release ends it");
    host_tap('k'); host_tap('\''); run(20);
    synth_note_on(60, 110, P_SMP1, 0x7D1); run(30); synth_note_off_tag(0x7D1); run(300);
    CHECK(s->oneshot && synth_active_voices() == 1, "' makes it one-shot: it plays on after the key is let go");
    run(1500);
    host_tap('\''); host_tap(KEY_ESC); run(200);
    /* the keyboard plays the slot */
    host_key('z', true); run(50);
    CHECK(synth_active_voices() == 1, "Z plays the sample");
    host_key('z', false); run(400);
    /* grab: a phrase played after a pause, then taken from the stretcher's ring */
    host_tap(KEY_ESC); run(1500);
    for (int i = 0; i < 4; i++) { synth_note_on((uint8_t)(60 + 4 * i), 110, P_ORGAN, 0x7D2); run(200); synth_note_off_tag(0x7D2); }
    run(600);
    host_tap('2'); host_tap('a'); run(50);
    double g = samples[1].len / (double)samples[1].rate;
    CHECK(g > 0.7 && g < 2.0, "A grabs the phrase just played into slot 2: %.2f s", g);
    host_tap('f'); run(1500);
    m = measure(500);
    CHECK(stretch.frozen && m.rms > 300, "F hands it to the stretcher, which plays it frozen (rms %.0f)", m.rms);
    stretch_freeze(false); host_tap(KEY_ESC); run(300);
    /* a project carries the samples: the frames follow the blob */
    static uint8_t blob[PROJECT_MAX]; static uint8_t room[8 << 20];
    uint32_t sum0 = sample_sum(&samples[0]), sum1 = sample_sum(&samples[1]), len0 = samples[0].len;
    size_t n = project_save(blob, sizeof blob, sizeof room, true);
    int nm = project_media_count();
    for (int i = 0; i < nm; i++) { struct project_media m; if (project_media_get(i, &m)) memcpy(room + m.offset, m.data, m.bytes); }
    sampler_clear(0); sampler_clear(1);
    project_load(blob, n);
    nm = project_media_count();
    for (int i = 0; i < nm; i++) { struct project_media m; if (project_media_get(i, &m)) memcpy(m.data, room + m.offset, m.bytes); }
    project_loaded();
    CHECK(nm == 2 && samples[0].len == len0 && sample_sum(&samples[0]) == sum0 && sample_sum(&samples[1]) == sum1 && samples[0].bits == 8,
          "a saved project brings both samples back, frame for frame");
    n = project_save(blob, sizeof blob, 4096, true);
    CHECK(project_media_dropped() == 2, "without room for the frames, they are left out, and it says so");
    synth_draw_slot = draw_slot; HOST_SET_ECHO(true);
    host_tap(KEY_TAB); to_play(); run(50);
}

/* level and pitch over ms, rendered the way the 1 kHz tick does, so the line input keeps flowing */
struct heard { double rms, rms_r, hz; };
static struct heard listen(int ms) {
    double sq = 0, sq_r = 0, sum_l = 0, sum_r = 0; long n = 0, first = -1, last = 0, crossings = 0; int16_t prev = 0;
    for (int i = 0; i < ms; i++) {
        host_now_ms++; host_audio_ms(); app_step(host_now_ms);
        for (int k = 0; k < host_last_frames; k++, n++) {
            int16_t l = host_last_audio[2 * k], r = host_last_audio[2 * k + 1];
            sq += (double)l * l; sq_r += (double)r * r; sum_l += l; sum_r += r;
            if (prev < 0 && l >= 0) { if (first < 0) first = n; else { last = n; crossings++; } }
            prev = l;
        }
    }
    double ml = sum_l / n, mr = sum_r / n;                               /* what is heard: without the DC */
    return (struct heard){ sqrt(fmax(0, sq / n - ml * ml)), sqrt(fmax(0, sq_r / n - mr * mr)), crossings && last > first ? crossings * 48000.0 / (double)(last - first) : 0 };
}

/* the mixer on F8, and the line input */
static void mix_checks(void) {
    printf("mixer and input\n");
    to_play(); host_tap(KEY_ESC); run(300);
    HOST_SET_ECHO(false);
    host_tap(KEY_F8); run(3000);                                        /* the echo tails of earlier checks die away */
    host_tap(KEY_END); host_key('4', true); run(200);                  /* the PLAY strip is selected: off */
    struct heard h = listen(300);
    CHECK(mix.ch[CH_PLAY].db == MIX_DB_OFF && h.rms < 5, "MIX: END takes the PLAY fader off; a chord is silent (rms %.1f)", h.rms);
    host_tap(KEY_HOME); run(100); h = listen(300);
    CHECK(mix.ch[CH_PLAY].db == 0 && h.rms > 1000, "HOME puts it back at 0 dB (rms %.0f)", h.rms);
    host_tap(']'); for (int i = 0; i < 9; i++) host_tap('['); run(100);
    h = listen(300);
    CHECK(mix.ch[CH_PLAY].pan == -80 && h.rms_r < h.rms * 0.3, "[ ] pan: hard left, the right side falls away (L %.0f, R %.0f)", h.rms, h.rms_r);
    mix.ch[CH_PLAY].pan = 0;
    host_tap(KEY_RIGHT); host_tap(KEY_BACKSPACE); run(100); h = listen(300);
    CHECK(mix.ch[CH_SEQ].solo && h.rms < 5, "BKSP solos SEQ: PLAY is not heard (rms %.1f)", h.rms);
    host_tap(KEY_BACKSPACE); host_key('4', false); host_tap(KEY_ESC); run(300);
    /* the input: picked with \, heard only with MON on */
    host_input_tone(1000, 8000);
    host_tap('\\'); run(100);
    h = listen(300);
    CHECK(mix.input == 0 && h.rms < 5, "\\ turns the line input on; muted, it is not heard (rms %.1f)", h.rms);
    host_tap(KEY_RIGHT); host_tap(KEY_RIGHT); host_tap(KEY_ENTER); run(100);          /* INPUT: MON */
    h = listen(500);
    CHECK(!mix.ch[CH_INPUT].mute && fabs(h.hz - 1000) < 3 && h.rms > 2000, "ENTER on INPUT: MON, the 1 kHz tone comes through (%.1f Hz, rms %.0f)", h.hz, h.rms);
    CHECK(mix.in_xruns == 0, "the input never ran dry (%u gaps)", mix.in_xruns);
    HOST_FREEZE(); run(1500); h = listen(500);
    CHECK(stretch.frozen && h.rms > 300, "heard, the input reaches the stretcher: frozen, it plays (rms %.0f)", h.rms);
    HOST_FREEZE(); run(300);
    host_tap(KEY_ENTER); run(11000);                                   /* muted, until the stretcher's 10 s have passed */
    HOST_FREEZE(); run(1500); h = listen(500);
    CHECK(h.rms < 5, "muted, it does not: nothing to freeze (rms %.1f)", h.rms);
    HOST_FREEZE(); run(300);
    /* unheard, the input still reaches the sampler */
    host_tap(KEY_F3); for (int i = 0; i < 3; i++) host_tap(KEY_TAB);
    run(30);
    for (int i = 0; i < SMP_SOURCES && sampler.source != SMP_IN; i++) { host_tap('q'); run(10); }
    host_tap('3'); host_tap('r'); run(700); host_tap('r'); run(50);
    CHECK(samples[2].rate && samples[2].len > samples[2].rate / 2, "WAVE page 8, source IN: records the input (%.2f s)", samples[2].len / (double)samples[2].rate);
    for (int i = 0; i < SMP_SOURCES && sampler.source != SMP_OUT; i++) { host_tap('q'); run(10); }
    host_tap(KEY_TAB); host_tap(KEY_F8); run(30);
    /* the mixer is saved with the project */
    static uint8_t blob[PROJECT_MAX];
    mix.ch[CH_RHYTHM].db = -12; mix.ch[CH_TAPE].pan = 40;
    size_t n = project_save(blob, sizeof blob, 0, true);
    mix.ch[CH_RHYTHM].db = 0; mix.ch[CH_TAPE].pan = 0;
    project_load(blob, n);
    CHECK(mix.ch[CH_RHYTHM].db == -12 && mix.ch[CH_TAPE].pan == 40, "the mixer comes back with the project");
    mix.ch[CH_RHYTHM].db = 0; mix.ch[CH_TAPE].pan = 0;
    mix_set_input(-1); host_input_tone(0, 0);
    HOST_SET_ECHO(true); to_play(); run(50);
}

/* the 8-track tape: record, play back, an input-only track, the output's delay taken off, erase, copy a region */
static int first_loud(int t, uint32_t from, uint32_t to) {
    for (uint32_t p = from; p < to; p++) if (abs(tape_sample(t, p)) > 2000) return (int)p;
    return -1;
}
static void tape_checks(void) {
    printf("tape\n");
    host_tap(KEY_F6); host_tap(KEY_ESC); run(300);
    CHECK(tape.len && tape.blocks >= 64, "tape: %u blocks, %u s of mono sound left", tape.blocks, tape_free_seconds());
    uint32_t free0 = tape.blocks_free;
    HOST_SET_ECHO(false);
    tape.cur = 0; host_tap(KEY_HOME);
    for (int t = 0; t < TAPE_TRACKS; t++) tape.tr[t].arm = false;
    host_tap('q'); host_tap('r'); run(100);
    CHECK(tape.recording && tape.playing && tape.tr[0].arm, "Q arms track 1, R records");
    host_key('4', true); run(2000); host_key('4', false); run(300);
    host_tap('r'); host_tap(KEY_SPACE); run(50);
    CHECK(tape.tr[0].used > 48000 * 2 && tape.blocks_free < free0, "2.3 s on track 1 (%.2f s, %u blocks taken)", tape.tr[0].used / 48000.0, free0 - tape.blocks_free);
    host_tap('q'); host_tap(KEY_HOME); host_tap(KEY_SPACE); run(300);
    struct heard h = listen(500);
    CHECK(tape.playing && h.rms > 500 && synth_active_voices() == 0, "played back from the start, the chord comes off the tape (rms %.0f)", h.rms);
    host_tap(KEY_SPACE); run(50);
    /* track 2 takes the input alone, heard or not */
    host_input_tone(1000, 8000); mix_set_input(0); run(100);
    host_tap(KEY_DOWN); host_tap('q'); host_shift_tap('q'); host_tap(KEY_HOME); run(20);
    CHECK(tape.cur == 1 && tape.tr[1].source == TAPE_SRC_IN, "↓ ⇧Q: track 2 records the input alone");
    host_key('4', true); host_tap('r'); run(1000); host_tap('r'); host_tap(KEY_SPACE); host_key('4', false); run(300);
    uint32_t cross = 0; int16_t prev = 0;
    for (uint32_t p = 12000; p < 36000; p++) { int16_t v = tape_sample(1, p); if (prev < 0 && v >= 0) cross++; prev = v; }
    CHECK(cross > 490 && cross < 510, "it holds the 1 kHz tone and not the chord (%u cycles in 0.5 s)", cross);
    host_tap('q'); mix_set_input(-1); host_input_tone(0, 0);
    /* the output's delay: what is played while the tape rolls lands that much earlier, where it was heard */
    host_latency = 480;
    host_tap(KEY_DOWN); host_tap('q'); host_tap(KEY_HOME); host_tap('r'); run(500);
    uint32_t at = tape.pos;
    synth_note_on(60, 120, P_CHIP, 0x7E0); run(400); synth_note_off_tag(0x7E0); run(300);   /* CHIP: a 1 ms attack */
    host_tap('r'); host_tap(KEY_SPACE); run(50); host_latency = 0;
    int onset = first_loud(2, 0, at + 4800);
    CHECK(onset >= 0 && abs(onset - (int)(at - 480)) < 100, "with 480 frames of output delay, a note played at %u lands at %d", at, onset);
    host_tap('q');
    /* a region of track 1 copied onto track 4 at 5 s */
    tape.loop_in = 48000; tape.loop_out = 72000; tape.loop = false;
    tape.cur = 0; host_shift_tap('c'); run(20); tape.cur = 3; tape_seek_to(48000 * 5); host_shift_tap('v'); run(20);
    bool same = true; for (uint32_t k = 0; k < 24000; k += 7) if (tape_sample(3, 48000 * 5 + k) != tape_sample(0, 48000 + k)) same = false;
    CHECK(same && tape.tr[3].used == 48000 * 5 + 24000, "⇧C ⇧V: the region of track 1 is on track 4 at 5 s, frame for frame");
    tape.cur = 0; host_shift_tap('d'); run(20);
    CHECK(first_loud(0, 48000, 72000) < 0 && first_loud(0, 0, 40000) >= 0, "⇧D erases the region on track 1, and only the region");
    for (int t = 0; t < 4; t++) { tape.cur = t; host_tap('e'); host_tap('e'); run(10); }
    for (int i = 0; i < 200; i++) tape_work();
    CHECK(tape.blocks_free == free0 && tape.used == 0, "E E on each track: all the blocks are back (%u of %u)", tape.blocks_free, free0);
    tape.loop_in = tape.loop_out = 0; tape.cur = 0;
    HOST_SET_ECHO(true); to_play(); run(50);
}

/* the tracker: notes and effects on the sample clock, the order list, patterns saved, 1.0 songs imported */
static void tracker_checks(void) {
    printf("tracker\n");
    host_tap(KEY_F2); host_tap(KEY_ESC); run(200);
    HOST_DEMO(2); run(50); host_tap(KEY_SPACE); run(300);                 /* EMPTY, stopped */
    HOST_SET_ECHO(false);
    int draw_slot = synth_draw_slot; synth_draw_slot = 0;                 /* DRAWN: a sine */
    struct seq_pattern *p0 = &seq_pat[0], *p1 = &seq_pat[1];
    memset(p0->cell, 0, sizeof p0->cell); memset(p1->cell, 0, sizeof p1->cell);
    seq.bpm = 120; seq.lpb = 4; p0->rows = 16; p1->rows = 16;             /* a row is 125 ms */
    p0->cell[0][0] = (struct seq_cell){ 69, P_DRAWN + 1, 0, 0, 0 };
    p0->cell[8][0] = (struct seq_cell){ 81, 0, 0, 3, 0xFF };              /* A5 with 3FF: glide up, no new note */
    seq.edit_pat = 0; seq_play_pattern(true); run(60);
    struct heard h = listen(300);
    CHECK(fabs(h.hz - 440) < 2, "row 0: A4 on DRAWN plays 440 Hz (%.1f)", h.hz);
    run(700); h = listen(300);
    CHECK(fabs(h.hz - 880) < 4 && synth_active_voices() == 1, "row 8: 3FF glides to A5 (%.1f Hz) on the same voice (%d)", h.hz, synth_active_voices());
    seq_play(false); run(300);
    /* ECx lets go inside the row, on the tick; the volume column sets the velocity */
    memset(p0->cell, 0, sizeof p0->cell);
    p0->cell[0][0] = (struct seq_cell){ 69, P_CHIP + 1, 0x7F + 1, 0xE, 0xC4 };        /* a quarter of the row */
    p0->cell[8][0] = (struct seq_cell){ 69, P_CHIP + 1, 0x3F + 1, 0, 0 };
    seq_play_pattern(true);
    int16_t buf[48000 * 2 / 2]; int got = 0;
    for (int i = 0; i < 250 && got < 24000; i++) { host_now_ms++; host_audio_ms(); app_step(host_now_ms); memcpy(buf + got * 2, host_last_audio, (size_t)host_last_frames * 4); got += host_last_frames; }
    int last_loud = 0; for (int i = 0; i < 6000; i++) if (abs(buf[2 * i]) > 300) last_loud = i;
    CHECK(last_loud > 1400 && last_loud < 1500 + 1200, "EC4 lets the note go a quarter row in: sound ends at frame %d (1500 + the release)", last_loud);
    run(700); h = listen(200);
    double quiet = h.rms;
    seq_play(false); run(300);
    p0->cell[8][0].vol = 0x7F + 1; seq_play_pattern(true); run(1060); h = listen(200); seq_play(false); run(300);
    CHECK(quiet > 100 && quiet < h.rms * 0.7, "volume 3F plays softer than 7F (rms %.0f against %.0f)", quiet, h.rms);
    /* the order list and Dxx: pattern 0 breaks on row 2 into pattern 1 at row 5 */
    memset(p0->cell, 0, sizeof p0->cell);
    p0->cell[2][0] = (struct seq_cell){ 0, 0, 0, 0xD, 5 };
    seq.song_len = 2; seq.order[0] = 0; seq.order[1] = 1; seq.ord = 0;
    seq_play(true); run(125 * 3 + 20);
    CHECK(seq.edit_pat == 1 && seq.pos == 5, "D05 on row 2 goes on to order 1, pattern 1, row 5 (pattern %u row %d)", seq.edit_pat, seq.pos);
    seq_play(false); run(100);
    /* patterns and the song survive a save */
    static uint8_t blob[PROJECT_MAX];
    p1->cell[9][3] = (struct seq_cell){ 50, 4, 0x20 + 1, 0xA, 0x0F };
    size_t n = project_save(blob, sizeof blob, 0, true);
    struct seq_cell keep = p1->cell[9][3]; memset(p1->cell, 0, sizeof p1->cell); seq.song_len = 1;
    project_load(blob, n);
    CHECK(!memcmp(&seq_pat[1].cell[9][3], &keep, sizeof keep) && seq.song_len == 2 && seq.order[1] == 1, "patterns, the order list and the song's settings come back from a save");
    /* a 1.0 song: a tied note with a 50 %% gate becomes a note held two rows and cut half way through the second */
    uint8_t note[64] = { 0 }, vel[64] = { 0 };
    note[0] = 60; note[1] = 255; note[4] = 62;
    seq_import_steps("OLD", 100, 0, "LEAD", P_SQLEAD, 50, false, note, vel);
    const struct seq_cell *a = &seq_pat[0].cell[0][0], *b = &seq_pat[0].cell[1][0];
    CHECK(a->note == 60 && a->inst == P_SQLEAD + 1 && b->note == 0 && b->fx == 0xE && b->param == 0xC8 && seq_pat[0].cell[4][0].note == 62,
          "1.0 steps import: the note, held through its tie, cut at the gate (E%X%02X)", b->fx, b->param);
    synth_draw_slot = draw_slot; HOST_SET_ECHO(true);
    seq_load_demo(0); to_play(); run(100);                      /* back to the first demo, not playing */
}

/* the effects: a reverb tail where the send is up, the master's filter, drive and crusher */
static void fx_checks(void) {
    printf("effects\n");
    host_tap(KEY_F8); host_tap(KEY_ESC); run(2000);
    HOST_SET_ECHO(false);
    mix.ch[CH_PLAY].reverb = 0;
    synth_note_on(60, 120, P_CHIP, 0x7F1); run(200); synth_note_off_tag(0x7F1); run(150);
    struct heard dry = listen(300);
    run(2000);
    mix.ch[CH_PLAY].reverb = 100;
    synth_note_on(60, 120, P_CHIP, 0x7F1); run(200); synth_note_off_tag(0x7F1); run(150);
    struct heard wet = listen(300);
    run(4000); struct heard later = listen(300);
    CHECK(wet.rms > 200 && dry.rms < 20 && later.rms < wet.rms / 8, "reverb send: a tail after the note (rms %.0f, dry %.0f), dying away (%.0f)", wet.rms, dry.rms, later.rms);
    mix.ch[CH_PLAY].reverb = 0; run(3000);
    /* the inserts, on a test signal: a 5 kHz square through the low-pass, a quiet sine through the drive, the crusher */
    static int32_t l[32], r[32];
    fx.filter_on = true; fx.filter_mode = FXF_LOW; fx.filter_cut = 40; fx.filter_res = 0;
    double in = 0, out = 0;
    for (int b = 0; b < 200; b++) {
        for (int i = 0; i < 32; i++) { int k = b * 32 + i; l[i] = r[i] = (k / 5) % 2 ? 8000 : -8000; }
        fx_master(l, r, 32);
        if (b > 20) for (int i = 0; i < 32; i++) { in += 8000.0 * 8000; out += (double)l[i] * l[i]; }
    }
    CHECK(sqrt(out / in) < 0.2, "FILTER low at cutoff 40: a 4.8 kHz square comes out %.0f dB down", 20 * log10(sqrt(out / in)));
    fx.filter_on = false; fx.drive_on = true; fx.drive = 100;
    in = out = 0;
    for (int b = 0; b < 50; b++) {
        for (int i = 0; i < 32; i++) { int k = b * 32 + i; l[i] = r[i] = (int32_t)(2000 * sin(k * 0.05)); in += (double)l[i] * l[i]; }
        fx_master(l, r, 32);
        for (int i = 0; i < 32; i++) out += (double)l[i] * l[i];
    }
    CHECK(out > in * 16, "DRIVE at 100 lifts a quiet signal %.0f dB", 10 * log10(out / in));
    fx.drive_on = false; fx.crush_on = true; fx.crush_bits = 4; fx.crush_rate = 3;
    bool steps = true;
    for (int b = 0; b < 10; b++) {
        for (int i = 0; i < 32; i++) l[i] = r[i] = (int32_t)(20000 * sin((b * 32 + i) * 0.01));
        fx_master(l, r, 32);
        for (int i = 0; i < 32; i++) if (l[i] % 4096) steps = false;
    }
    CHECK(steps, "CRUSH at 4 bits: every frame on a 4-bit step");
    fx.crush_on = false;
    HOST_SET_ECHO(true); to_play(); run(50);
}

/* MIDI through the host's port: notes, running status, bend, the pedal, the tracker's entry, clock in and out */
static void midi_send(const uint8_t *b, int n) { host_midi_in(b, n); run(20); }
static void midi_checks(void) {
    printf("midi\n");
    host_tap(KEY_F3); host_tap(KEY_ESC); run(300);                     /* WAVE: MIDI plays its sound, DRAWN */
    HOST_SET_ECHO(false);
    int draw_slot = synth_draw_slot; synth_draw_slot = 0;
    CHECK(midi_open(0), "the host's MIDI port opens");
    midi_send((const uint8_t[]){ 0x90, 69, 100 }, 3);
    struct heard h = listen(300);
    CHECK(synth_active_voices() == 1 && fabs(h.hz - 440) < 2, "note on A4: the page's sound at 440 Hz (%.1f)", h.hz);
    midi_send((const uint8_t[]){ 0xE0, 0x7F, 0x7F }, 3);
    h = listen(300);
    CHECK(fabs(h.hz - 493.9) < 4, "pitch bend up: two semitones (%.1f Hz)", h.hz);
    midi_send((const uint8_t[]){ 0xE0, 0x00, 0x40, 64, 100 }, 5);        /* centre, then running status: no new status */
    CHECK(synth_active_voices() == 1, "running status: a data pair on its own is not a note for the bend message");
    midi_send((const uint8_t[]){ 0x90, 60, 100, 64, 100 }, 5);           /* a note, then another under running status */
    CHECK(synth_active_voices() == 3, "running status: two notes from one status byte (%d voices)", synth_active_voices());
    midi_send((const uint8_t[]){ 0xB0, 64, 127, 0x80, 69, 0, 0x90, 60, 0 }, 9);   /* pedal down, let go of two */
    run(600);
    CHECK(synth_active_voices() == 3, "the sustain pedal holds the notes let go under it");
    midi_send((const uint8_t[]){ 0xB0, 64, 0, 123, 0 }, 5);
    run(800);
    CHECK(synth_active_voices() == 0, "pedal up, then all notes off: silence");
    /* the tracker, stopped, writes a note at the cursor */
    host_tap(KEY_F2); run(30); host_tap(KEY_HOME); run(20);
    struct seq_cell keep = seq_pat[seq.edit_pat].cell[0][0];
    midi_send((const uint8_t[]){ 0x90, 72, 90, 0x80, 72, 0 }, 6);
    CHECK(seq_pat[seq.edit_pat].cell[0][0].note == 72, "SEQ: a MIDI note goes in at the cursor (%u)", seq_pat[seq.edit_pat].cell[0][0].note);
    seq_pat[seq.edit_pat].cell[0][0] = keep;
    /* clock in: 24 a beat at 100 BPM sets the tempo; start and stop run the tracker */
    uint16_t bpm = seq.bpm; midi.clock_in = true;
    for (int i = 0; i < 72; i++) { host_midi_in((const uint8_t[]){ 0xF8 }, 1); run(25); }
    CHECK(midi.ext_bpm >= 99 && midi.ext_bpm <= 101 && seq.bpm == midi.ext_bpm, "clock in at 25 ms a clock: %u BPM", midi.ext_bpm);
    midi_send((const uint8_t[]){ 0xFA }, 1);
    CHECK(seq.playing, "start (FA) plays the song");
    midi_send((const uint8_t[]){ 0xFC }, 1);
    CHECK(!seq.playing, "stop (FC) stops it");
    midi.clock_in = false; seq.bpm = bpm;
    /* out: the tracker's notes as MIDI channels, and 24 clocks a beat */
    midi.notes_out = midi.clock_out = true; host_midi_out_len = 0;
    seq_load_demo(0); seq.bpm = 120; seq_play(true); run(1000); seq_play(false); run(20);
    int ons = 0, clocks = 0, start = 0, stop = 0, chans = 0;
    for (int i = 0; i < host_midi_out_len; i++) {
        uint8_t b = host_midi_out[i];
        if ((b & 0xF0) == 0x90 && i + 2 < host_midi_out_len && host_midi_out[i + 2]) { ons++; chans |= 1 << (b & 15); }
        if (b == 0xF8) clocks++; if (b == 0xFA) start++; if (b == 0xFC) stop++;
    }
    CHECK(ons > 10 && (chans & 0x1F) == 0x1F, "the tracker's notes go out on channels 1-5 (%d notes)", ons);
    CHECK(start == 1 && stop == 1 && clocks >= 47 && clocks <= 49, "start, 48 clocks in a second at 120 BPM (%d), stop", clocks);
    midi.notes_out = midi.clock_out = false; midi_open(-1); seq_load_demo(0);
    synth_draw_slot = draw_slot; HOST_SET_ECHO(true); to_play(); run(50);
}

/* OPERATOR: velocity and key scaling set an operator's level; the LFO moves pitch and level */
static void fm_checks(void) {
    printf("operator\n");
    to_play(); host_tap(KEY_ESC); run(300);
    HOST_SET_ECHO(false);
    struct fm_patch keep = fm_bank[3], *pt = &fm_bank[3];
    memset(pt, 0, sizeof *pt); snprintf(pt->name, sizeof pt->name, "TEST"); pt->algo = 7; pt->lfo_rate = 60;
    pt->op[0] = (struct fm_op){ 0, 2, 0, 100, 1, 1, 50, 100, 100, 0 };           /* a sine, fully velocity-sensitive */
    for (int o = 1; o < FM_OPS; o++) pt->op[o].ratio_x2 = 2;
    synth_note_on(60, 127, P_FM4, 0x7F2); run(100); struct heard loud = listen(200); synth_note_off_tag(0x7F2); run(300);
    synth_note_on(60, 40, P_FM4, 0x7F2); run(100); struct heard soft = listen(200); synth_note_off_tag(0x7F2); run(300);
    CHECK(soft.rms < loud.rms * 0.45 && soft.rms > 20, "VELOCITY 100: velocity 40 plays at %.0f%% of velocity 127", 100 * soft.rms / loud.rms);
    pt->op[0].vel = 0; pt->op[0].key = 100;
    synth_note_on(48, 127, P_FM4, 0x7F2); run(100); struct heard low = listen(200); synth_note_off_tag(0x7F2); run(300);
    synth_note_on(96, 127, P_FM4, 0x7F2); run(100); struct heard high = listen(200); synth_note_off_tag(0x7F2); run(300);
    CHECK(high.rms < low.rms * 0.35, "KEY SCALE 100: C7 %.1f dB under C3", 20 * log10(high.rms / low.rms));
    pt->op[0].key = 0; pt->lfo_amp = 100; pt->lfo_rate = 50;
    synth_note_on(60, 127, P_FM4, 0x7F2); run(50);
    double lo = 1e9, hi = 0;
    for (int k = 0; k < 20; k++) { struct heard w = listen(25); lo = fmin(lo, w.rms); hi = fmax(hi, w.rms); }
    synth_note_off_tag(0x7F2); run(300);
    CHECK(hi > lo * 3, "LFO AMP 100: the level swings (rms %.0f to %.0f)", lo, hi);
    pt->lfo_amp = 0; pt->lfo_pitch = 100; pt->lfo_rate = 45;                  /* about 1 Hz */
    synth_note_on(69, 127, P_FM4, 0x7F2); run(50);
    double fl = 1e9, fh = 0;
    for (int k = 0; k < 16; k++) { struct heard w = listen(80); fl = fmin(fl, w.hz); fh = fmax(fh, w.hz); }
    synth_note_off_tag(0x7F2); run(300);
    CHECK(fh > 452 && fl < 428, "LFO PITCH 100: A4 swings a semitone either way (%.0f to %.0f Hz)", fl, fh);
    fm_bank[3] = keep; HOST_SET_ECHO(true);
}

/* undo and redo: Ctrl+Z / Ctrl+Y after edits on each page */
static void ctrl_tap(uint8_t c) { host_key(KEY_LCTRL, true); host_tap(c); host_key(KEY_LCTRL, false); run(30); }
static uint32_t tape_sum(int t) { uint32_t h = 2166136261u; for (uint32_t i = 0; i < tape.tr[t].used; i += 5) h = (h ^ (uint32_t)tape_sample(t, i)) * 16777619u; return h ^ tape.tr[t].used; }
/* the arithmetic of the USB class drivers: HID reports into keys and pointer events, USB-MIDI packets both ways */
static bool has_key(const struct hid_out *o, uint8_t code, bool down) {
    for (int i = 0; i < o->nkeys; i++) if (o->keys[i].code == code && o->keys[i].down == down) return true;
    return false;
}
/* Synaptics packets, W mode (w: 4-15 a finger's width, 0 two fingers, 3 pass-through), and advanced gesture mode's
   second-finger packet (x, y, z at half resolution) */
static void syn_pk(uint8_t p[6], int w, int x, int y, int z, int l) {
    p[0] = (uint8_t)(0x80 | ((w >> 2) & 3) << 4 | ((w >> 1) & 1) << 2 | l);
    p[1] = (uint8_t)(((y >> 8) & 0x0F) << 4 | ((x >> 8) & 0x0F));
    p[2] = (uint8_t)z;
    p[3] = (uint8_t)(0xC0 | ((y >> 12) & 1) << 5 | ((x >> 12) & 1) << 4 | (w & 1) << 2 | l);
    p[4] = (uint8_t)x; p[5] = (uint8_t)y;
}
static void agm_pk(uint8_t p[6], int x, int y, int z) {
    x >>= 1; y >>= 1; z >>= 1;
    p[0] = 0x84; p[1] = (uint8_t)x; p[2] = (uint8_t)y; p[3] = (uint8_t)(0xC0 | (z & 0x30));
    p[4] = (uint8_t)(((y >> 8) & 0x0F) << 4 | ((x >> 8) & 0x0F)); p[5] = (uint8_t)(0x10 | (z & 0x0F));
}
static void synaptics_checks(void) {
    struct syn_state st; struct pointer_event ev[3]; uint8_t pk[6], rel[3];
    syn_start(&st, true);
    syn_pk(pk, 5, 3000, 3000, 60, 0);
    int n = syn_packet(&st, pk, ev, rel);
    bool one = n == 1 && ev[0].finger == 0 && ev[0].z == 60 && ev[0].size == 23 && abs((int)ev[0].tx - 12516) < 16 && abs((int)ev[0].ty - 15608) < 16;
    agm_pk(pk, 5000, 2000, 50);
    int n0 = syn_packet(&st, pk, ev, rel);
    syn_pk(pk, 0, 2000, 3000, 70, 0);
    n = syn_packet(&st, pk, ev, rel);
    bool two = n0 == 0 && n == 2 && ev[0].finger == 0 && ev[1].finger == 1 && ev[0].tx < 6000 && ev[1].tx > 27000 && ev[1].z >= 30;
    CHECK(one && two, "Synaptics: a finger (its width and where it is), then a second one from its own packet (%d events)", n);
    syn_pk(pk, 5, 4900, 2000, 60, 0);                     /* the first lifts: the one left is the second, near 5000,2000 */
    n = syn_packet(&st, pk, ev, rel);
    bool kept = false, lifted = false;
    for (int i = 0; i < n; i++) { kept |= ev[i].finger == 1 && ev[i].z == 60; lifted |= ev[i].finger == 0 && ev[i].z == 0; }
    syn_pk(pk, 0, 0, 0, 0, 0);
    int n2 = syn_packet(&st, pk, ev, rel);
    bool all_up = n2 == 1 && ev[0].finger == 1 && ev[0].z == 0;
    syn_pk(pk, 3, 0, 0, 0, 0); pk[1] = 0x09; pk[4] = 5; pk[5] = 0xFD;
    int pt = syn_packet(&st, pk, ev, rel);
    CHECK(kept && lifted && all_up && pt == -1 && rel[0] == 0x09 && rel[1] == 5, "the first finger lifts: the second keeps its slot; then all up; a TrackPoint packet passes through");
    syn_start(&st, false);
    syn_pk(pk, 0, 2000, 3000, 70, 0);
    n = syn_packet(&st, pk, ev, rel);
    CHECK(n == 1 && ev[0].finger == 0 && ev[0].z == 70, "without gesture mode, two fingers are followed as one");
}

/* a webcam's configuration as cameras send it: VideoControl, then VideoStreaming with an MJPEG format (skipped) and a
   YUY2 one in three sizes, alternate settings 1-3 with bigger and bigger isochronous endpoints */
static int uvc_config(uint8_t *c) {
    int n = 0;
#define PUT(...) do { const uint8_t b_[] = { __VA_ARGS__ }; memcpy(c + n, b_, sizeof b_); n += (int)sizeof b_; } while (0)
    PUT(9, 2, 0, 0, 2, 1, 0, 0x80, 250);                                          /* configuration (lengths not checked) */
    PUT(8, 0x0B, 0, 2, 0x0E, 3, 0, 0);                                            /* interface association */
    PUT(9, 4, 0, 0, 1, 0x0E, 1, 0, 0);                                            /* VideoControl */
    PUT(13, 0x24, 0x01, 0x10, 0x01, 0x4D, 0, 0x80, 0x8D, 0x5B, 0, 1, 1);         /* header: UVC 1.10 */
    PUT(7, 5, 0x83, 3, 16, 0, 6);                                                /* its interrupt endpoint */
    PUT(9, 4, 1, 0, 0, 0x0E, 2, 0, 0);                                            /* VideoStreaming, alternate 0 */
    PUT(15, 0x24, 0x01, 2, 0, 0, 0x81, 0, 3, 0, 0, 0, 1, 0, 0);                  /* input header */
    PUT(11, 0x24, 0x06, 1, 1, 1, 0, 0, 0, 0, 0);                                  /* MJPEG format: not ours */
    PUT(30, 0x24, 0x07, 1, 0, 0x80, 0x02, 0xE0, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x15, 0x16, 5, 0, 1, 0x15, 0x16, 5, 0);
    PUT(27, 0x24, 0x04, 2, 3, 'Y', 'U', 'Y', '2', 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71, 16, 1, 0, 0, 0, 0);
    PUT(30, 0x24, 0x05, 1, 0, 0x80, 0x02, 0xE0, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x15, 0x16, 5, 0, 1, 0x15, 0x16, 5, 0);   /* 640x480 */
    PUT(30, 0x24, 0x05, 2, 0, 0x40, 0x01, 0xF0, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x15, 0x16, 5, 0, 1, 0x15, 0x16, 5, 0);   /* 320x240 */
    PUT(30, 0x24, 0x05, 3, 0, 0xA0, 0x00, 0x78, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x15, 0x16, 5, 0, 1, 0x15, 0x16, 5, 0);   /* 160x120 */
    for (int alt = 1; alt <= 3; alt++) {
        PUT(9, 4, 1, (uint8_t)alt, 1, 0x0E, 2, 0, 0);
        uint16_t mps = alt == 1 ? 512 : alt == 2 ? 1024 : (1024 | 2 << 11);
        PUT(7, 5, 0x81, 5, (uint8_t)mps, (uint8_t)(mps >> 8), 1);
    }
#undef PUT
    return n;
}

static void uvc_checks(void) {
    static uint8_t cfgb[1024], pic[160 * 120 * 2], b0[160 * 120 * 2], b1[160 * 120 * 2], luma[160 * 120];
    int n = uvc_config(cfgb);
    struct uvc_info u;
    bool ok = uvc_parse(cfgb, n, 0, &u);
    int pick = uvc_pick(&u, 320), small = uvc_pick(&u, 100);
    CHECK(ok && u.vc_if == 0 && u.vs_if == 1 && u.bcd == 0x0110 && u.nframes == 3 && pick >= 0 && u.frames[pick].w == 320 && u.frames[pick].h == 240 &&
          u.frames[pick].format == 2 && small >= 0 && u.frames[small].w == 160 && u.frames[pick].interval == 333333,
          "UVC: a webcam's descriptors: the streaming interface, YUY2 in 3 sizes (MJPEG skipped), 320x240 picked, 30 fps");
    /* a webcam that is two cameras, the infrared one (greys only) first: that one is passed over, the other found */
    static uint8_t two[512]; int m = 0;
#define PUT(...) do { const uint8_t b_[] = { __VA_ARGS__ }; memcpy(two + m, b_, sizeof b_); m += (int)sizeof b_; } while (0)
    PUT(9, 2, 0, 0, 4, 1, 0, 0x80, 250);
    for (int cam = 0; cam < 2; cam++) {
        uint8_t vc = (uint8_t)(cam * 2), vsi = (uint8_t)(vc + 1), ep = (uint8_t)(0x82 - cam);
        PUT(8, 0x0B, vc, 2, 0x0E, 3, 0, 0);
        PUT(9, 4, vc, 0, 0, 0x0E, 1, 0, 0);
        PUT(13, 0x24, 0x01, 0x00, 0x01, 0x4D, 0, 0x80, 0x8D, 0x5B, 0, 1, vsi);                  /* UVC 1.00, its stream */
        PUT(9, 4, vsi, 0, 0, 0x0E, 2, 0, 0);
        PUT(14, 0x24, 0x01, 1, 0, 0, ep, 0, 3, 0, 0, 0, 1, 0);
        if (cam == 0) PUT(27, 0x24, 0x04, 1, 1, 'Y', '8', ' ', ' ', 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71, 8, 1, 0, 0, 0, 0);
        else          PUT(27, 0x24, 0x04, 1, 1, 'Y', 'U', 'Y', '2', 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71, 16, 1, 0, 0, 0, 0);
        PUT(30, 0x24, 0x05, 1, 0, 0x80, 0x02, 0xE0, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x15, 0x16, 5, 0, 1, 0x15, 0x16, 5, 0);
        PUT(9, 4, vsi, 1, 1, 0x0E, 2, 0, 0);
        PUT(7, 5, ep, 5, 0x00, 0x04, 1);
    }
#undef PUT
    struct uvc_info ir, rgb;
    bool ir_ok = uvc_parse(two, m, 0, &ir), rgb_ok = uvc_parse(two, m, 2, &rgb);
    CHECK(!ir_ok && rgb_ok && rgb.vc_if == 2 && rgb.vs_if == 3 && rgb.bcd == 0x0100 && rgb.nframes == 1 && rgb.frames[0].w == 640,
          "UVC: a webcam that is two cameras: the infrared one's greys passed over, the picture found in its own stream (%d)", rgb.vs_if);
    uint8_t probe[48];
    int pl = uvc_probe_fill(&u, &u.frames[pick], probe);
    CHECK(pl == 34 && probe[2] == 2 && probe[3] == 2 && (probe[4] | probe[5] << 8 | probe[6] << 16) == 333333, "UVC: the probe (UVC 1.1: 34 bytes) asks for format 2, frame 2, 30 fps");
    /* a 160x120 picture in 1000-byte payloads: whole frames come out, a spoilt one doesn't, a frame without EOF ends at the toggle */
    struct uvc_frame f = u.frames[small];
    uint32_t want = uvc_frame_bytes(&f);
    struct uvc_asm a; uvc_asm_init(&a, b0, b1, sizeof b0, want);
    int got = 0; bool same = true;
    for (int fr = 0; fr < 4; fr++) {
        for (uint32_t i = 0; i < want; i++) pic[i] = (uint8_t)(i * 7 + fr * 31);
        for (uint32_t at = 0; at < want; at += 998) {
            uint8_t p[1000]; uint32_t k = MIN(998u, want - at);
            p[0] = 2; p[1] = (uint8_t)((fr & 1) | (at + k == want && fr != 2 ? 2 : 0) | (fr == 1 && at == 998 * 5 ? 0x40 : 0));
            memcpy(p + 2, pic + at, k);
            if (uvc_payload(&a, p, (int)k + 2)) { got++; same &= a.ready && !memcmp(a.ready, pic, want) == (fr != 3); }
        }
    }
    uint8_t e[2] = { 2, 0 }; if (uvc_payload(&a, e, 2)) got++;                          /* frame 3 is ended by the next toggle */
    CHECK(got == 3 && a.dropped == 1 && a.frames == 3, "UVC: payloads into frames: whole ones out (%d), the spoilt one dropped (%u), the one without EOF ended by the toggle", got, a.dropped);
    uvc_luma(&f, a.ready, luma);
    CHECK(luma[0] == a.ready[0] && luma[1] == a.ready[2] && luma[160 * 120 - 1] == a.ready[160 * 120 * 2 - 2], "UVC: YUY2 to brightness: every other byte");
    (void)same;
}

static void usb_checks(void) {
    struct hid_out o; uint8_t last[8] = { 0 };
    uint8_t a[8] = { 0, 0, 0x04 }, sa[8] = { 0x02, 0, 0x04, 0x3A }, none[8] = { 0 }, roll[8] = { 0, 0, 1, 1, 1, 1, 1, 1 };
    hid_boot_keys(last, a, &o);
    CHECK(o.nkeys == 1 && has_key(&o, 'a', true), "USB keyboard: A pressed");
    hid_boot_keys(last, sa, &o);
    CHECK(o.nkeys == 2 && has_key(&o, KEY_LSHIFT, true) && has_key(&o, KEY_F1, true), "left shift and F1 join it, A stays down");
    hid_boot_keys(last, roll, &o);
    CHECK(o.nkeys == 0, "a rollover report changes nothing");
    hid_boot_keys(last, none, &o);
    CHECK(o.nkeys == 3 && has_key(&o, 'a', false) && has_key(&o, KEY_F1, false) && has_key(&o, KEY_LSHIFT, false), "all let go: three releases");
    uint8_t m[4] = { 1, (uint8_t)-3, 5, 0 };
    hid_boot_mouse(m, 4, &o);
    CHECK(o.nev && o.ev[0].dx == -3 && o.ev[0].dy == 5 && o.ev[0].buttons == 1 && !o.ev[0].abs, "USB mouse: moved -3,+5 with the left button");

    static const uint8_t tablet[] = {                   /* QEMU's usb-tablet */
        0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
        0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00,
        0x26, 0xff, 0x7f, 0x35, 0x00, 0x46, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02, 0x05, 0x01, 0x09, 0x38, 0x15, 0x81,
        0x25, 0x7f, 0x35, 0x00, 0x45, 0x00, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xc0, 0xc0 };
    struct hid_parse hp; struct hid_state hs; memset(&hs, 0, sizeof hs);
    bool ok = hid_parse(&hp, tablet, sizeof tablet);
    uint8_t tr[6] = { 1, 0xff, 0x3f, 0x00, 0x60, 0 };
    hid_report(&hp, &hs, tr, 6, &o);
    CHECK(ok && hp.pointer && hp.absolute && o.nev && o.ev[0].abs && o.ev[0].ax == 0x3fff && o.ev[0].ay == 0x6000 && o.ev[0].buttons == 1,
          "a tablet's report descriptor: absolute X/Y (%u, %u) and its button", o.ev[0].ax, o.ev[0].ay);

    static const uint8_t pen[] = {                      /* a pen digitizer with a report ID: tip, barrel, in range, X 0..4096, Y 0..2048 */
        0x05, 0x0D, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x05, 0x09, 0x20, 0xA1, 0x00, 0x09, 0x42, 0x09, 0x44, 0x09, 0x32, 0x15, 0x00,
        0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x95, 0x05, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x26, 0x00, 0x10, 0x75,
        0x10, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x26, 0x00, 0x08, 0x81, 0x02, 0xC0, 0xC0 };
    ok = hid_parse(&hp, pen, sizeof pen);
    uint8_t pr[6] = { 5, 0x01, 0x00, 0x08, 0x00, 0x04 };
    hid_report(&hp, &hs, pr, 6, &o);
    CHECK(ok && hp.ids && o.nev && o.ev[0].abs && abs((int)o.ev[0].ax - 16383) < 8 && abs((int)o.ev[0].ay - 16383) < 8 && o.ev[0].buttons == 1,
          "a pen: the tip is the button, the middle of the tablet is the middle (%u, %u)", o.ev[0].ax, o.ev[0].ay);

    static const uint8_t media[] = {                    /* consumer keys as a 16-bit array, report 2 */
        0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x19, 0x00, 0x2A, 0x3C, 0x02, 0x15, 0x00, 0x26, 0x3C, 0x02, 0x95, 0x01,
        0x75, 0x10, 0x81, 0x00, 0xC0 };
    ok = hid_parse(&hp, media, sizeof media);
    memset(&hs, 0, sizeof hs);
    uint8_t up[3] = { 2, 0xE9, 0 }, rel[3] = { 2, 0, 0 };
    hid_report(&hp, &hs, up, 3, &o);
    bool pressed = o.nkeys == 1 && has_key(&o, KEY_VOLUP, true);
    hid_report(&hp, &hs, rel, 3, &o);
    CHECK(ok && hp.consumer && pressed && o.nkeys == 1 && has_key(&o, KEY_VOLUP, false), "a keyboard's volume key (consumer page, array): pressed and let go");
    static const uint8_t media_bits[] = {                /* the same as one bit each */
        0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x09, 0xE9, 0x09, 0xEA, 0x09, 0xE2,
        0x81, 0x02, 0x95, 0x05, 0x81, 0x01, 0xC0 };
    ok = hid_parse(&hp, media_bits, sizeof media_bits);
    memset(&hs, 0, sizeof hs);
    uint8_t down2[1] = { 2 };
    hid_report(&hp, &hs, down2, 1, &o);
    CHECK(ok && o.nkeys == 1 && has_key(&o, KEY_VOLDOWN, true), "volume down as a bit of its own");

    static const uint8_t ptp[] = {                      /* a precision touchpad: mouse (ID 1), touchpad (ID 4, two fingers), Input Mode (ID 3) */
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x02, 0x15, 0x00,
        0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81,
        0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0,
        0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x04,
        0x09, 0x22, 0xA1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01,
        0x75, 0x02, 0x25, 0x02, 0x09, 0x51, 0x81, 0x02, 0x75, 0x01, 0x95, 0x04, 0x81, 0x03, 0x05, 0x01, 0x15, 0x00, 0x26, 0xFF,
        0x0F, 0x75, 0x10, 0x09, 0x30, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x81, 0x02, 0xC0,
        0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02,
        0x95, 0x01, 0x75, 0x02, 0x25, 0x02, 0x09, 0x51, 0x81, 0x02, 0x75, 0x01, 0x95, 0x04, 0x81, 0x03, 0x05, 0x01, 0x15, 0x00,
        0x26, 0xFF, 0x0F, 0x75, 0x10, 0x09, 0x30, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x81, 0x02, 0xC0,
        0x05, 0x0D, 0x09, 0x54, 0x25, 0x7F, 0x95, 0x01, 0x75, 0x08, 0x81, 0x02, 0x05, 0x09, 0x09, 0x01, 0x25, 0x01, 0x75, 0x01,
        0x95, 0x01, 0x81, 0x02, 0x95, 0x07, 0x81, 0x03, 0xC0,
        0x05, 0x0D, 0x09, 0x0E, 0xA1, 0x01, 0x85, 0x03, 0x09, 0x22, 0xA1, 0x02, 0x09, 0x52, 0x15, 0x00, 0x25, 0x0A, 0x75, 0x08,
        0x95, 0x01, 0xB1, 0x02, 0xC0, 0xC0 };
    ok = hid_parse(&hp, ptp, sizeof ptp);
    uint8_t mode[8]; int ml = hid_mode_report(&hp, 3, mode, sizeof mode);
    CHECK(ok && hp.touchpad && hp.has_mode && ml == 2 && mode[0] == 3 && mode[1] == 3, "a precision touchpad: found, and its Input Mode report is %d bytes (%02x %02x)", ml, mode[0], mode[1]);
    memset(&hs, 0, sizeof hs);
    uint8_t mrep[4] = { 1, 1, 5, (uint8_t)-2 };         /* mouse mode, before the switch: a click and a move */
    hid_report(&hp, &hs, mrep, 4, &o);
    bool mouse = o.nev && !o.ev[0].touch && o.ev[0].dx == 5 && o.ev[0].dy == -2 && o.ev[0].buttons == 1;
    uint8_t down[13] = { 4, 0x03, 0x00, 0x08, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 1, 0 };   /* finger 1 at 2048,1024 */
    hid_report(&hp, &hs, down, 13, &o);
    bool touched = o.nev && o.ev[0].touch && o.ev[0].z >= 30 && abs((int)o.ev[0].tx - 16387) < 16 && abs((int)o.ev[0].ty - 8197) < 16 && o.ev[0].buttons == 0;
    uint8_t click[13] = { 4, 0x03, 0x00, 0x08, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 1, 1 };
    hid_report(&hp, &hs, click, 13, &o);
    bool clicked = o.ev[0].touch && o.ev[0].buttons == 1;
    uint8_t lift[13] = { 4, 0x01, 0x00, 0x08, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0, 0 };
    hid_report(&hp, &hs, lift, 13, &o);
    CHECK(mouse && touched && clicked && o.ev[0].touch && o.ev[0].z < 30, "its reports: a mouse move before the switch, then a finger down at the middle-left (%u, %u), a click, a lift",
          down[3] ? 16387 : 0, 8197);

    /* two fingers: contact IDs 2 and 1 land on slots 0 and 1; when ID 2 leaves the frame it is lifted */
    memset(&hs, 0, sizeof hs);
    uint8_t two[13] = { 4, 0x0B, 0x00, 0x04, 0x00, 0x04, 0x07, 0x00, 0x0C, 0x00, 0x08, 2, 0 };   /* ID 2 at 1024,1024; ID 1 at 3072,2048 */
    hid_report(&hp, &hs, two, 13, &o);
    bool both = o.nev == 2 && o.ev[0].finger == 0 && o.ev[1].finger == 1 && o.ev[0].z >= 30 && o.ev[1].z >= 30
             && abs((int)o.ev[0].tx - 8195) < 16 && abs((int)o.ev[1].tx - 24584) < 16 && abs((int)o.ev[1].ty - 16390) < 16;
    uint8_t one[13] = { 4, 0x07, 0x00, 0x0D, 0x00, 0x08, 0, 0, 0, 0, 0, 1, 0 };                  /* ID 1 moved on, alone */
    hid_report(&hp, &hs, one, 13, &o);
    bool kept = false, gone = false;
    for (int e = 0; e < o.nev; e++) { kept |= o.ev[e].finger == 1 && o.ev[e].z >= 30 && o.ev[e].tx > 26000; gone |= o.ev[e].finger == 0 && o.ev[e].z == 0; }
    CHECK(both && kept && gone && o.nev == 2, "two fingers at once: each on its own slot, positions kept apart; one leaves the frame and is lifted (%d events)", o.nev);
    uint8_t palm[13] = { 4, 0x06, 0x00, 0x0D, 0x00, 0x08, 0, 0, 0, 0, 0, 1, 0 };                 /* ID 1, no confidence: a palm */
    hid_report(&hp, &hs, palm, 13, &o);
    CHECK(o.nev == 1 && o.ev[0].finger == 1 && o.ev[0].z == 0 && !hs.down, "a contact the pad calls a palm lets go");

    static const uint8_t hybrid[] = {                   /* one finger per report: a frame of two comes as two reports */
        0x05, 0x0D, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x04,
        0x09, 0x22, 0xA1, 0x02, 0x15, 0x00, 0x25, 0x01, 0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01,
        0x75, 0x02, 0x25, 0x03, 0x09, 0x51, 0x81, 0x02, 0x75, 0x01, 0x95, 0x04, 0x81, 0x03, 0x05, 0x01, 0x15, 0x00, 0x26, 0xFF,
        0x0F, 0x75, 0x10, 0x09, 0x30, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x81, 0x02, 0x05, 0x0D, 0x09, 0x30, 0x25, 0x7F, 0x75,
        0x08, 0x81, 0x02, 0xC0,
        0x05, 0x0D, 0x09, 0x54, 0x25, 0x7F, 0x95, 0x01, 0x75, 0x08, 0x81, 0x02, 0x05, 0x09, 0x09, 0x01, 0x25, 0x01, 0x75, 0x01,
        0x95, 0x01, 0x81, 0x02, 0x95, 0x07, 0x81, 0x03, 0xC0 };
    ok = hid_parse(&hp, hybrid, sizeof hybrid);
    memset(&hs, 0, sizeof hs);
    uint8_t ha[9] = { 4, 0x03, 0x00, 0x04, 0x00, 0x04, 100, 2, 0 }, hb[9] = { 4, 0x07, 0x00, 0x0C, 0x00, 0x04, 20, 0, 0 };
    hid_report(&hp, &hs, ha, 9, &o);
    bool first = o.nev == 1 && o.ev[0].finger == 0 && o.ev[0].z > 60;               /* pressure 100 of 127 */
    hid_report(&hp, &hs, hb, 9, &o);
    bool second = o.nev == 1 && o.ev[0].finger == 1 && o.ev[0].z >= 30 && o.ev[0].z < 80 && hs.down == 3 && !hs.left;
    uint8_t hc[9] = { 4, 0x07, 0x00, 0x0C, 0x00, 0x05, 20, 1, 0 };                  /* the next frame: only ID 1 */
    hid_report(&hp, &hs, hc, 9, &o);
    bool swept = o.nev == 2 && hs.down == 2;
    uint8_t hd[9] = { 4, 0, 0, 0, 0, 0, 0, 0, 0 };                                    /* an empty frame: all up */
    hid_report(&hp, &hs, hd, 9, &o);
    CHECK(ok && first && second && swept && !hs.down && o.nev == 1 && o.ev[0].z == 0,
          "hybrid reports: two fingers in two reports, pressure from the pad, the one missing from the next frame lifted, an empty frame lifts the rest");
    uint8_t pk[] = { 0x09, 0x90, 0x45, 0x64, 0x08, 0x80, 0x45, 0x00, 0x0F, 0xF8, 0, 0, 0x04, 0xF0, 0x7E, 0x7F, 0x07, 0x09, 0x01, 0xF7,
                     0x1C, 0xC0, 0x05, 0, 0x0C, 0xC0, 0x05, 0x00 };
    uint8_t bytes[64];
    int n = umidi_decode(pk, sizeof pk, bytes, sizeof bytes);
    static const uint8_t want[] = { 0x90, 0x45, 0x64, 0x80, 0x45, 0x00, 0xF8, 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7, 0xC0, 0x05 };
    CHECK(n == (int)sizeof want && !memcmp(bytes, want, sizeof want), "USB-MIDI in: notes, clock, a sysex and a program change (cable 1 left out): %d bytes", n);
    static const uint8_t stream[] = { 0x90, 0x40, 0x7F, 0x41, 0x7F, 0x90, 0xF8, 0x42, 0x7F, 0xF0, 0x01, 0x02, 0x03, 0xF7, 0xC0, 0x05, 0xE0, 0x00, 0x40 };
    struct umidi_enc e; memset(&e, 0, sizeof e); uint8_t out[64]; int no = 0;
    for (unsigned i = 0; i < sizeof stream; i++) if (umidi_encode(&e, stream[i], out + no)) no += 4;
    static const uint8_t want_pk[] = { 0x09, 0x90, 0x40, 0x7F, 0x09, 0x90, 0x41, 0x7F, 0x0F, 0xF8, 0, 0, 0x09, 0x90, 0x42, 0x7F,
                                       0x04, 0xF0, 0x01, 0x02, 0x06, 0x03, 0xF7, 0, 0x0C, 0xC0, 0x05, 0, 0x0E, 0xE0, 0x00, 0x40 };
    CHECK(no == (int)sizeof want_pk && !memcmp(out, want_pk, sizeof want_pk), "USB-MIDI out: running status spelled out, real time in the middle of a message, sysex in threes (%d bytes)", no);
}

static void undo_checks(void) {
    printf("undo\n");
    CHECK(undo_capacity() >= 1u << 20, "undo has %u KiB", undo_capacity() >> 10);
    /* the tracker: a note in, taken back, put back */
    host_tap(KEY_F2); host_tap(KEY_ESC); run(100); host_tap(KEY_HOME); run(1200);
    struct seq_cell before = seq_pat[seq.edit_pat].cell[0][0];
    host_tap('q'); run(30);
    struct seq_cell after = seq_pat[seq.edit_pat].cell[0][0];
    ctrl_tap('z');
    bool back = !memcmp(&seq_pat[seq.edit_pat].cell[0][0], &before, sizeof before);
    ctrl_tap('y');
    CHECK(back && !memcmp(&seq_pat[seq.edit_pat].cell[0][0], &after, sizeof after), "SEQ: a note entered, Ctrl+Z takes it out, Ctrl+Y puts it back");
    ctrl_tap('z'); run(1200);
    /* WAVE: a pen stroke is one step */
    host_tap(KEY_F3); run(30);
    while (1) { host_tap(KEY_TAB); run(10); if (wave_bank[synth_draw_slot].tab[0] || 1) break; }
    host_tap(KEY_TAB); run(10);                                         /* D → 5 → 6: draw */
    int16_t tab0[WAVE_LEN]; memcpy(tab0, wave_bank[synth_draw_slot].tab, sizeof tab0);
    for (int i = 0; i <= 30; i++) { host_touch(3000 + i * 600, 26000, 80); run(16); }
    host_touch(0, 0, 0); run(30);
    bool drew = memcmp(tab0, wave_bank[synth_draw_slot].tab, sizeof tab0) != 0;
    ctrl_tap('z');
    CHECK(drew && !memcmp(tab0, wave_bank[synth_draw_slot].tab, sizeof tab0), "WAVE: a stroke of the pen, undone in one step");
    run(1200);
    /* a sample's frames: reversed, then back */
    host_tap(KEY_TAB); run(10);                                          /* page 6 → page 8 */
    struct sample *sm = &samples[5]; sm->bits = 16; sm->rate_i = 0; sm->rate = 48000; sm->len = sm->end = 20000; sm->start = 0;
    for (uint32_t i = 0; i < sm->len; i++) ((int16_t *)sm->data)[i] = (int16_t)(i * 3);
    host_tap('6'); run(20);
    uint32_t sum0 = sample_sum(sm);
    host_tap('u'); run(30);
    uint32_t sum1 = sample_sum(sm);
    ctrl_tap('z');
    CHECK(sum1 != sum0 && sample_sum(sm) == sum0, "the sampler: a reverse undone, frame for frame");
    host_tap(KEY_TAB); host_tap('1'); run(1200);
    /* OPERATOR: a parameter */
    host_tap(KEY_F5); run(30);
    static struct fm_patch fb0[FM_PATCHES]; memcpy(fb0, fm_bank, sizeof fb0);
    host_tap(KEY_PGDN); run(30);                                         /* the next algorithm */
    bool changed = memcmp(fb0, fm_bank, sizeof fb0) != 0;
    ctrl_tap('z');
    CHECK(changed && !memcmp(fb0, fm_bank, sizeof fb0), "OPERATOR: a change undone");
    run(1200);
    /* TAPE: a take over what was there, undone and redone */
    host_tap(KEY_F6); host_tap(KEY_ESC); run(100);
    for (int t = 0; t < TAPE_TRACKS; t++) tape.tr[t].arm = false;
    tape.cur = 0; host_tap(KEY_HOME); host_tap('q'); host_tap('r'); host_key('4', true); run(1500); host_key('4', false); host_tap('r'); host_tap(KEY_SPACE); run(300);
    uint32_t first = tape_sum(0);
    run(1200);
    host_tap(KEY_HOME); host_tap('r'); host_key('7', true); run(800); host_key('7', false); host_tap('r'); host_tap(KEY_SPACE); run(300);
    uint32_t second = tape_sum(0);
    ctrl_tap('z');
    uint32_t undone = tape_sum(0);
    ctrl_tap('y');
    CHECK(second != first && undone == first && tape_sum(0) == second, "TAPE: a take over the first, undone (the first is back) and redone");
    run(1200);
    host_tap('e'); host_tap('e'); run(30);
    for (int i = 0; i < 300; i++) tape_work();
    bool gone = tape.tr[0].used == 0;
    ctrl_tap('z');
    CHECK(gone && tape_sum(0) == second, "TAPE: E E erases the track; Ctrl+Z brings it back");
    host_tap('q'); tape_clear_all(); for (int i = 0; i < 300; i++) tape_work();
    to_play(); run(50);
}

static void stick_checks(const char *image) {
    printf("stick (%s)\n", image);
    CHECK(disk.have_tape, "project partition found: %s", disk.status);
    if (!disk.have_tape) return;
    seq.bpm = 133; snprintf(seq.title, sizeof seq.title, "CHECK"); wave_bank[3].tab[10] = 1234;
    struct sample *sm = &samples[0];                                   /* a second of a saw, 16 bit */
    sm->bits = 16; sm->rate_i = 0; sm->rate = sampler_rate_hz(0); sm->len = sm->end = 48000; sm->start = 0;
    for (uint32_t i = 0; i < sm->len; i++) ((int16_t *)sm->data)[i] = (int16_t)((i * 331u) % 60000u - 30000);
    uint32_t sum = sample_sum(sm), len = sm->len;
    sm->len = sm->end = 48000 - 77;                                   /* frames that end partway into a sector */
    sum = sample_sum(sm); len = sm->len;
    CHECK(disk_save_slot(2, "CHECK"), "saved to slot 3");
    disk_rescan();                                                     /* read from the stick, not remembered */
    CHECK(disk.slot_used[2] && !strcmp(disk.slot[2].name, "CHECK"), "its header on the stick is whole after the frames went out (%s)", disk.slot_used[2] ? disk.slot[2].name : "gone");
    seq.bpm = 90; wave_bank[3].tab[10] = 0; sampler_clear(0);
    CHECK(disk_load_slot(2) && seq.bpm == 133 && wave_bank[3].tab[10] == 1234, "loaded back: tempo %u, wave sample %d", seq.bpm, wave_bank[3].tab[10]);
    CHECK(len && samples[0].len == len && sample_sum(&samples[0]) == sum, "with the sample's frames (%u)", samples[0].len);
    CHECK(disk.tape.last_slot == 3, "slot 3 is the one that loads at boot");
    CHECK(disk_delete_slot(2) && !disk.slot_used[2], "deleted");
    /* the stick is bigger than the image: the project partition was grown to its end at boot */
    CHECK(disk.tape_sectors * 512 > 180ull << 20, "the project partition reaches the end of the stick (%u MiB)", (unsigned)(disk.tape_sectors / 2048));
    /* a song out: the first demo, rendered into a WAV on the stick */
    seq_load_demo(0); seq.bpm = 175;
    host_tap(KEY_F7); host_tap(KEY_TAB); run(50);
    uint32_t total = song_length();
    host_tap('e'); run(20);
    CHECK(song.exporting, "SONGS: E starts the export (%u:%02u)", total / 48000 / 60, total / 48000 % 60);
    for (int i = 0; i < 2000 && song.exporting; i++) run(1);
    struct fat_file wf; bool found = fat_find(&disk.fat, song.name, &wf);
    static int16_t head[48000 * 2]; bool ok = found && wf.size == 44 + total * 4 && fat_read(&disk.fat, &wf, 44 + 48000 * 4, head, sizeof head);
    double sq = 0; for (int i = 0; ok && i < 48000 * 2; i++) sq += (double)head[i] * head[i];
    CHECK(ok && !song.exporting && sqrt(sq / 96000) > 1000, "%s: %u bytes, the second second is music (rms %.0f)", song.name, found ? wf.size : 0, sqrt(sq / 96000));
    /* and back in: into sample slot 4, and onto tape track 3 */
    struct fat_entry list[64]; int nl = song_wavs(list, 64), k = -1;
    for (int i = 0; i < nl; i++) if (!memcmp(list[i].name, song.name, 13)) k = i;
    CHECK(k >= 0 && song_import(&list[k], false, 3) && samples[3].len > 48000 * 2, "imported into sample slot 4 (%.1f s)", samples[3].len / (double)samples[3].rate);
    tape_erase(2); for (int i = 0; i < 200; i++) tape_work();
    tape.pos = 0; tape.cur = 2;
    CHECK(song_import(&list[k], true, 2) && tape.tr[2].used >= total - 48, "onto tape track 3 (%u of %u frames)", tape.tr[2].used, total);
    /* a project keeps its tape: saved, the tape wiped, loaded back */
    uint32_t tsum = 2166136261u; for (uint32_t i = 0; i < tape.tr[2].used; i += 3) tsum = (tsum ^ (uint32_t)tape_sample(2, i)) * 16777619u;
    uint32_t tused = tape.tr[2].used;
    CHECK(disk_save_slot(4, "WITH TAPE") && !disk.tape_left_out && disk.slot[4].ext_sectors > 0, "saved with the tape: its frames in an extent (%u KiB)", disk.slot[4].ext_sectors / 2);
    tape_clear_all(); for (int i = 0; i < 400; i++) tape_work();
    CHECK(disk_load_slot(4), "loaded back");
    uint32_t tsum2 = 2166136261u; for (uint32_t i = 0; i < tape.tr[2].used; i += 3) tsum2 = (tsum2 ^ (uint32_t)tape_sample(2, i)) * 16777619u;
    CHECK(tape.tr[2].used == tused && tsum2 == tsum && tape_peak(2, 0, tused) > 0, "the tape came back, frame for frame (%u frames)", tape.tr[2].used);
    disk_delete_slot(4); tape_clear_all(); for (int i = 0; i < 4; i++) host_tap(KEY_TAB); to_play();   /* round the views to PROJECTS */
    run(5000);                                                         /* the export's echoes die away */
}

/* the network stack on a simulated port: DHCP, ARP, ping, UDP, multicast; then a cable with no DHCP server on it */
static const uint8_t grp_mac_link[6] = { 0x01, 0x00, 0x5E, 0x4C, 0x4E, 0x4B };
static const uint8_t srv_mac[6] = { 0x52, 0x55, 0x0A, 0x00, 0x02, 0x02 }, our_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 },
                     x_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x09 }, ff_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint16_t nb16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t nb32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void np16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void np32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint16_t nsum(const uint8_t *p, int n, uint32_t s) {
    for (int i = 0; i + 1 < n; i += 2) s += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (n & 1) s += (uint32_t)p[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t)~s;
}
static int ip_frame(uint8_t *f, const uint8_t *dmac, const uint8_t *smac, uint32_t src, uint32_t dst, uint8_t proto, const uint8_t *pl, int n) {
    memcpy(f, dmac, 6); memcpy(f + 6, smac, 6); np16(f + 12, 0x0800);
    uint8_t *h = f + 14; memset(h, 0, 20); h[0] = 0x45; np16(h + 2, (uint32_t)(20 + n)); h[8] = 64; h[9] = proto; np32(h + 12, src); np32(h + 16, dst);
    np16(h + 10, nsum(h, 20, 0)); memcpy(h + 20, pl, (size_t)n);
    return 14 + 20 + n;
}
static int udp_frame(uint8_t *f, const uint8_t *dmac, const uint8_t *smac, uint32_t src, uint32_t dst, uint16_t sp, uint16_t dp, const void *d, int n) {
    uint8_t u[1500]; np16(u, sp); np16(u + 2, dp); np16(u + 4, (uint32_t)(8 + n)); np16(u + 6, 0); memcpy(u + 8, d, (size_t)n);
    return ip_frame(f, dmac, smac, src, dst, 17, u, 8 + n);
}
static int arp_frame(uint8_t *f, int op, const uint8_t *smac, uint32_t spa, uint32_t tpa, const uint8_t *dmac) {
    memcpy(f, dmac, 6); memcpy(f + 6, smac, 6); np16(f + 12, 0x0806);
    uint8_t *a = f + 14; np16(a, 1); np16(a + 2, 0x0800); a[4] = 6; a[5] = 4; np16(a + 6, (uint32_t)op);
    memcpy(a + 8, smac, 6); np32(a + 14, spa); memset(a + 18, 0, 6); np32(a + 24, tpa);
    return 42;
}
static int dhcp_frame(uint8_t *f, int type, uint32_t xid, uint32_t yi) {
    uint8_t b[300] = { 0 }; b[0] = 2; b[1] = 1; b[2] = 6; np32(b + 4, xid); np32(b + 16, yi); memcpy(b + 28, our_mac, 6); np32(b + 236, 0x63825363);
    int o = 240;
    b[o++] = 53; b[o++] = 1; b[o++] = (uint8_t)type;
    b[o++] = 1; b[o++] = 4; np32(b + o, 0xFFFFFF00u); o += 4; b[o++] = 3; b[o++] = 4; np32(b + o, NET_IP(10, 0, 2, 2)); o += 4;
    b[o++] = 54; b[o++] = 4; np32(b + o, NET_IP(10, 0, 2, 2)); o += 4; b[o++] = 51; b[o++] = 4; np32(b + o, 86400); o += 4; b[o++] = 255;
    return udp_frame(f, ff_mac, srv_mac, NET_IP(10, 0, 2, 2), 0xFFFFFFFFu, 67, 68, b, 300);
}
/* the next frame the stack sent of a kind: ethertype, and for IPv4 the protocol (0: any) and UDP port (0: any) */
static int next_sent(uint8_t *f, uint16_t type, uint8_t proto, uint16_t port) {
    int n;
    while ((n = host_net_sent(f, 1536)) > 0) {
        if (nb16(f + 12) != type) continue;
        if (type == 0x0806) return n;
        int hl = (f[14] & 15) * 4;
        if (proto && f[23] != proto) continue;
        if (port && nb16(f + 14 + hl + 2) != port) continue;
        return n;
    }
    return 0;
}
static bool udp_sum_ok(const uint8_t *f) {                            /* the pseudo header and the datagram sum to zero */
    int hl = (f[14] & 15) * 4; const uint8_t *u = f + 14 + hl; int ul = nb16(u + 4);
    uint8_t ph[12]; memcpy(ph, f + 26, 8); ph[8] = 0; ph[9] = 17; np16(ph + 10, (uint32_t)ul);
    uint32_t s = 0; for (int i = 0; i < 12; i += 2) s += (uint32_t)(ph[i] << 8 | ph[i + 1]);
    return nsum(u, ul, s) == 0;
}
static uint32_t got_src; static int got_len; static char got[64]; static uint32_t got_dst;
static void got_udp(uint32_t src, uint16_t sport, uint32_t dst, const uint8_t *d, int len, uint64_t now) {
    (void)sport; (void)now; got_src = src; got_dst = dst; got_len = len; memcpy(got, d, (size_t)MIN(len, 63)); got[MIN(len, 63)] = 0;
}
static void net_checks(void) {
    printf("network\n");
    static uint8_t f[1536];
    host_net(true, true); net_init(); run(300);
    int n = next_sent(f, 0x0800, 17, 67);
    uint32_t xid = n ? nb32(f + 14 + 20 + 8 + 4) : 0;
    CHECK(n && !memcmp(f, ff_mac, 6) && f[14 + 20 + 8 + 242] == 1, "cable in: a DHCP DISCOVER, broadcast");
    host_net_inject(f, dhcp_frame(f, 2, xid, NET_IP(10, 0, 2, 15))); run(20);
    n = next_sent(f, 0x0800, 17, 67);
    const uint8_t *o = f + 14 + 20 + 8 + 240; bool asks = false;
    for (int i = 0; n && i < 60 && o[i] != 255; i += 2 + o[i + 1]) if (o[i] == 50 && nb32(o + i + 2) == NET_IP(10, 0, 2, 15)) asks = true;
    CHECK(n && o[2] == 3 && asks, "the OFFER taken: a REQUEST for 10.0.2.15");
    host_net_inject(f, dhcp_frame(f, 5, xid, NET_IP(10, 0, 2, 15))); run(20);
    CHECK(net.phase == NET_READY && net.ip == NET_IP(10, 0, 2, 15) && net.gw == NET_IP(10, 0, 2, 2) && net.mask == 0xFFFFFF00u && !net.link_local,
          "the ACK: 10.0.2.15/24, router 10.0.2.2 (%s)", net.status);
    while (host_net_sent(f, 1536)) { }
    host_net_inject(f, arp_frame(f, 1, srv_mac, NET_IP(10, 0, 2, 2), NET_IP(10, 0, 2, 15), ff_mac)); run(20);
    n = next_sent(f, 0x0806, 0, 0);
    CHECK(n && nb16(f + 20) == 2 && !memcmp(f, srv_mac, 6) && !memcmp(f + 22, our_mac, 6) && nb32(f + 28) == NET_IP(10, 0, 2, 15), "ARP: asked for our address, it answers");
    uint8_t echo[16] = { 8, 0, 0, 0, 0x12, 0x34, 0, 7, 'h', 'e', 'l', 'l', 'o', '!', '!', '!' };
    np16(echo + 2, nsum(echo, 16, 0));
    host_net_inject(f, ip_frame(f, our_mac, srv_mac, NET_IP(10, 0, 2, 2), NET_IP(10, 0, 2, 15), 1, echo, 16)); run(20);
    n = next_sent(f, 0x0800, 1, 0);
    CHECK(n && f[34] == 0 && !memcmp(f + 38, echo + 4, 12) && nsum(f + 34, 16, 0) == 0 && nsum(f + 14, 20, 0) == 0 && !memcmp(f, srv_mac, 6),
          "ping: answered with the same bytes, both checksums right");
    net_listen(5000, got_udp); got_len = -1;
    host_net_inject(f, udp_frame(f, our_mac, srv_mac, NET_IP(10, 0, 2, 2), NET_IP(10, 0, 2, 15), 1234, 5000, "abc", 3)); run(20);
    CHECK(got_len == 3 && !strcmp(got, "abc") && got_src == NET_IP(10, 0, 2, 2), "UDP in: to port 5000, handed over");
    bool now_sent = net_send(NET_IP(10, 0, 2, 9), 5001, 6000, "xyz", 3); run(20);
    n = next_sent(f, 0x0806, 0, 0);
    bool asked = n && nb16(f + 20) == 1 && nb32(f + 38) == NET_IP(10, 0, 2, 9);
    host_net_inject(f, arp_frame(f, 2, x_mac, NET_IP(10, 0, 2, 9), NET_IP(10, 0, 2, 15), our_mac)); run(20);
    n = next_sent(f, 0x0800, 17, 6000);
    CHECK(!now_sent && asked && n && !memcmp(f, x_mac, 6) && !memcmp(f + 42, "xyz", 3) && udp_sum_ok(f), "UDP out to a new host: ARP first, then the datagram (its checksum right)");
    net_listen(5007, got_udp); got_len = -1;
    net_join(NET_IP(239, 0, 0, 7)); run(20);
    n = next_sent(f, 0x0800, 2, 0);
    static const uint8_t grp_mac[6] = { 0x01, 0x00, 0x5E, 0x00, 0x00, 0x07 };
    bool igmp = n && !memcmp(f, grp_mac, 6) && f[22] == 1 && f[14 + 24] == 0x16 && nb32(f + 14 + 24 + 4) == NET_IP(239, 0, 0, 7);
    host_net_inject(f, udp_frame(f, grp_mac, srv_mac, NET_IP(10, 0, 2, 2), NET_IP(239, 0, 0, 7), 5007, 5007, "link", 4)); run(20);
    bool heard = got_len == 4 && got_dst == NET_IP(239, 0, 0, 7);
    net_send(NET_IP(239, 0, 0, 7), 5007, 5007, "peer", 4); run(20);
    n = next_sent(f, 0x0800, 17, 5007);
    CHECK(igmp && heard && n && !memcmp(f, grp_mac, 6) && f[22] == 1, "a multicast group: joined (IGMP), heard, sent to (its MAC, TTL 1)");
    /* a cable to another computer, no DHCP: after six seconds an address of its own, probed first */
    host_net(true, true); net_init(); run(6500);
    CHECK(net.phase == NET_PROBING, "no DHCP answer: an address of its own, being checked (%s)", net.status);
    n = next_sent(f, 0x0806, 0, 0); while (n && nb32(f + 28) != 0) n = next_sent(f, 0x0806, 0, 0);
    uint32_t first = n ? nb32(f + 38) : 0;
    host_net_inject(f, arp_frame(f, 2, x_mac, first, first, ff_mac)); run(20);      /* somebody has it */
    run(4500);
    CHECK(first >> 16 == 0xA9FE && net.phase == NET_READY && net.link_local && net.ip >> 16 == 0xA9FE && net.ip != first,
          "taken by another machine: the next one (%s)", net.status);
    host_net(false, false); run(1200);
    CHECK(net.phase == NET_NONE, "the port gone: no network");
}

/* Ableton Link against a peer played here byte for byte: our state and its format, the answer to its own, the
   measurement of its session's clock (pongs with a clock 10 s ahead), joining it, its tempo and its start, our tempo back */
static uint64_t lg64(const uint8_t *p) { return (uint64_t)nb32(p) << 32 | nb32(p + 4); }
static void lp64(uint8_t *p, uint64_t v) { np32(p, (uint32_t)(v >> 32)); np32(p + 4, (uint32_t)v); }
static const uint8_t *lentry(const uint8_t *p, int n, const char *key, uint32_t *size) {   /* an entry of a payload, 0 if absent */
    for (const uint8_t *e = p; e + 8 <= p + n; e += 8 + nb32(e + 4)) if (!memcmp(e, key, 4)) { *size = nb32(e + 4); return e + 8; }
    return 0;
}
static int peer_alive(uint8_t *b, int type, uint64_t upb, uint64_t bo, uint64_t to, bool playing, uint64_t beats, uint64_t ts) {
    memcpy(b, "_asdp_v\x01", 8); b[8] = (uint8_t)type; b[9] = 5; b[10] = b[11] = 0; memcpy(b + 12, "PEERSESS", 8);
    int n = 20;
    memcpy(b + n, "tmln", 4); np32(b + n + 4, 24); lp64(b + n + 8, upb); lp64(b + n + 16, bo); lp64(b + n + 24, to); n += 32;
    memcpy(b + n, "sess", 4); np32(b + n + 4, 8); memcpy(b + n + 8, "PEERSESS", 8); n += 16;
    memcpy(b + n, "stst", 4); np32(b + n + 4, 17); b[n + 8] = playing; lp64(b + n + 9, beats); lp64(b + n + 17, ts); n += 25;
    memcpy(b + n, "mep4", 4); np32(b + n + 4, 6); np32(b + n + 8, NET_IP(169, 254, 1, 2)); np16(b + n + 12, 20809); n += 14;
    return n;
}
static void link_checks(void) {
    printf("link\n");
    static uint8_t f[1536], b[512];
    host_net(true, true); net_init(); run(11000);                        /* a cable, nobody else: link-local */
    bool addr = net.phase == NET_READY && net.link_local;
    while (host_net_sent(f, 1536)) { }
    uint16_t bpm0 = seq.bpm;
    link_enable(true); run(300);
    int n = next_sent(f, 0x0800, 17, 20808);
    const uint8_t *m = f + 42; int ml = n ? nb16(f + 38) - 8 : 0; uint32_t sz = 0;
    const uint8_t *tln = n ? lentry(m + 20, ml - 20, "tmln", &sz) : 0, *mep = n ? lentry(m + 20, ml - 20, "mep4", &sz) : 0;
    uint64_t upb = tln ? lg64(tln) : 0;
    CHECK(addr && n && !memcmp(f, "\x01\x00\x5e\x4c\x4e\x4b", 6) && !memcmp(m, "_asdp_v\x01", 8) && m[8] == 1 && m[9] == 5 && tln && mep &&
          upb == (uint64_t)(60000000 + bpm0 / 2) / bpm0 && nb32(mep) == net.ip && nb16(mep + 4) == 20809,
          "Link on: its state multicast (alive, TTL 5): a timeline at %u BPM, its session, the measurement endpoint", bpm0);
    /* a peer: 100 BPM, its session's clock 10 s ahead of ours; it says who it is first (ARP), as it would */
    host_net_inject(f, arp_frame(f, 1, x_mac, NET_IP(169, 254, 1, 2), NET_IP(169, 254, 1, 2), ff_mac)); run(5);
    uint64_t now_us = host_now_ms * 1000, ahead = 10000000;
    int pl = peer_alive(b, 1, 600000, 0, now_us + ahead - 5000000, false, 0, 0);
    host_net_inject(f, udp_frame(f, grp_mac_link, x_mac, NET_IP(169, 254, 1, 2), NET_IP(224, 76, 78, 75), 20808, 20808, b, pl)); run(20);
    bool answered = false;                                               /* the answer, straight to it */
    while ((n = next_sent(f, 0x0800, 17, 20808)) > 0) answered |= nb32(f + 30) == NET_IP(169, 254, 1, 2) && f[42 + 8] == 2;
    int pings = 0, joined_at = -1;
    for (int t = 0; t < 400 && joined_at < 0; t++) {                   /* its side of the ping-pong */
        run(1);
        while ((n = next_sent(f, 0x0800, 17, 20809)) > 0) {
            const uint8_t *q = f + 42; int ql = nb16(f + 38) - 8;
            if (memcmp(q, "_link_v\x01", 8) || q[8] != 1) continue;
            pings++;
            uint8_t r[128]; int rn = 9;
            memcpy(r, "_link_v\x01", 8); r[8] = 2;
            memcpy(r + rn, "sess", 4); np32(r + rn + 4, 8); memcpy(r + rn + 8, "PEERSESS", 8); rn += 16;
            memcpy(r + rn, "__gt", 4); np32(r + rn + 4, 8); lp64(r + rn + 8, host_now_ms * 1000 + ahead); rn += 16;
            memcpy(r + rn, q + 9, (size_t)(ql - 9)); rn += ql - 9;
            host_net_inject(f, udp_frame(f, our_mac, x_mac, NET_IP(169, 254, 1, 2), net.ip, 20809, 20809, r, rn));
        }
        if (lnk.bpm_q16 >> 16 == 100) joined_at = t;
    }
    CHECK(answered && pings > 50 && joined_at >= 0 && seq.bpm == 100, "its alive answered; its clock measured (%d pings), ahead: its session joined, 100 BPM", pings);
    pl = peer_alive(b, 2, 600000, 0, now_us + ahead - 5000000, true, 0, host_now_ms * 1000 + ahead);
    host_net_inject(f, udp_frame(f, our_mac, x_mac, NET_IP(169, 254, 1, 2), net.ip, 20808, 20808, b, pl)); run(20);
    bool armed = !seq.playing;
    run(3000);
    CHECK(lnk.playing && armed && seq.playing, "it starts: the sequencer comes in on the next bar, not at once");
    while (host_net_sent(f, 1536)) { }
    seq.bpm = 120; run(300);
    n = next_sent(f, 0x0800, 17, 20808);
    tln = n ? lentry(f + 42 + 20, nb16(f + 38) - 8 - 20, "tmln", &sz) : 0;
    CHECK(tln && lg64(tln) == 500000 && lg64(tln + 8) > 0 && lnk.bpm_q16 >> 16 == 120, "our tempo changed: a timeline at 120 BPM, ranked above its (beat origin %lld)", tln ? (long long)lg64(tln + 8) : -1);
    seq_play(false); run(50); link_enable(false); run(20);
    bool bye = false;
    while ((n = next_sent(f, 0x0800, 17, 20808)) > 0) bye |= f[42 + 8] == 3;
    CHECK(bye, "Link off: a bye-bye");
    seq.bpm = bpm0; host_net(false, false); run(1200);
}

/* installing: the stick copied onto a second drive (an image with a GPT on it, as a laptop's disk), made to boot, its
   projects along; ERASE has to be typed; afterwards the drive the machine started from is the one used */
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void type_word(const char *w) { for (; *w; w++) host_tap((uint8_t)*w); }
static void install_checks(void) {
    printf("install\n");
    if (!disk.have_tape) { CHECK(false, "no stick to install from"); return; }
    const char *tpath = "build/host/target.img";
    int fd = open(tpath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    uint64_t secs = (600ull << 20) / 512;
    bool made = fd >= 0 && ftruncate(fd, (off_t)(secs * 512)) == 0;
    uint8_t s[512] = { 0 };
    s[446 + 4] = 0xEE; s[446 + 8] = 1; s[446 + 12] = 0xFF; s[446 + 13] = 0xFF; s[446 + 14] = 0xFF; s[446 + 15] = 0xFF; s[510] = 0x55; s[511] = 0xAA;
    made &= pwrite(fd, s, 512, 0) == 512;
    memset(s, 0, 512); memcpy(s, "EFI PART", 8); s[72] = 2; s[80] = 128; s[84] = 128;
    made &= pwrite(fd, s, 512, 512) == 512 && pwrite(fd, s, 512, (off_t)((secs - 1) * 512)) == 512;   /* and its backup at the end */
    memset(s, 0, 512); s[0] = 0x28; s[128] = 0xA2;                           /* two partitions in its table */
    made &= pwrite(fd, s, 512, 1024) == 512;
    if (fd >= 0) close(fd);
    int d = host_disk_add(tpath);
    char desc[40]; install_describe(d, desc, sizeof desc);
    CHECK(made && d == 1 && !strcmp(desc, "GPT, 2 partitions"), "a second drive: %s", desc);
    CHECK(disk_save_slot(1, "INSTALLED"), "a project on the stick to take along");
    host_tap(KEY_F7); for (int i = 0; i < 4; i++) host_tap(KEY_TAB); run(100);             /* FILE, its INSTALL view */
    host_tap('i'); type_word("wrong"); host_tap(KEY_ENTER); run(100);
    bool refused = !inst.running && !inst.done;
    host_tap('i'); type_word("erase"); host_tap(KEY_ENTER); run(20);
    bool started = inst.running || inst.done;
    for (int i = 0; i < 60000 && inst.running; i += 50) run(50);
    CHECK(refused && started && inst.done && !inst.failed, "ERASE typed, it installs (%s)", inst.status);
    uint8_t m[512], t[512], u[512]; bool same_gap = true;
    plat_blk_read(1, 0, 1, m);
    for (int i = 1; i < 64; i++) { plat_blk_read(0, (uint64_t)i, 1, t); plat_blk_read(1, (uint64_t)i, 1, u); same_gap &= !memcmp(t, u, 512); }
    bool fat_ok = false, tape_ok = false;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = m + 446 + i * 16;
        if (e[4] == 0x0C && le32(e + 8) == disk.fat_lba) fat_ok = true;
        if (e[4] == 0x7F && le32(e + 8) == disk.tape_lba && le32(e + 12) == secs - disk.tape_lba) tape_ok = true;
    }
    uint32_t sig = le32(m + 440);
    plat_blk_read(1, secs - 1, 1, t);
    bool wiped = true; for (int i = 0; i < 512; i++) wiped &= !t[i];
    CHECK(fat_ok && tape_ok && sig && same_gap && wiped && m[510] == 0x55,
          "its partition table: the boot partition as on the stick, the projects to the end of the disk, signature %08x; Limine's stage copied; the old GPT backup gone", sig);
    host_boot_id = sig;                                                        /* started from it: its projects are used */
    disk_rescan();
    bool from_target = disk.boot_drive == 1 && disk.slot_used[1] && !strcmp(disk.slot[1].name, "INSTALLED");
    host_boot_id = 0; host_disk_remove(1); disk_rescan();
    CHECK(from_target && disk.boot_drive == 0, "started from the disk, the disk's projects are the ones in use; the stick again once it is gone");
    disk_delete_slot(1);
    host_tap(KEY_TAB); to_play(); run(50);
}

/* BARE.UPD with both kernels: written into the slots the stick doesn't boot, limine.conf switched to them; a 1.0
   stick (one HB64.ELF) gets its second 64-bit slot made on the way, from a file under the old name, HOMEBREW.UPD */
static bool file_is(const char *path, const uint8_t *want, uint32_t n) {
    struct fat_file f; static uint8_t got[400000];
    return fat_find(&disk.fat, path, &f) && f.size >= n && n <= sizeof got && fat_read(&disk.fat, &f, 0, got, n) && !memcmp(got, want, n);
}
static bool conf_has(const char *what) {                                  /* on a path line */
    struct fat_file f; static char t[4097];
    if (!fat_find(&disk.fat, "BOOT/LIMINE/LIMINE.CONF", &f) || f.size > 4096 || !fat_read(&disk.fat, &f, 0, t, f.size)) return false;
    t[f.size] = 0;
    char p[40]; snprintf(p, sizeof p, "/BOOT/%s", what);
    return strstr(t, p) != 0;
}
static bool put_update(const char *name, uint32_t version, const uint8_t *k32, uint32_t n32, const uint8_t *k64, uint32_t n64) {
    static uint8_t u[32 + 700000];
    uint32_t h[6] = { version, n32, crc32(k32, n32), version, n64, crc32(k64, n64) };
    memcpy(u, "HBUPD002", 8); memcpy(u + 8, h, sizeof h); memcpy(u + 32, k32, n32); memcpy(u + 32 + n32, k64, n64);
    struct fat_file f;
    return fat_create(&disk.fat, "", name, 32 + n32 + n64, &f) && fat_write_inplace(&disk.fat, &f, 0, u, 32 + n32 + n64);
}
static void update_checks(void) {
    printf("updates\n");
    if (!disk.have_boot_fat) return;
    static uint8_t k32[300000], k64[400000];
    for (uint32_t i = 0; i < sizeof k32; i++) k32[i] = (uint8_t)(i * 7);
    for (uint32_t i = 0; i < sizeof k64; i++) k64[i] = (uint8_t)(i * 13 ^ 0x5A);
    char msg[96] = "";
    CHECK(conf_has("HB_A.ELF") && conf_has("H64_A.ELF"), "a fresh stick boots slot A, 32 and 64 bit");
    bool ok = put_update("BARE.UPD", 2000000000u, k32, sizeof k32, k64, sizeof k64) && disk_check_update(1, msg, sizeof msg);
    CHECK(ok, "BARE.UPD with both kernels applied: %s", msg);
    CHECK(conf_has("HB_B.ELF") && conf_has("H64_B.ELF") && file_is("BOOT/HB_B.ELF", k32, sizeof k32) && file_is("BOOT/H64_B.ELF", k64, sizeof k64),
          "both kernels in their B slots, and limine.conf points there");
    msg[0] = 0;
    CHECK(!disk_check_update(2000000000u, msg, sizeof msg) && conf_has("HB_B.ELF"), "the same build again is left alone");
    /* a 1.0 stick: limine.conf names HB64.ELF */
    struct fat_file cf; static char t[4096];
    ok = fat_find(&disk.fat, "BOOT/LIMINE/LIMINE.CONF", &cf) && cf.size <= sizeof t && fat_read(&disk.fat, &cf, 0, t, cf.size);
    char *at = ok ? memmem(t, cf.size, "/BOOT/H64_B.ELF", 15) : 0;                  /* the path line, not the comment */
    if (at) { at += 6; memcpy(at, "HB64.ELF", 8); memmove(at + 8, at + 9, (size_t)(t + cf.size - at - 9)); t[cf.size - 1] = '\n'; fat_write_inplace(&disk.fat, &cf, 0, t, cf.size); }
    for (uint32_t i = 0; i < sizeof k64; i++) k64[i] = (uint8_t)(i * 29 + 3);
    ok = at && conf_has("HB64.ELF") && put_update("HOMEBREW.UPD", 2000000001u, k32, sizeof k32, k64, sizeof k64) && disk_check_update(2000000000u, msg, sizeof msg);
    CHECK(ok && conf_has("H64_B.ELF") && !conf_has("HB64.ELF") && conf_has("HB_A.ELF") && file_is("BOOT/H64_B.ELF", k64, sizeof k64),
          "a 1.0 stick and the old file name, beside an older BARE.UPD: its second 64-bit slot made, the kernel in it, limine.conf moved over (%s)", msg);
    struct fat_file uf;
    if (fat_find(&disk.fat, "HOMEBREW.UPD", &uf)) fat_create(&disk.fat, "", "HOMEBREW.UPD", 0, &uf);
    if (fat_find(&disk.fat, "BARE.UPD", &uf)) fat_create(&disk.fat, "", "BARE.UPD", 0, &uf);
}

/* the rhythm section: R starts it; every hit lands on the pattern's grid, to the sample */
static void rhythm_checks(void) {
    printf("rhythm\n");
    to_play(); host_tap(KEY_ESC); run(300);
    uint16_t bpm = seq.bpm; seq.bpm = 60;
    rhythm.pattern = 2; rhythm.bass = false; rhythm.mute = 6;           /* DISCO: four equal kicks a bar, alone */
    HOST_SET_ECHO(false); run(300);
    host_wav_open("/tmp/hb-check-rhythm.wav");
    host_tap('r'); run_capture(6000);
    host_wav_close();
    CHECK(rhythm.playing, "R starts the rhythm (pattern %s)", rhythm_names[rhythm.pattern]);
    host_tap('r'); run(300);
    CHECK(!rhythm.playing, "R stops it");
    seq.bpm = bpm; rhythm.bass = true; rhythm.mute = 0; HOST_SET_ECHO(true);
    FILE *f = fopen("/tmp/hb-check-rhythm.wav", "rb"); fseek(f, 44, SEEK_SET);
    static int16_t d[48000 * 8 * 2]; long n = (long)fread(d, 4, 48000 * 8, f); fclose(f); remove("/tmp/hb-check-rhythm.wav");
    double on[64]; int no = 0; long quiet = 48 * 30;
    for (long i = 0; i < n && no < 64; i++) { int a = abs(d[2 * i]); if (a < 150) quiet++; else { if (a > 1500 && quiet > 48 * 30) on[no++] = i / 48000.0; if (a > 1500) quiet = 0; } }
    double step = 60.0 / 60 / 4, worst = 0;                              /* a sixteenth at 60 BPM */
    for (int i = 1; i < no; i++) { double k = (on[i] - on[0]) / step, dev = fabs(k - floor(k + 0.5)) * step; worst = fmax(worst, dev); }
    if (getenv("CHECK_DEBUG")) { for (int i = 0; i < no; i++) printf("    onset %.4f\n", on[i]); }
    CHECK(no >= 6 && worst < 0.0002, "every kick on the sixteenth grid: %d kicks, worst %.3f ms off", no, worst * 1000);
}

/* the key layers: F keys switch pages, Shift+key is a function (and plays nothing), the volume keys */
static void layer_checks(void) {
    printf("key layers\n");
    to_play(); host_tap(KEY_ESC); run(300);
    bool echo = audio_echo(); int voices = synth_active_voices();
    host_shift_tap('e'); run(30);
    CHECK(audio_echo() != echo && synth_active_voices() == voices, "⇧E toggles echo and plays no note");
    host_shift_tap('e'); run(30);
    int v = audio_volume_db();
    host_tap(KEY_VOLDOWN); run(20);
    CHECK(audio_volume_db() == v - 2, "the volume-down key lowers the volume 2 dB (%d → %d)", v, audio_volume_db());
    host_shift_tap('='); run(20);
    CHECK(audio_volume_db() == v, "⇧= raises it back");
    host_tap(KEY_F2); run(20);
    for (int d = 1; d >= 0; d--) {                                      /* 2 is also a note key on SEQ */
        HOST_DEMO(d); run(40);
        CHECK(seq.playing && strcmp(seq.title, d ? "HARBOR" : "BARE METAL") == 0, "SEQ: ⇧%d starts %s", d + 1, seq.title);
    }
    host_tap(KEY_SPACE); to_play(); run(50);
    host_shift_tap('/'); run(50);
    CHECK(ui_help, "⇧? shows the keys");
    host_tap(KEY_ESC); run(50);
    CHECK(!ui_help, "Esc closes them");
}

/* the TOUCH page (F9): keys put fingers on the pads; two pads joined through the body make the circuit sing */
static void touch_checks(void) {
    printf("touch\n");
    to_play(); host_tap(KEY_ESC); run(300);
    HOST_SET_ECHO(false);
    host_tap(KEY_F9); run(200);
    struct heard quiet = listen(200);
    host_key(KEY_LSHIFT, true); host_key('a', true); host_key('s', true); host_key(KEY_LSHIFT, false); run(100);
    struct heard firm = listen(400);
    host_key('a', false); host_key('s', false); run(300);
    struct heard after = listen(300);
    CHECK(quiet.rms < 5 && firm.rms > 2000 && firm.hz > 500 && after.rms < 20,
          "TOUCH: OUT and IN- pressed hard make a tone (%.0f Hz, rms %.0f); silence before and after (%.1f, %.1f)", firm.hz, firm.rms, quiet.rms, after.rms);
    host_key('a', true); host_key('s', true); run(150);
    struct heard early = listen(250);
    run(900);
    struct heard late = listen(250);
    host_key('a', false); host_key('s', false); run(300);
    CHECK(early.rms > 1000 && late.hz > early.hz * 3, "held longer, the keys press harder: the pitch climbs (%.0f to %.0f Hz)", early.hz, late.hz);
    host_tap(KEY_SPACE); host_tap('a'); host_tap('s'); run(200);
    struct heard held = listen(200);
    host_tap(KEY_SPACE); run(300);
    struct heard let_go = listen(200);
    CHECK(held.rms > 2000 && let_go.rms < 20, "SPACE holds the fingers after the keys go up (rms %.0f), and lets them go (%.1f)", held.rms, let_go.rms);
    mix.ch[CH_TOUCH].mute = true;
    host_key(KEY_LSHIFT, true); host_key('a', true); host_key('s', true); host_key(KEY_LSHIFT, false); run(100);
    struct heard muted = listen(200);
    host_key('a', false); host_key('s', false); mix.ch[CH_TOUCH].mute = false; run(300);
    CHECK(muted.rms < 5, "the mixer's TOUCH channel mutes it (rms %.1f)", muted.rms);
    host_finger(0, 4096, 8192, 120, 0); host_finger(1, 12288, 8192, 120, 0); run(100);   /* two fingers on the touchpad */
    struct heard pad2 = listen(300);
    host_finger(0, 0, 0, 0, 0); host_finger(1, 0, 0, 0, 0); run(300);
    CHECK(pad2.rms > 2000 && pad2.hz > 20, "two touchpad fingers on OUT and IN- play it (%.0f Hz)", pad2.hz);
    /* saved with the project: the knobs, and the TOUCH channel in the mixer's new chunk; a 2.1 project still loads */
    static uint8_t blob[PROJECT_MAX];
    touch.knob[TK_RANGE] = 77; touch.hum60 = true; mix.ch[CH_TOUCH].db = -12; mix.ch[CH_TOUCH].reverb = 33;
    size_t n = project_save(blob, sizeof blob, 0, true);
    touch_init(); mix.ch[CH_TOUCH].db = 0; mix.ch[CH_TOUCH].reverb = 0;
    project_load(blob, n);
    bool kept = touch.knob[TK_RANGE] == 77 && touch.hum60 && mix.ch[CH_TOUCH].db == -12 && mix.ch[CH_TOUCH].reverb == 33;
    struct __attribute__((packed)) { int8_t db[6], pan[6]; uint8_t mute[6], solo[6], echo[6], in_mono; } old;
    memset(&old, 0, sizeof old); old.db[CH_SEQ] = -7; old.pan[CH_TAPE] = 20; old.echo[CH_PLAY] = 50;
    uint32_t l; n = 0;
    memcpy(blob, "HBPJ", 4); l = 1; memcpy(blob + 4, &l, 4); blob[8] = '2'; n = 9;
    memcpy(blob + n, "MIXR", 4); l = sizeof old; memcpy(blob + n + 4, &l, 4); memcpy(blob + n + 8, &old, sizeof old); n += 8 + sizeof old;
    memcpy(blob + n, "END ", 4); l = 0; memcpy(blob + n + 4, &l, 4); n += 8;
    project_load(blob, n);
    CHECK(kept && mix.ch[CH_SEQ].db == -7 && mix.ch[CH_TAPE].pan == 20 && mix.ch[CH_PLAY].echo == 50 && mix.ch[CH_TOUCH].db == -12,
          "saved: the knobs and the TOUCH channel; a 2.1 project's six channels still load where they were");
    touch_init(); mix_init(); HOST_SET_ECHO(true);
    to_play(); run(50);
}

/* a Synaptics pad in absolute mode: the strum plate on PLAY, the pointer elsewhere */
static void pad_checks(void) {
    printf("touchpad\n");
    to_play(); host_tap(KEY_ESC); run(300);
    host_key('4', true); run(50);                                       /* hold C major */
    int hits = 0;
    for (int i = 0; i <= 40; i++) { host_touch(i * 32767 / 40, 20000, 90); run(12); }
    for (int r = 0; r < 2; r++) for (int z = 0; z < OMNI_ZONES; z++) if (omni.zone_hit_ms[r][z] && omni_zone_keys[r][z] != ' ') hits++;
    CHECK(hits >= 20, "PLAY: a finger across the pad strums every zone, low to high (%d of 21)", hits);
    host_touch(0, 0, 0); run(50);
    memset(omni.zone_hit_ms, 0, sizeof omni.zone_hit_ms); hits = 0;   /* two fingers at once, from both ends to the middle */
    for (int i = 0; i <= 20; i++) { host_finger(0, i * 16300 / 20, 20000, 90, 0); host_finger(1, 32767 - i * 16300 / 20, 20000, 90, 0); run(12); }
    for (int r = 0; r < 2; r++) for (int z = 0; z < OMNI_ZONES; z++) if (omni.zone_hit_ms[r][z] && omni_zone_keys[r][z] != ' ') hits++;
    int held = pad.n;
    host_finger(0, 0, 0, 0, 0); host_finger(1, 0, 0, 0, 0); run(50);
    CHECK(hits >= 20 && held == 2 && pad.n == 0, "two fingers at once, one from each end: every zone strummed (%d of 21)", hits);
    host_key('4', false); host_tap(KEY_ESC); run(200);
    host_tap(KEY_F2); run(30);
    int x0 = ptr.x;
    for (int i = 0; i <= 10; i++) { host_touch(8000 + i * 1000, 16000, 80); run(12); }
    host_touch(0, 0, 0); run(20);
    CHECK(ptr.x > x0 + 50, "SEQ: a finger moving right moves the pointer (%d → %d)", x0, ptr.x);
    /* WAVE: the pad is a pen; on page 6 a finger low on the pad draws the wave low */
    host_tap(KEY_F3); host_tap(KEY_TAB); host_tap(KEY_TAB); run(50);
    int16_t *t = wave_bank[synth_draw_slot].tab; int neg = 0;
    for (int i = 0; i <= 40; i++) { host_touch(3000 + i * 450, 24000, 80); run(12); }
    host_touch(0, 0, 0); run(30);
    for (int i = 0; i < WAVE_LEN; i++) if (t[i] < -8000) neg++;
    CHECK(neg > 40, "WAVE page 6: a finger drawn low across the pad draws the wave low there (%d of 256)", neg);
    host_tap(KEY_TAB); host_tap(KEY_TAB); to_play(); run(30);
}

/* the colour schemes: every text colour readable on what it is drawn on (WCAG contrast ratios), a switch rewrites the
   whole screen once, ⇧H goes round all ten, and the scheme rides in the stick's settings */
static double lin_rgb(int c) { double v = c / 255.0; return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double rel_lum(uint32_t rgb) { return 0.2126 * lin_rgb(rgb >> 16 & 255) + 0.7152 * lin_rgb(rgb >> 8 & 255) + 0.0722 * lin_rgb(rgb & 255); }
static double contrast(uint8_t a, uint8_t b) {
    double x = rel_lum(gfx_rgb(a)), y = rel_lum(gfx_rgb(b));
    return (x > y ? x + 0.05 : y + 0.05) / (x > y ? y + 0.05 : x + 0.05);
}
static void colour_checks(void) {
    printf("colours\n");
    static const struct { uint8_t fg, bg; double min; const char *what; } pairs[] = {
        { C_TEXT, C_BG, 6.5, "text on the background" }, { C_TEXT, C_PANEL, 6, "text on a panel" }, { C_BRIGHT, C_PANEL, 7, "bright text" },
        { C_DIM, C_PANEL, 3, "hints on a panel" }, { C_DIM, C_BG, 3, "hints on the background" }, { C_BRIGHT, C_BORDER, 3.5, "a keycap" },
        { C_BLACK, C_AMBER, 4.5, "text on the accent" }, { C_AMBER_D, C_AMBER, 2.5, "the page tab's key" }, { C_AMBER, C_PANEL, 3, "accent text" },
        { C_GREEN, C_PANEL, 3, "green text" }, { C_CYAN, C_PANEL, 3, "cyan text" }, { C_PINK, C_PANEL, 2.6, "pink text" },
        { C_RED, C_PANEL, 3, "red text" }, { C_BLACK, C_GREEN, 3, "text on green" }, { C_BLACK, C_RED, 3, "text on red" },
        { C_BLACK, C_PINK, 3, "text on pink" }, { C_BORDER, C_PANEL, 1.25, "borders" }, { C_SCOPE, C_BLACK, 3, "the scope" },
    };
    for (int t = 0; t < GFX_THEMES; t++) {
        gfx_theme(t);
        int bad = 0; char worst[96] = "";
        for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
            double c = contrast(pairs[i].fg, pairs[i].bg);
            if (c < pairs[i].min) { bad++; snprintf(worst, sizeof worst, ", not: %s (%.1f)", pairs[i].what, c); }
        }
        CHECK(!bad, "%-9s every colour pair readable%s", gfx_theme_name(t), worst);
    }
    gfx_theme(0); run(100);
    uint64_t px0 = gfx_px_written; run(40);
    uint64_t idle = gfx_px_written - px0;
    host_shift_tap('h'); px0 = gfx_px_written; run(40);
    uint64_t sw = gfx_px_written - px0, all = (uint64_t)gfx_width() * gfx_height();
    CHECK(gfx_theme_now() == 1 && sw >= all && sw < all * 2, "⇧H: the next scheme (%s), every pixel rewritten once (%llu of %llu; %llu without a switch)",
          gfx_theme_name(gfx_theme_now()), (unsigned long long)sw, (unsigned long long)all, (unsigned long long)idle);
    for (int i = 0; i < GFX_THEMES - 1; i++) { host_shift_tap('h'); run(20); }
    CHECK(gfx_theme_now() == 0, "ten times ⇧H: round to the first one again");
    if (disk.have_tape) {                                              /* kept with a project save, back after a rescan */
        int s = (int)disk.tape.slots - 1;
        gfx_theme(7); run(50);
        bool saved = !disk.slot_used[s] && disk_save_slot(s, "COLOURS");
        gfx_theme(0); run(50); disk_rescan();
        uint8_t v, f, th = 0; uint16_t m;
        CHECK(saved && disk_settings(&v, &f, &m, &th) && th == 7, "the scheme is kept on the stick with a project save (%s back)", gfx_theme_name(th));
        disk_delete_slot(s); gfx_theme(0); run(50);
    }
}

/* the FX page's effects, on test signals straight through perf_block (120 BPM: a beat is 24000 frames), then the page's
   keys and a DUB throw through the whole app */
static int32_t tl[32], tr[32];
static void pf_feed(int blocks, int32_t (*sig)(int), int *k, int32_t *out, int cap, int *nout) {
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < 32; i++) tl[i] = tr[i] = sig((*k)++);
        perf_block(tl, tr, 32);
        for (int i = 0; i < 32 && out && *nout < cap; i++) out[(*nout)++] = tl[i];
    }
}
static int32_t sig_silence(int k) { (void)k; return 0; }
static int32_t sig_ramp(int k) { return (k % 20000) - 10000; }
static int32_t sig_sine(int k) { return (int32_t)(12000 * sin(k * 2 * M_PI * 1000 / 48000)); }
static int32_t sig_square(int k) { return (k / 5) % 2 ? 8000 : -8000; }
static int crossings(const int32_t *x, int from, int to) { int c = 0; for (int i = from + 1; i < to; i++) c += x[i - 1] < 0 && x[i] >= 0; return c; }
static void perf_checks(void) {
    printf("fx page\n");
    static int32_t out[48000];
    int k = 0, n = 0;
    uint16_t bpm = seq.bpm; seq.bpm = 120; perf_work(host_now_ms);
    for (int e = 0; e < PERF_FX; e++) { perf.on[e] = perf.latched[e] = false; }
    pf_feed(400, sig_ramp, &k, 0, 0, &n);                                 /* the ring fills with a ramp */
    /* REPEAT at 1/8 beat (3000 frames), no fade: the input stops, the slice goes on, the same every 3000 frames */
    perf.x[PF_REPEAT] = 450; perf.y[PF_REPEAT] = 0; perf.on[PF_REPEAT] = true;
    n = 0; pf_feed(400, sig_silence, &k, out, 48000, &n);
    int same = 0, loud = 0;
    for (int i = 3200; i < 9000; i++) { same += abs(out[i] - out[i + 3000]) <= 2; loud += abs(out[i]) > 100; }
    perf.on[PF_REPEAT] = false; n = 0; pf_feed(20, sig_silence, &k, out, 48000, &n);
    CHECK(same > 5700 && loud > 5000 && out[n - 1] == 0, "REPEAT 1/8 beat: the input stops, the slice goes on, the same every 3000 frames (%d of 5800); let go, silence", same);
    /* REVERSE: the ramp comes back falling */
    pf_feed(400, sig_ramp, &k, 0, 0, &n);
    perf.x[PF_REVERSE] = 300; perf.y[PF_REVERSE] = 1000; perf.on[PF_REVERSE] = true;
    n = 0; pf_feed(100, sig_silence, &k, out, 48000, &n);
    int falling = 0; for (int i = 400; i < 3000; i++) falling += out[i] < out[i - 1] || out[i - 1] - out[i] < -15000;
    perf.on[PF_REVERSE] = false; pf_feed(20, sig_silence, &k, 0, 0, &n);
    CHECK(falling > 2500, "REVERSE: a rising ramp comes back falling (%d of 2600 steps down)", falling);
    /* TAPE STOP in about half a second: the pitch falls, then nothing */
    perf.x[PF_TAPE] = 220; perf.y[PF_TAPE] = 0;
    pf_feed(200, sig_sine, &k, 0, 0, &n);
    perf.on[PF_TAPE] = true;
    n = 0; pf_feed(900, sig_sine, &k, out, 48000, &n);
    int c0 = crossings(out, 1000, 5800), c1 = crossings(out, 10000, 14800), c2 = crossings(out, 17000, 21800), c3 = crossings(out, 26000, 28000);
    perf.on[PF_TAPE] = false; pf_feed(40, sig_silence, &k, 0, 0, &n);
    CHECK(c0 > c1 && c1 > c2 && c3 == 0, "TAPE STOP: a 1 kHz tone slows (%d, %d, %d crossings in 100 ms) to a stop (%d)", c0, c1, c2, c3);
    /* GATE 1/16 beat (1500 frames), full depth: open, shut, open */
    perf.x[PF_GATE] = 600; perf.y[PF_GATE] = 1000; perf.on[PF_GATE] = true;
    n = 0; pf_feed(200, sig_sine, &k, out, 48000, &n);
    double open = 0, shut = 0;
    for (int i = 100; i < 650; i++) open += abs(out[i]) + abs(out[i + 1500]);
    for (int i = 850; i < 1400; i++) shut += abs(out[i]) + abs(out[i + 1500]);
    perf.on[PF_GATE] = false; pf_feed(20, sig_silence, &k, 0, 0, &n);
    CHECK(shut < open / 50, "GATE 1/16 at full depth: shut %.0f dB under open", 20 * log10(fmax(1, shut) / open));
    /* FILTER far left: a 4.8 kHz square nearly gone; the middle: untouched */
    perf.x[PF_FILTER] = 60; perf.y[PF_FILTER] = 0; perf.on[PF_FILTER] = true;
    n = 0; pf_feed(200, sig_square, &k, out, 48000, &n);
    double e = 0; for (int i = 2000; i < 6000; i++) e += (double)out[i] * out[i];
    perf.x[PF_FILTER] = 500; n = 0; pf_feed(200, sig_square, &k, out, 48000, &n);
    double m = 0; for (int i = 2000; i < 6000; i++) m += (double)out[i] * out[i];
    perf.on[PF_FILTER] = false; pf_feed(20, sig_silence, &k, 0, 0, &n);
    CHECK(sqrt(e / 4000) < 8000 * 0.1 && fabs(sqrt(m / 4000) - 8000) < 100, "FILTER: low-pass at the left, a 4.8 kHz square %.0f dB down; the middle is off (rms %.0f)", 20 * log10(sqrt(e / 4000) / 8000), sqrt(m / 4000));
    /* CRUSH at the right: two bits */
    perf.x[PF_CRUSH] = 1000; perf.y[PF_CRUSH] = 0; perf.on[PF_CRUSH] = true;
    n = 0; pf_feed(100, sig_sine, &k, out, 48000, &n);
    bool two = true; for (int i = 600; i < 3000; i++) two &= out[i] % 16384 == 0;
    perf.on[PF_CRUSH] = false; pf_feed(20, sig_silence, &k, 0, 0, &n);
    CHECK(two, "CRUSH at the right: every frame on a 2-bit step");
    /* the page: keys hold, Shift latches, Space holds, 0 lets everything go; Ctrl+0 is the page */
    to_play(); run(50); ctrl_tap('0'); run(50);
    host_key('3', true); run(40); bool held = perf.on[PF_REVERSE] && perf.sel == PF_REVERSE;
    host_key('3', false); run(40); bool let = !perf.on[PF_REVERSE];
    host_shift_tap('5'); run(40); bool latched = perf.on[PF_GATE] && perf.latched[PF_GATE];
    host_shift_tap('5'); run(40); latched &= !perf.on[PF_GATE] && !perf.latched[PF_GATE];   /* ⇧5 again: unlatched */
    host_key('2', true); run(40); host_tap(KEY_SPACE); host_key('2', false); run(40); bool hold = perf.on[PF_REPEAT] && perf.latched[PF_REPEAT];
    host_tap('0'); run(40); bool none = true; for (int e2 = 0; e2 < PERF_FX; e2++) none &= !perf.on[e2] && !perf.latched[e2];
    CHECK(held && let && latched && hold && none, "Ctrl+0 opens it; 3 held, let go; ⇧5 latches and unlatches; SPACE holds what sounds; 0 all off (%d%d%d%d%d)", held, let, latched, hold, none);
    /* DUB through the app: a note with the throw held rings on after both let go; without it, nothing */
    HOST_SET_ECHO(false); mix.ch[CH_PLAY].echo = 0;
    synth_note_on(60, 120, P_CHIP, 0x7F1); run(200); synth_note_off_tag(0x7F1); run(250);
    struct heard dry = listen(300);
    run(3000);
    host_key('7', true); run(20);
    synth_note_on(60, 120, P_CHIP, 0x7F1); run(200); synth_note_off_tag(0x7F1); run(30);
    host_key('7', false); run(220);
    struct heard dub = listen(300);
    run(6000);
    CHECK(dub.rms > 60 && dry.rms < 20 && !audio_echo(), "DUB: the note rings on in the echo after both let go (rms %.0f; without it %.0f); the echo is put back after", dub.rms, dry.rms);
    /* on the input alone: a tone through the line in, gated; the gate follows the input */
    host_input_tone(1000, 8000); mix_set_input(0); mix.ch[CH_INPUT].mute = false; run(300);
    perf.source = PERF_INPUT; perf.x[PF_GATE] = 600; perf.y[PF_GATE] = 1000; perf.on[PF_GATE] = true; run(200);
    double hi = 0; int lo_n = 0; double w[300];
    for (int i = 0; i < 300; i++) { w[i] = listen(1).rms; if (w[i] > hi) hi = w[i]; }
    for (int i = 0; i < 300; i++) lo_n += w[i] < hi / 20;
    perf.on[PF_GATE] = false; perf.source = PERF_ALL; run(100);
    struct heard plain = listen(100);
    mix.ch[CH_INPUT].mute = true; mix_set_input(-1); host_input_tone(0, 0);
    CHECK(hi > 2000 && lo_n > 90 && plain.rms > 2000, "on the input alone: its tone gated by the page (%d of 300 ms shut, loudest %.0f), then plain again", lo_n, hi);
    HOST_SET_ECHO(true); seq.bpm = bpm; to_play(); run(50);
}

/* the ANS page: a line drawn on the plate sounds at its row's pitch, a rising one rises, an empty plate is silent; a
   key sounds its row and, while the plate plays, writes it; the camera's picture lands on the plate; undo; a whole
   picture about as loud as a line; the plate kept in a project */
static int count_lit(void) { int n = 0; for (int i = 0; i < ANS_ROWS * ANS_COLS; i++) n += ans.plate[i] != 0; return n; }
static void ans_checks(void) {
    printf("ans\n");
    to_play(); run(50); host_key(KEY_LCTRL, true); host_tap('-'); host_key(KEY_LCTRL, false); run(100);
    HOST_SET_ECHO(false);
    uint8_t bars = ans.bars; ans.octave = 2; ans.bars = 4;
    ans_clear(); ans.playing = true; ans.seek = 1; run(300);
    struct heard empty = listen(300);
    for (int c = 0; c < ANS_COLS; c++) ans.plate[c * ANS_ROWS + 144] = 230;             /* C4, the whole pass */
    run(200);
    struct heard c4 = listen(500);
    CHECK(empty.rms < 2 && fabs(c4.hz - 261.6) < 2 && c4.rms > 1500, "a line at C4 sounds 261.6 Hz (%.1f Hz, rms %.0f); an empty plate is silent (%.1f)", c4.hz, c4.rms, empty.rms);
    ans_clear();
    for (int c = 0; c < ANS_COLS; c++) ans.plate[c * ANS_ROWS + 72 + c * 144 / ANS_COLS] = 230;   /* C3 to C5 over the pass */
    ans.seek = 1; run(200);
    struct heard lo = listen(400); run(ans_frames_per_pass() / 48 / 2 - 1100);
    struct heard hi = listen(400);
    CHECK(lo.hz > 120 && lo.hz < 180 && hi.hz > 240 && hi.hz < 400 && hi.hz > lo.hz * 1.6, "a line rising across the plate rises (%.0f Hz, then %.0f Hz halfway)", lo.hz, hi.hz);
    ans.playing = false; ans_clear(); run(300);
    host_key('z', true); run(200);                                                    /* C3: row 72 */
    struct heard key = listen(300);
    host_key('z', false); run(300);
    ans.playing = true; ans.seek = 1; host_key('z', true); run(1000); host_key('z', false); run(100);
    int written = 0; for (int c = 0; c < ANS_COLS; c++) written += ans.plate[c * ANS_ROWS + 72] != 0;
    CHECK(fabs(key.hz - 130.8) < 2 && written > 20, "a key sounds its tone while stopped (%.1f Hz), and while it plays writes its row (%d columns)", key.hz, written);
    ans.playing = false; run(100);
    host_tap(KEY_BACKSPACE); host_tap(KEY_BACKSPACE); run(50);                         /* cleared, then undone */
    int cleared = count_lit();
    host_key(KEY_LCTRL, true); host_tap('z'); host_key(KEY_LCTRL, false); run(50);
    CHECK(cleared == 0 && count_lit() == written, "Backspace twice clears the plate; Ctrl+Z brings it back (%d cells)", count_lit());
    ans_clear(); host_tap(KEY_ENTER); run(200);
    int pic = count_lit();
    const uint8_t *cl; int cw, chh; bool cam_off = !plat_camera_frame(&cl, &cw, &chh);
    CHECK(pic > 5000 && cam_off, "Enter: the camera's next picture is on the plate (%d cells lit), and the camera is off again", pic);
    int best = 0, most = 0;                                                            /* the picture's busiest column */
    for (int c = 0; c < ANS_COLS; c++) { int n = 0; for (int r = 0; r < ANS_ROWS; r++) n += ans.plate[c * ANS_ROWS + r] != 0; if (n > most) { most = n; best = c; } }
    ans.playing = true; ans.seek = MAX(1, best - 8); run(80);
    struct heard dense = listen(200);
    ans_clear(); for (int c = 0; c < ANS_COLS; c++) ans.plate[c * ANS_ROWS + 144] = 230; run(300);
    struct heard thin = listen(200);
    CHECK(dense.rms > thin.rms / 3 && dense.rms < thin.rms * 3, "a picture (%d tones at once) and one line about as loud (rms %.0f and %.0f)", most, dense.rms, thin.rms);
    ans.playing = false; run(200);
    if (disk.have_tape) {
        int s = (int)disk.tape.slots - 2;
        bool saved = !disk.slot_used[s] && disk_save_slot(s, "PLATE");
        ans_clear();
        bool back = saved && disk_load_slot(s);
        int lit = count_lit(); bool at = ans.plate[7 * ANS_ROWS + 144] == 230;
        CHECK(back && lit == ANS_COLS * 1 && at, "the plate is kept in a project (%d cells back)", lit);
        disk_delete_slot(s);
    }
    ans_clear(); ans.bars = bars; HOST_SET_ECHO(true); to_play(); run(50);
}

/* GENDY: the oscillator on its own (in tune, no mean, a level, a wave that keeps moving; DRIFT lets the pitch walk
   within its bounds), then through the synth as the presets GENDY 1-4 */
static void gendy_checks(void) {
    printf("gendy\n");
    static int32_t out[48000];
    uint32_t inc = (uint32_t)(220.0 * 4294967296.0 / 48000.0);                          /* A3 */
    for (int k = 0; k < GENDY_PATCHES; k++) {
        const struct gendy_patch *p = &gendy_bank[k];
        struct gendy_osc g; gendy_osc_start(&g, p, 1234u + (uint32_t)k);
        int16_t early[GENDY_POINTS]; int moved = 0;
        uint32_t per_tenth[10];
        for (int i = 0; i < 48000; i += 32) {
            gendy_render(&g, p, out + i, 32, inc);
            if (i == 32 * 20) memcpy(early, g.amp, sizeof early);
            if ((i + 32) % 4800 == 0) per_tenth[(i + 32) / 4800 - 1] = g.periods;
        }
        for (int i = 0; i < g.n; i++) moved += abs(g.amp[i] - early[i]);
        double sum = 0, sq = 0; int32_t peak = 0;
        for (int i = 0; i < 48000; i++) { sum += out[i]; sq += (double)out[i] * out[i]; peak = MAX(peak, abs(out[i])); }
        double mean = sum / 48000, rms = sqrt(sq / 48000);
        uint32_t lo = 1000, hi = 0;
        for (int t = 1; t < 10; t++) { uint32_t d = per_tenth[t] - per_tenth[t - 1]; lo = MIN(lo, d); hi = MAX(hi, d); }
        bool pitch = p->drift ? g.periods > 220 / 4 && g.periods < 220 * 4 && hi > lo : g.periods >= 219 && g.periods <= 221;
        CHECK(pitch && fabs(mean) < 400 && rms > 2500 && rms < 24000 && peak <= 32767 && moved > 200,
              "%-7s %2d points: %u periods in a second at A3 (%s), mean %.0f, rms %.0f, peak %d; the wave moved %d",
              p->name, g.n, g.periods, p->drift ? "drifting" : "in tune", mean, rms, peak, moved);
    }
    struct gendy_osc a, b;                                                                  /* a seed repeats its walk */
    gendy_osc_start(&a, &gendy_bank[0], 99); gendy_osc_start(&b, &gendy_bank[0], 99);
    for (int i = 0; i < 4800; i += 32) { gendy_render(&a, &gendy_bank[0], out, 32, inc); gendy_render(&b, &gendy_bank[0], out + 100, 32, inc); }
    CHECK(!memcmp(a.amp, b.amp, sizeof a.amp) && a.periods == b.periods, "the same seed walks the same way");
    HOST_SET_ECHO(false);
    run(1500);
    synth_note_on(57, 110, P_GD1, 0x7C0); run(100);
    struct level on = measure(400);
    synth_note_off_tag(0x7C0); run(gendy_bank[0].r_ms + 400);
    struct level off = measure(200);
    CHECK(!strcmp(synth_preset_name(P_GD1), gendy_bank[0].name) && on.rms > 800 && !on.clips && off.rms < 3,
          "the preset %s plays (rms %.0f, no clips) and ends after its release (%.1f)", synth_preset_name(P_GD1), on.rms, off.rms);
    HOST_SET_ECHO(true);
}

/* sieves: the reader, membership, snapping; the rhythm section's SIEVE pattern on the grid; typed on the SIEVES view,
   undone, kept in a project */
static int sieve_list(const char *text, int *out, int lo, int hi) {
    struct sieve s; memset(&s, 0, sizeof s); s.unit = 1; snprintf(s.text, SIEVE_TEXT, "%s", text);
    if (!sieve_compile(&s)) return -1;
    int n = 0; for (int x = lo; x < hi; x++) if (sieve_has(&s, x)) out[n++] = x;
    return n;
}
static void sieve_checks(void) {
    printf("sieves\n");
    int m[64], n = sieve_list("(-3@2&4@0)|(-3@1&4@1)|(3@2&4@2)|(-3@0&4@3)", m, 0, 12);
    static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 };
    CHECK(n == 7 && !memcmp(m, major, sizeof major), "Xenakis's major scale as a sieve: %d %d %d %d %d %d %d", m[0], m[1], m[2], m[3], m[4], m[5], m[6]);
    n = sieve_list("3@0|4@0", m, 0, 12);
    int neg = sieve_list("3@1", m + 20, -6, 0);
    CHECK(n == 6 && m[1] == 3 && m[2] == 4 && m[5] == 9 && neg == 2 && m[20] == -5 && m[21] == -2, "3@0|4@0: %d of 12; 3@1 below zero too (%d, %d)", n, m[20], m[21]);
    struct sieve e; memset(&e, 0, sizeof e);
    static const char *const bad[] = { "3@", "(3@1", "3@1)", "0@1", "", "3x", "3@1|" };
    int told = 0;
    for (int i = 0; i < 7; i++) { snprintf(e.text, SIEVE_TEXT, "%s", bad[i]); told += !sieve_compile(&e) && e.err[0]; }
    snprintf(e.text, SIEVE_TEXT, "3@"); sieve_compile(&e);
    CHECK(told == 7, "7 broken formulas refused, each with a reason (\"%s\")", e.err);
    struct sieve s; memset(&s, 0, sizeof s); s.unit = 1; snprintf(s.text, SIEVE_TEXT, "(-3@2&4@0)|(-3@1&4@1)|(3@2&4@2)|(-3@0&4@3)"); sieve_compile(&s);
    int32_t a = sieve_snap_pitch(&s, 61 * 256 + 100), b = sieve_snap_pitch(&s, 66 * 256 + 200);
    s.unit = 2; int32_t q = sieve_snap_pitch(&s, 62 * 256 + 100);          /* quarter tones: member 125 is 62.5 */
    CHECK(a == 60 * 256 && b == 67 * 256 && sieve_next(&s, 123, 50) == 124 && q == 62 * 256 + 128,
          "snapping to it: C#4+ to C4, F#4+ to G4; read in quarter tones, 62.4 to 62.5");
    /* the SIEVE pattern: S1 = 3@0 on the kick alone, at 60 BPM a sixteenth is 250 ms */
    to_play(); host_tap(KEY_ESC); run(300);
    uint16_t bpm = seq.bpm; seq.bpm = 60;
    char keep[SIEVE_TEXT]; memcpy(keep, sieves[0].text, SIEVE_TEXT);
    snprintf(sieves[0].text, SIEVE_TEXT, "3@0"); sieve_compile(&sieves[0]);
    rhythm.pattern = RHYTHM_SIEVE; rhythm.bass = false; rhythm.mute = 6;
    HOST_SET_ECHO(false); run(300);
    host_wav_open("/tmp/hb-check-sieve.wav");
    rhythm_play(true); run_capture(6000); rhythm_play(false);
    host_wav_close();
    FILE *f = fopen("/tmp/hb-check-sieve.wav", "rb"); fseek(f, 44, SEEK_SET);
    static int16_t d[48000 * 7 * 2]; long nf = (long)fread(d, 4, 48000 * 7, f); fclose(f); remove("/tmp/hb-check-sieve.wav");
    double on[64]; int no = 0; long quiet = 48 * 30;
    for (long i = 0; i < nf && no < 64; i++) { int v = abs(d[2 * i]); if (v < 150) quiet++; else { if (v > 1500 && quiet > 48 * 30) on[no++] = i / 48000.0; if (v > 1500) quiet = 0; } }
    double worst = 0;
    for (int i = 1; i < no; i++) worst = fmax(worst, fabs(on[i] - on[i - 1] - 0.75));
    CHECK(no >= 7 && worst < 0.001, "the SIEVE pattern plays S1 = 3@0 on the kick: %d kicks, every third sixteenth (worst %.2f ms off)", no, worst * 1000);
    seq.bpm = bpm; rhythm.bass = true; rhythm.mute = 0; rhythm.pattern = 0; HOST_SET_ECHO(true);
    memcpy(sieves[0].text, keep, SIEVE_TEXT); sieve_compile(&sieves[0]);
    /* on the view (F12 again steps to it): S1, Enter, 5, ⇧2 (@), 1, ⇧\ (|), 7, Enter */
    host_tap(KEY_F12); run(50);
    int steps = 0;
    for (int i = 0; i < 4 && xen_current() != XV_SIEVES; i++) { host_tap(KEY_F12); run(30); steps++; }
    char before[SIEVE_TEXT]; memcpy(before, sieves[0].text, SIEVE_TEXT);
    host_tap(KEY_ENTER); for (int i = 0; i < 20; i++) host_tap(KEY_BACKSPACE);
    host_tap('5'); host_shift_tap('2'); host_tap('1'); host_shift_tap('\\'); host_tap('7'); host_tap(KEY_ENTER); run(50);
    bool typed = !strcmp(sieves[0].text, "5@1|7") && sieves[0].ok;
    host_key(KEY_LCTRL, true); host_tap('z'); host_key(KEY_LCTRL, false); run(50);
    bool undone = !strcmp(sieves[0].text, before) && sieves[0].ok;
    CHECK(xen_current() == XV_SIEVES && typed && undone, "F12 again steps to SIEVES (%d presses); typed with ⇧2 and ⇧\\: 5@1|7; Ctrl+Z puts back %s",
          steps, sieves[0].text);
    static uint8_t blob[PROJECT_MAX];
    snprintf(sieves[2].text, SIEVE_TEXT, "11@0|11@4"); sieves[2].unit = 6; sieve_compile(&sieves[2]);
    size_t len = project_save(blob, sizeof blob, 0, true);
    sieve_init();
    project_load(blob, len);
    CHECK(!strcmp(sieves[2].text, "11@0|11@4") && sieves[2].unit == 6 && sieves[2].ok, "a project keeps its sieves (S3 %s, unit %d)", sieves[2].text, sieves[2].unit);
    sieve_init(); to_play(); run(50);
}

/* clouds: as dense as asked (a Poisson stream), inside their band, on a sieve's notes or its sixteenths, sliding; heard
   on their own mixer channel; switched and steered on the view; kept in a project */
static uint32_t marks_from(uint32_t i0, int cloud, int32_t *lo, int32_t *hi) {
    uint32_t n = 0; *lo = 1 << 30; *hi = -(1 << 30);
    for (uint32_t i = i0; i < cloud_mark_n; i++) {
        const struct cloud_mark *m = &cloud_marks[i % CLOUD_MARKS];
        if (m->cloud != cloud) continue;
        n++; *lo = MIN(*lo, m->pitch); *hi = MAX(*hi, m->pitch);
    }
    return n;
}
static void cloud_checks(void) {
    printf("clouds\n");
    to_play(); host_tap(KEY_ESC); run(300);
    HOST_SET_ECHO(false);
    cloud_defaults();
    struct cloud *a = &clouds[0];
    a->density = 50; a->low = 60; a->high = 72; a->length = 10; a->glide = 0;       /* 6.3 a second, C4..C5 */
    uint32_t i0 = cloud_mark_n; a->on = true; run(10000); a->on = false; cloud_stop(0); run(300);
    int32_t lo, hi; uint32_t n = marks_from(i0, 0, &lo, &hi);
    CHECK(n > 40 && n < 90 && lo >= 60 * 256 && hi <= 72 * 256, "a cloud of 6.3 notes a second: %u in 10 s, all between C4 and C5 (%.1f to %.1f)", n, lo / 256.0, hi / 256.0);
    a->density = 80; i0 = cloud_mark_n; a->on = true; run(4000); a->on = false; cloud_stop(0); run(300);
    uint32_t dense = marks_from(i0, 0, &lo, &hi);
    CHECK(dense > 160 && dense < 240, "at 50 a second: %u in 4 s", dense);
    /* a pitch sieve: only the triad's notes */
    char keep[SIEVE_TEXT]; memcpy(keep, sieves[0].text, SIEVE_TEXT);
    snprintf(sieves[0].text, SIEVE_TEXT, "12@0|12@4|12@7"); sieve_compile(&sieves[0]);
    a->density = 70; a->low = 48; a->high = 84; a->pitch_sieve = 0;
    i0 = cloud_mark_n; a->on = true; run(3000); a->on = false; cloud_stop(0); run(200);
    int wrong = 0; n = 0;
    for (uint32_t i = i0; i < cloud_mark_n; i++) { const struct cloud_mark *m = &cloud_marks[i % CLOUD_MARKS]; int pc = (m->pitch >> 8) % 12; n++; wrong += (m->pitch & 255) || (pc != 0 && pc != 4 && pc != 7); }
    CHECK(n > 50 && !wrong, "on the sieve 12@0|12@4|12@7: %u notes, all C, E or G (%d not)", n, wrong);
    /* a rhythm sieve: every note on a beat of 120 BPM (24000 frames) */
    snprintf(sieves[1].text, SIEVE_TEXT, "4@0"); sieve_compile(&sieves[1]);
    uint16_t bpm = seq.bpm; seq.bpm = 120;
    a->pitch_sieve = -1; a->rhythm_sieve = 1; a->density = 60;
    i0 = cloud_mark_n; a->on = true; run(4200); a->on = false; cloud_stop(0); run(200);
    uint32_t first = 0, off = 0, beats = 0, prev = 0; n = 0;
    for (uint32_t i = i0; i < cloud_mark_n; i++) {
        uint32_t at = cloud_marks[i % CLOUD_MARKS].at;
        if (!n) first = at; else if (at != prev) { beats++; uint32_t d = (at - first) % 24000; off = MAX(off, MIN(d, 24000 - d)); }
        prev = at; n++;
    }
    CHECK(n >= 8 && beats >= 6 && off <= 64, "on the sieve 4@0 as sixteenths: %u notes on %u beats after the first, none more than %u frames off the beat", n, beats, off);
    seq.bpm = bpm; a->rhythm_sieve = -1;
    memcpy(sieves[0].text, keep, SIEVE_TEXT); sieve_init();
    /* glissandi, and the channel */
    struct cloud *b = &clouds[1];
    b->glide = 80; b->density = 60;
    i0 = cloud_mark_n; b->on = true; run(1500);
    struct level heard = measure(500);
    mix.ch[CH_CLOUD].mute = true; run(400);
    struct level muted = measure(300);
    mix.ch[CH_CLOUD].mute = false; b->on = false; cloud_stop(1); run(300);
    int slides = 0, fast = 0; n = 0;
    for (uint32_t i = i0; i < cloud_mark_n; i++) { const struct cloud_mark *m = &cloud_marks[i % CLOUD_MARKS]; if (m->cloud != 1) continue; n++; slides += m->glide != 0; fast += abs(m->glide) > 12 * 256; }
    CHECK(n > 5 && (uint32_t)slides * 10 >= n * 9 && (uint32_t)fast < n / 3 + 1 && heard.rms > 200 && muted.rms < heard.rms / 50,
          "glissandi: %d of %u notes slide, %d faster than an octave a second; heard on CLOUDS (rms %.0f), silent muted (%.1f)", slides, n, fast, heard.rms, muted.rms);
    /* the view: 1 switches cloud A, 0 all off; a MIDI note centres the chosen cloud's band */
    host_tap(KEY_F12); run(30);
    for (int i = 0; i < 4 && xen_current() != XV_CLOUDS; i++) { host_tap(KEY_F12); run(30); }
    host_tap('1'); run(300); bool on1 = clouds[0].on;
    host_tap('0'); run(100); bool off0 = !clouds[0].on;
    int w = clouds[0].high - clouds[0].low;
    ui_pages[PAGE_XEN]->midi(90, 100, host_now_ms); run(300);
    bool centred = clouds[0].on && clouds[0].low + w / 2 == 90;
    ui_pages[PAGE_XEN]->midi(90, 0, host_now_ms); run(100);
    CHECK(xen_current() == XV_CLOUDS && on1 && off0 && centred && !clouds[0].on, "the view: 1 plays cloud A, 0 stops it; a MIDI note plays it around F#6 while held");
    static uint8_t blob[PROJECT_MAX];
    clouds[2].density = 33; clouds[2].sound = P_GD2; clouds[2].on = true;
    size_t len = project_save(blob, sizeof blob, 0, true);
    clouds[2].on = false; cloud_defaults();
    project_load(blob, len);
    CHECK(clouds[2].density == 33 && clouds[2].sound == P_GD2 && !clouds[2].on, "a project keeps its clouds (C: density %d, %s), and doesn't start them", clouds[2].density, synth_preset_name(clouds[2].sound));
    cloud_defaults(); HOST_SET_ECHO(true); to_play(); run(50);
}

/* UPIC: an arc sounds its pitch where the cursor meets it and glides with it; nothing sounds where there is none; the
   hand can hold the cursor; retrograde and inversion; the pen on the view; undo; a page kept in a project (media) */
static void upic_arc(int t0, int p0, int t1, int p1, int sound) {
    struct upic_pt pt[2] = { { (uint16_t)t0, (uint16_t)p0 }, { (uint16_t)t1, (uint16_t)p1 } };
    upic_add(pt, 2, (uint8_t)sound, 110);
}
static void px_pointer(int x, int y, int b) { host_pointer(x * 32768 / 1024, y * 32768 / 768, b); }
static void upic_checks(void) {
    printf("upic\n");
    to_play(); host_tap(KEY_ESC); run(300);
    HOST_SET_ECHO(false);
    upic_clear(); upic.bars = 0; upic.seconds = 4;                                    /* a page of 4 s */
    upic_arc(0, 69 * 256, 16384, 69 * 256, P_ORGAN);                                  /* A4 for the first second */
    upic_arc(32768, 57 * 256, 65535, 69 * 256, P_ORGAN);                              /* A3 up to A4 over the last two */
    upic.seek = 1; upic.playing = true; run(200);
    struct heard a4 = listen(500);
    run(700);
    struct heard gap = listen(300);                                                    /* 1.7-2.0 s: no arc */
    run(250);
    struct heard low = listen(300); run(900);
    struct heard high = listen(300);
    upic.playing = false; run(400);
    CHECK(fabs(a4.hz - 440) < 4 && gap.rms < 5 && low.hz > 215 && low.hz < 260 && high.hz > low.hz * 1.35 && high.hz < 450,
          "an arc at A4 sounds %.1f Hz; none between arcs (rms %.1f); a glissando rises (%.0f Hz, then %.0f)", a4.hz, gap.rms, low.hz, high.hz);
    upic.scrub = 8000; upic.scrub_ms = host_now_ms + 100000; run(200);
    struct heard held = listen(300);
    upic.scrub = -1; run(400);
    struct heard let = listen(200);
    mix.ch[CH_UPIC].mute = true; upic.scrub = 8000; upic.scrub_ms = host_now_ms + 100000; run(200);
    struct heard muted = listen(200);
    upic.scrub = -1; mix.ch[CH_UPIC].mute = false; run(300);
    upic.scrub = 8000; upic.scrub_ms = host_now_ms; run(400);                         /* a hold nobody renews: let go */
    bool expired = upic.scrub == -1;
    CHECK(fabs(held.hz - 440) < 4 && let.rms < 5 && muted.rms < 5 && expired,
          "the hand holds the cursor on the A (%.1f Hz), lets go (rms %.1f), a stale hold ends by itself; UPIC's channel muted, silent (%.1f)", held.hz, let.rms, muted.rms);
    upic_mirror_time();
    const struct upic_pt *q = upic.d.pt + upic.d.arc[1].first;
    bool retro = q[0].t == 0 && q[0].p == 69 * 256 && q[1].t == 32767 && q[1].p == 57 * 256;
    upic_mirror_pitch();
    bool inv = q[0].p == UPIC_LO + UPIC_HI - 69 * 256;
    CHECK(retro && inv, "backwards: the rising arc falls from the page's start; upside down: its A4 mirrored about the middle");
    /* the pen on the view: a stroke from lower left to upper right, snapped to semitones; undone */
    upic_clear();
    host_tap(KEY_F12); run(30);
    for (int i = 0; i < 4 && xen_current() != XV_UPIC; i++) { host_tap(KEY_F12); run(30); }
    run(50);
    host_tap(KEY_DOWN); host_tap(KEY_DOWN); host_tap(KEY_RIGHT); host_tap(KEY_UP); host_tap(KEY_UP); run(30);   /* snap (from sound): semitones */
    for (int i = 0; i <= 30; i++) { px_pointer(200 + i * 10, 500 - i * 7, 1); run(16); }
    px_pointer(500, 290, 0); run(50);
    int n = upic.narcs ? upic.d.arc[0].n : 0; bool rising = n >= 2, steps = true;
    for (int i = 1; i < n; i++) { const struct upic_pt *pp = upic.d.pt + upic.d.arc[0].first; rising &= pp[i].t > pp[i - 1].t && pp[i].p >= pp[i - 1].p; }
    for (int i = 0; i < n; i++) steps &= (upic.d.pt[upic.d.arc[0].first + i].p & 255) == 0;
    if (getenv("CHECK_DEBUG")) for (int i = 0; i < n; i++) printf("    pt %d: t %u p %u\n", i, upic.d.pt[upic.d.arc[0].first + i].t, upic.d.pt[upic.d.arc[0].first + i].p);
    int before_undo = upic.narcs;
    host_key(KEY_LCTRL, true); host_tap('z'); host_key(KEY_LCTRL, false); run(50);
    if (getenv("CHECK_DEBUG")) printf("    view %d arcs %d -> %d rising %d steps %d\n", xen_current(), before_undo, upic.narcs, rising, steps);
    CHECK(xen_current() == XV_UPIC && n >= 4 && rising && steps && upic.narcs == 0,
          "the pen draws an arc (%d points, forward in time, rising, on semitones); Ctrl+Z takes it away", n);
    if (disk.have_tape) {
        upic_clear(); upic.seconds = 8;
        for (int k = 0; k < 20; k++) upic_arc(k * 3000, (40 + k) * 256, k * 3000 + 2500, (52 + k) * 256, P_GD1 + k % 4);
        int s = (int)disk.tape.slots - 2;
        bool saved = !disk.slot_used[s] && disk_save_slot(s, "UPIC");
        upic_clear(); upic.seconds = 2;
        bool back = saved && disk_load_slot(s);
        bool same = upic.narcs == 20 && upic.seconds == 8 && upic.d.arc[19].sound == P_GD4 && upic.d.pt[upic.d.arc[19].first + 1].p == 71 * 256;
        CHECK(back && same, "a page of 20 arcs is kept in a project (%d back)", upic.narcs);
        disk_delete_slot(s);
    }
    /* a cloud written onto the page: CLOUDS, Enter */
    upic_clear(); upic.bars = 0; upic.seconds = 8; cloud_defaults();
    clouds[1].density = 50; clouds[1].glide = 60;                                    /* B: 6 a second, gliding */
    xen_goto(XV_CLOUDS); host_tap(KEY_TAB); host_tap(KEY_ENTER); run(50);
    int sloped = 0, inband = 0;
    for (int a = 0; a < upic.narcs; a++) {
        const struct upic_pt *pp = upic.d.pt + upic.d.arc[a].first;
        sloped += pp[0].p != pp[1].p; inband += pp[0].p >= clouds[1].low * 256 && pp[0].p <= clouds[1].high * 256;
    }
    CHECK(upic.narcs > 25 && upic.narcs < 80 && sloped > upic.narcs / 2 && inband == upic.narcs && upic.d.arc[0].sound == clouds[1].sound,
          "cloud B written onto an 8-second page: %d arcs (6 a second), %d sloping, all starting in its band", upic.narcs, sloped);
    upic_clear(); upic.bars = 4; cloud_defaults(); HOST_SET_ECHO(true); to_play(); run(50);
}

/* instruments from files: the built-in ones read cleanly, a broken file says where, a stick's file joins; each plays
   as its file says (a strip struck note by note, a free one gliding, an arpeggio on the tempo, hold); F1 steps to them */
static int find_inst(const char *name) { for (int i = 0; i < inst_count; i++) if (!strcmp(insts[i].name, name)) return i; return -1; }
static int onsets(int ms, double *first_gap) {                                   /* notes starting, from the audio */
    static int16_t d[48000 * 4 * 2];
    host_wav_open("/tmp/hb-check-inst.wav"); run_capture(ms); host_wav_close();
    FILE *f = fopen("/tmp/hb-check-inst.wav", "rb"); fseek(f, 44, SEEK_SET);
    long n = (long)fread(d, 4, 48000 * 4, f); fclose(f); remove("/tmp/hb-check-inst.wav");
    int no = 0; long quiet = 48 * 20, last = -1; double gap = 0;
    for (long i = 0; i < n; i++) { int a = abs(d[2 * i]); if (a < 200) quiet++; else { if (a > 1500 && quiet > 48 * 20) { if (last >= 0 && no == 1) gap = (i - last) / 48000.0; last = i; no++; } if (a > 1500) quiet = 0; } }
    if (first_gap) *first_gap = gap;
    return no;
}
static void inst_checks(void) {
    printf("instruments\n");
    to_play(); host_tap(KEY_ESC); run(300);
    HOST_SET_ECHO(false);
    inst_load_all();
    int clean = 0; for (int i = 0; i < inst_count; i++) clean += !insts[i].errors;
    CHECK(inst_count >= inst_builtin_count && clean == inst_count && find_inst("STYLO") >= 0 && !strcmp(synth_preset_name(P_INST1 + find_inst("STYLO")), "STYLO"),
          "%d built-in instruments, all read cleanly; each is a preset by its name", inst_builtin_count);
    struct inst t;
    static const char bad[] = "name: oops\n[sound]\nwave: sawtooth\nattack: soon\n[play]\nstrip: E4 A2\nwobble: 3\n";
    bool named = inst_parse(bad, (int)strlen(bad), &t);
    static const char nameless[] = "[sound]\nwave: saw\n";
    struct inst u; bool none = !inst_parse(nameless, (int)strlen(nameless), &u);
    CHECK(named && t.errors == 4 && strstr(t.err, "line 3") && !strcmp(t.name, "OOPS") && none,
          "a file with 4 mistakes still makes an instrument and says where (%s); one without a name makes none", t.err);
    /* a stick's file joins the built-in ones */
    static const char mine[] = "name: MINE\nabout: added on a computer\n[sound]\nwave: sine\n[play]\nkeys: chromatic C4\n";
    struct fat_file ff;
    bool wrote = disk.have_boot_fat && fat_create(&disk.fat, "INSTR", "MINE.TXT", sizeof mine - 1, &ff) && fat_write_inplace(&disk.fat, &ff, 0, mine, sizeof mine - 1);
    inst_load_all();
    CHECK(wrote && inst_count >= inst_builtin_count + 1 && find_inst("MINE") >= 0, "INSTR/MINE.TXT on the stick joins them (%d instruments)", inst_count);
    /* STYLO: the strip from A2, a key a semitone, struck again on a new one */
    int st = find_inst("STYLO"); inst_select(st);
    inst_strip(true, 45 * 256, 100); run(200);
    struct heard a2 = listen(300);
    inst_strip(true, 52 * 256, 100); run(150);
    struct heard e3 = listen(300);
    inst_strip(false, 0, 0); run(300);
    struct heard off = listen(200);
    CHECK(fabs(a2.hz - 110) < 2 && fabs(e3.hz - 164.8) < 3 && off.rms < 5, "STYLO: the strip plays A2 (%.1f Hz), then E3 (%.1f Hz), and stops when let go", a2.hz, e3.hz);
    /* THEREMIN: free, a finger's move glides */
    int th = find_inst("THEREMIN"); inst_select(th);
    inst_strip(true, 57 * 256, 100); run(400);
    struct heard a3 = listen(300);
    inst_strip(true, 69 * 256, 100); run(8);
    struct heard mid = listen(20); run(300);
    struct heard a4 = listen(300);
    inst_strip(false, 0, 0); run(600);
    CHECK(fabs(a3.hz - 220) < 6 && fabs(a4.hz - 440) < 10 && mid.hz > 200 && mid.hz < 440, "THEREMIN: A3 (%.0f Hz) glides up to A4 (%.0f Hz), passing through (%.0f Hz)", a3.hz, a4.hz, mid.hz);
    /* GRIDPADS: two pads held, an arpeggio at 4 a beat, 120 BPM: a note every 125 ms */
    uint16_t bpm = seq.bpm; seq.bpm = 120;
    int gp = find_inst("GRIDPADS"); inst_select(gp);
    struct preset keep_gp = insts[gp].snd;                                           /* short notes, so each is heard apart */
    inst_knob_set(&insts[gp], IK_DECAY, 25); inst_knob_set(&insts[gp], IK_SUSTAIN, 0); inst_knob_set(&insts[gp], IK_RELEASE, 5); inst_apply(gp);
    inst_note(300, 48 * 256, 110, true); inst_note(301, 55 * 256, 110, true);
    double gap = 0; int notes = onsets(2000, &gap);
    inst_note(300, 0, 0, false); inst_note(301, 0, 0, false); run(500);
    struct heard quiet = listen(200);
    insts[gp].snd = keep_gp; inst_apply(gp);
    CHECK(notes >= 13 && notes <= 18 && fabs(gap - 0.125) < 0.004 && quiet.rms < 5, "GRIDPADS: two pads held, %d arpeggio notes in 2 s, %.1f ms apart; let go, it stops", notes, gap * 1000);
    /* SIEVHARP holds: the key let go, it goes on; Space-like unlatch stops it */
    int sh = find_inst("SIEVHARP"); inst_select(sh);
    struct preset keep_sh = insts[sh].snd;
    inst_knob_set(&insts[sh], IK_DECAY, 25); inst_knob_set(&insts[sh], IK_RELEASE, 5); inst_apply(sh);
    inst_note('z', inst_step(&insts[sh], 48, 0), 110, true); run(50); inst_note('z', 0, 0, false);
    int still = onsets(1500, 0);
    inst_latch(false); run(900);
    struct heard gone = listen(300);
    insts[sh].snd = keep_sh; inst_apply(sh);
    CHECK(still >= 3 && gone.rms < 5, "SIEVHARP holds: after the key is let go, %d more notes; released, silence", still);
    /* SHRUTI: reeds a key opens and closes, in just intonation, sounding only with air in the bellows */
    int sr = find_inst("SHRUTI"); inst_select(sr);
    bool parsed = sr >= 0 && insts[sr].tuning == TUNING_JUST && insts[sr].tonic == 0 && insts[sr].hold == HOLD_TOGGLE && insts[sr].bellows && !insts[sr].errors;
    inst_note('z', 48 * 256, 105, true); inst_note('z', 0, 0, false);     /* Sa (C3) opens */
    inst_note('b', 55 * 256, 105, true); inst_note('b', 0, 0, false);     /* Pa (G3) opens */
    run(100);
    int32_t snd[4]; int nsnd = inst_sounding(snd, 4);
    struct heard dry = listen(300);
    bool sa_pa = nsnd == 2 && ((snd[0] == 48 * 256 && snd[1] == 55 * 256 + 5) || (snd[1] == 48 * 256 && snd[0] == 55 * 256 + 5));
    CHECK(parsed && sa_pa && dry.rms < 5, "SHRUTI: Sa and Pa open (Pa a pure 3:2, 5/256 of a semitone above equal), silent without air");
    for (int i = 0; i < 60; i++) { inst_pump(900); run(16); }               /* a second of Enter held */
    struct heard blown = listen(300);
    run(9000);
    struct heard leaked = listen(300);
    inst_note('z', 48 * 256, 105, true); inst_note('z', 0, 0, false);     /* Sa closes */
    run(50); nsnd = inst_sounding(snd, 4);
    CHECK(blown.rms > 200 && leaked.rms < blown.rms / 4 && nsnd == 1 && snd[0] == 55 * 256 + 5,
          "pumped, it sounds (rms %.0f); left alone the air leaks out (%.0f); Z again closes Sa", blown.rms, leaked.rms);
    inst_clear(); run(50);
    CHECK(inst_sounding(snd, 4) == 0, "Space closes every reed");
    seq.bpm = bpm; inst_select(-1);
    /* F1 steps from the omnichord through them; the tab takes the name */
    host_tap(KEY_F2); run(30); host_tap(KEY_F1); run(30);
    char seen[8][12]; int ns = 0;
    for (int i = 0; i < inst_count + 1 && ns < 8; i++) { host_tap(KEY_F1); run(30); snprintf(seen[ns++], 12, "%s", ui_pages[PAGE_PLAY]->name); }
    if (getenv("CHECK_DEBUG")) for (int i = 0; i < ns; i++) printf("    tab %d: %s (selected %d)\n", i, seen[i], inst_selected());
    CHECK(!strcmp(seen[0], insts[0].name) && !strcmp(seen[1], insts[1].name) && !strcmp(seen[ns - 1], "PLAY") && inst_selected() < 0,
          "F1 again steps through them: %s, %s … back to %s", seen[0], seen[1], seen[ns - 1]);
    /* a knob turned is kept in the project, by the instrument's name */
    static uint8_t blob[PROJECT_MAX];
    int sy = find_inst("STYLO"); int was = inst_knob_get(&insts[sy], IK_CUTOFF);
    inst_knob_set(&insts[sy], IK_CUTOFF, 42); inst_apply(sy);
    size_t plen = project_save(blob, sizeof blob, 0, true);
    inst_knob_set(&insts[sy], IK_CUTOFF, was); inst_apply(sy);
    project_load(blob, plen);
    CHECK(inst_knob_get(&insts[sy], IK_CUTOFF) == 42 && synth_preset(P_INST1 + sy)->cutoff == 42, "a knob turned (STYLO's cutoff 42) comes back with the project");
    inst_knob_set(&insts[sy], IK_CUTOFF, was); inst_apply(sy);
    /* the sound plays anywhere: the preset on its own */
    synth_note_on(69, 110, (uint8_t)(P_INST1 + find_inst("MINE")), 0x7D0); run(200);
    struct heard any = listen(300);
    synth_note_off_tag(0x7D0); run(400);
    CHECK(fabs(any.hz - 440) < 4, "an instrument's sound is a preset anywhere: MINE's sine at A4, %.1f Hz", any.hz);
    HOST_SET_ECHO(true);
}

/* the boot's splash: each piece sounds, draws, and hands the screen back on its own; a key ends it without reaching the
   page, and its sound fades; mute holds for it; the pick never repeats the last one */
static const struct fb_info *cfb;
static int amber_in_title(void) {                     /* the title bar's active tab: pixels of the scheme's accent */
    uint32_t want = gfx_rgb(C_AMBER), n = 0;
    for (uint32_t y = 0; y < 14 && y < cfb->height; y++)
        for (uint32_t x = 0; x < cfb->width; x++) n += (((const uint32_t *)((const uint8_t *)cfb->addr + y * cfb->pitch))[x] & 0xFFFFFF) == want;
    return (int)n;
}
/* Doom: typing iddqd starts it with the WAD on the stick; F1 leaves it (its clock and its music stop) and iddqd goes
   back; its own Quit leaves too; after an error it starts afresh. Freedoom (make freedoom) plays the WAD. */
extern int gametic, gamestate;                               /* the engine's (third_party/doom) */
extern unsigned int menuactive, usergame;
extern const uint8_t gammatable[5][256];
#define FREEDOOM "build/freedoom/freedoom-0.13.0/freedoom1.wad"
static void type_slowly(const char *s) { for (; *s; s++) { host_tap((uint8_t)*s); run(40); } }
static bool put_file(const char *name, const void *p, uint32_t n) {
    struct fat_file fi;
    return disk.have_boot_fat && fat_create(&disk.fat, "", name, n, &fi) && fat_write_inplace(&disk.fat, &fi, 0, p, n);
}
static int doom_level(void) { return mix.ch[CH_DOOM].vu_l + mix.ch[CH_DOOM].vu_r; }
static const uint8_t *wad_lump(const uint8_t *w, long n, const char *name) {
    uint32_t lumps = w[4] | w[5] << 8 | (uint32_t)w[6] << 16 | (uint32_t)w[7] << 24, dir = w[8] | w[9] << 8 | (uint32_t)w[10] << 16 | (uint32_t)w[11] << 24;
    for (uint32_t i = 0; i < lumps && dir + 16 * i + 16 <= (uint64_t)n; i++) {
        const uint8_t *e = w + dir + 16 * i;
        if (!strncmp((const char *)e + 8, name, 8)) return w + (e[0] | e[1] << 8 | (uint32_t)e[2] << 16 | (uint32_t)e[3] << 24);
    }
    return 0;
}
/* a MUS score as id's WADs have them: the organ on channel 0 holds C4 for half a second (70 ticks at 140 a second),
   the drum channel (MUS 15) strikes a kick with it, then the score ends */
static const uint8_t mus_score[] = {
    'M', 'U', 'S', 0x1A, 15, 0, 16, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0x40, 0, 16,                  /* controller 0 = program change: channel 0 plays GM 16, an organ */
    0x10, 0x80 | 60, 100,         /* play C4 at 100 on channel 0 */
    0x9F, 36 | 0x80, 110, 70,     /* play a kick (36) on the drum channel, then 70 ticks */
    0x00, 60,                     /* release C4 */
    0x60 };                       /* the end of the score */
static void doom_checks(void) {
    printf("doom\n");
    to_play(); host_tap(KEY_ESC); run(300);
    static uint8_t events[1 << 16];
    doomsnd_set_memory(events, sizeof events); doomsnd_music_volume(127);
    int before = synth_active_voices();
    bool loaded = doomsnd_music_load(mus_score, sizeof mus_score);
    doomsnd_music_play(false); run(200);
    int during = synth_active_voices() - before;
    run(900);
    CHECK(loaded && during == 2 && !doomsnd_music_playing() && synth_active_voices() == before,
          "a MUS score plays on BARE!'s voices (%d sounding: the organ and the kick) and ends", during);
    uint32_t bg = gfx_rgb(C_BG);
    if (!doom_memory()) {
        type_slowly("iddqd"); run(100);
        CHECK(!doom_showing() && strstr(doom_status(), "no memory"), "a machine this small keeps nothing for Doom, and iddqd says so (%s)", doom_status());
        return;
    }
    CHECK(doom_memory() >= (48u << 20), "this machine sets %u MiB aside for Doom", doom_memory() >> 20);
    type_slowly("iddqd"); run(100);
    CHECK(!doom_showing() && strstr(doom_status(), "no WAD"), "iddqd with no WAD on the stick says so (%s)", doom_status());
    static const uint8_t empty[12] = { 'I', 'W', 'A', 'D', 0, 0, 0, 0, 12, 0, 0, 0 };
    bool put = put_file("DOOM.WAD", empty, sizeof empty);
    type_slowly("iddqd"); run(300);
    CHECK(put && !doom_showing() && strstr(doom_status(), "stopped") && app_page() == PAGE_PLAY && gfx_rgb(C_BG) == bg,
          "a WAD that isn't one: Doom stops with its error and BARE!'s screen is back (%s)", doom_status());
    FILE *f = fopen(FREEDOOM, "rb");
    if (!f) { printf("  (no %s: make freedoom fetches it; the rest of Doom's checks are skipped)\n", FREEDOOM); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *wad = malloc((size_t)n); size_t got = fread(wad, 1, (size_t)n, f); fclose(f);
    put = got == (size_t)n && put_file("DOOM.WAD", wad, (uint32_t)n);
    static uint8_t pal[768]; const uint8_t *pp = wad_lump(wad, n, "PLAYPAL"); if (pp) memcpy(pal, pp, 768);
    free(wad);
    type_slowly("iddqd"); run(3000);
    CHECK(put && doom_showing() && gametic >= 80, "iddqd starts Doom afresh after the error: %d tics in its first 3 s", gametic);
    int same = 0; const uint8_t *g = gammatable[0];              /* the engine's gamma table: its level 0 adds one */
    for (int i = 0; i < 256; i++) same += gfx_rgb((uint8_t)i) == (uint32_t)(g[pal[3 * i]] << 16 | g[pal[3 * i + 1]] << 8 | g[pal[3 * i + 2]]);
    CHECK(same == 256, "its palette has the screen (%d of 256 colours are the WAD's)", same);
    run(4000);
    CHECK(doom_level() > 0, "its music plays on the DOOM channel");
    host_tap(KEY_ESC); run(300);
    CHECK(menuactive, "Esc opens its menu");
    host_tap(KEY_ENTER); run(300); host_tap(KEY_ENTER); run(300); host_tap(KEY_ENTER); run(3000);
    CHECK(gamestate == 0 && usergame && !menuactive, "New Game, an episode and a skill: a level starts");
    host_key(KEY_UP, true); run(1000); host_key(KEY_UP, false); run(100);
    host_key(KEY_LCTRL, true);
    bool fired = false;                                           /* the pistol's shot comes four tics in */
    for (int ms = 0; ms < 400; ms++) { run(1); for (int ch = 0; ch < DOOM_SFX_CHANNELS; ch++) fired |= doomsnd_sfx_playing(ch); }
    host_key(KEY_LCTRL, false); run(300);
    CHECK(fired, "Ctrl fires: the pistol's sound, the WAD's own sample, plays");
    host_tap(KEY_ESC); run(200); host_tap('s'); run(120);          /* the menu's own keys: S is Save Game */
    host_tap(KEY_ENTER); run(200); host_tap(KEY_ENTER); run(200);
    type_slowly("bare"); host_tap(KEY_ENTER); run(500);
    struct fat_file sf; char desc[25] = "";
    bool saved = fat_find(&disk.fat, "DOOMSAV0.DSG", &sf) && sf.size > 1000 && fat_read(&disk.fat, &sf, 0, desc, 24);
    CHECK(saved && !strcmp(desc, "BARE") && !fat_find(&disk.fat, "TEMP.DSG", &sf), "a saved game reaches the stick as DOOMSAV0.DSG (\"%s\"), and only that", desc);
    int t0 = gametic;
    host_tap(KEY_F1); run(1000);
    CHECK(!doom_showing() && app_page() == PAGE_PLAY && gfx_rgb(C_BG) == bg, "F1 leaves Doom for PLAY, in BARE!'s colours");
    CHECK(gametic == t0, "its time stands still while it's away (tic %d)", gametic);
    type_slowly("iddqd"); run(300);
    bool paused = doom_showing() && menuactive; int t1 = gametic;
    host_tap(KEY_ESC); run(1000);
    CHECK(paused && doom_showing() && gametic > t1 + 20, "iddqd goes back to it, paused at its menu; Esc plays on (tic %d)", gametic);
    type_slowly("iddqd"); run(300);
    CHECK(doom_showing(), "iddqd typed in Doom is Doom's own cheat, not a restart");
    host_tap(KEY_ESC); run(300); host_tap('q'); run(150);          /* Q is Quit Game */
    host_tap(KEY_ENTER); run(300); host_tap('y'); run(3000);
    CHECK(!doom_showing() && doom_resident(), "its Quit Game leaves it, still there to go back to");
    type_slowly("iddqd"); run(500);
    bool back = doom_showing() && menuactive;
    host_tap('l'); run(150); host_tap(KEY_ENTER); run(300); host_tap(KEY_ENTER); run(2000);   /* L: Load Game */
    CHECK(back && gamestate == 0 && usergame && !menuactive, "back in, at its menu; the saved game loads from the stick");
    host_tap(KEY_F7); run(300);
    CHECK(!doom_showing() && app_page() == PAGE_FILE, "F7 leaves it for FILE");
}

static void splash_checks(void) {
    printf("splash\n");
    to_play(); run(100);
    for (int k = 0; k < SPLASHES; k++) {
        uint64_t px0 = gfx_px_written;
        splash_start(k);
        struct level a = measure(3000);
        bool up = splash_showing();
        run(5000);
        CHECK(up && !splash_showing() && a.rms > 400 && a.clips == 0 && gfx_px_written - px0 > 100000 && amber_in_title() > 100,
              "%-11s sounds (rms %.0f, peak %.0f), draws (%llu px), then the page is back", splash_names[k], a.rms, a.peak,
              (unsigned long long)(gfx_px_written - px0));
    }
    run(2000);
    struct level quiet = measure(300);                 /* the instrument alone, idle: the floor to compare with */
    double floor_ac = quiet.hf;
    splash_start(SPLASH_GENDY); run(2500);
    host_tap('r'); run(40);
    bool gone = !splash_showing();
    run(200);
    struct level after = measure(300);
    double ac = after.hf;
    CHECK(gone && !rhythm.playing && ac < floor_ac + 2, "a key ends it at once; the key doesn't reach the page (the rhythm stays off), and the sound is gone 200 ms later (rms %.1f, idle %.1f)", ac, floor_ac);
    uint32_t theme_rgb[GFX_FREE_COLOR]; for (int i = 0; i < GFX_FREE_COLOR; i++) theme_rgb[i] = gfx_rgb((uint8_t)i);
    long stray = 0;                                                    /* every pixel on screen is one of the scheme's */
    for (uint32_t yy = 0; yy < cfb->height; yy++)
        for (uint32_t xx = 0; xx < cfb->width; xx++) {
            uint32_t p = ((const uint32_t *)((const uint8_t *)cfb->addr + yy * cfb->pitch))[xx] & 0xFFFFFF; bool ok = false;
            for (int i = 0; i < GFX_FREE_COLOR && !ok; i++) ok = p == theme_rgb[i];
            stray += !ok;
        }
    CHECK(stray == 0, "after a key ends it, nothing of the splash is left on screen (%ld pixels)", stray);
    audio_set_mute(true);
    splash_start(SPLASH_META);
    struct level m = measure(3000);
    splash_skip(); run(300); audio_set_mute(false);
    double mac = m.hf;
    CHECK(mac < 1, "muted, the splash is silent too (rms %.1f)", mac);
    bool never = true, all = true;
    for (int last = 0; last < SPLASHES; last++) {
        int seen[SPLASHES] = { 0 };
        for (int i = 0; i < 200; i++) { host_now_ms += 7; int k = splash_pick(last); never &= k != last && k >= 0 && k < SPLASHES; if (k >= 0 && k < SPLASHES) seen[k]++; }
        for (int k = 0; k < SPLASHES; k++) all &= k == last || seen[k] > 0;
    }
    CHECK(never && all, "the pick never repeats the last splash, and finds every other one");
    run(100);
}

static void ui_checks(void) {
    printf("pages\n");
    static const uint8_t keys[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9 };
    for (unsigned k = 0; k < sizeof keys; k++) { host_tap(keys[k]); host_pointer(12000 + k * 2000, 16000, 1); run(200); host_pointer(12000 + k * 2000, 16000, 0); run(100); }
    CHECK(true, "every page drawn, pointer pressed on each");
    to_play(); run(50);
}

/* CHECKS=usb,undo runs only those groups (the names below); without it, all of them */
static bool want(const char *group) {
    const char *only = getenv("CHECKS");
    if (!only || !*only) return true;
    size_t n = strlen(group);
    for (const char *p = only; *p;) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !strncmp(p, group, n)) return true;
        p += len + (e != 0);
    }
    return false;
}

int main(int argc, char **argv) {
    const char *image = argc > 1 ? argv[1] : 0;
    host_log = getenv("HOST_LOG") != 0;
    if (image && !host_disk_open(image)) { printf("cannot open %s\n", image); return 1; }
    static struct fb_info fb; fb = host_fb(1024, 768); cfb = &fb;
    app_init(&fb, 48000);
    run(300);
    if (want("audio")) audio_checks();
    if (want("timing")) timing_checks();
    if (want("stretch")) stretch_checks();
    if (want("keys")) key_checks();
    if (want("project")) project_checks();
    if (want("sampler")) sampler_checks();
    if (want("mix")) mix_checks();
    if (want("tape")) tape_checks();
    if (want("tracker")) tracker_checks();
    if (want("fx")) fx_checks();
    if (want("midi")) midi_checks();
    if (want("fm")) fm_checks();
    if (want("undo")) undo_checks();
    if (want("usb")) { usb_checks(); synaptics_checks(); uvc_checks(); }
    if (want("net")) net_checks();
    if (want("link")) link_checks();
    if (image && want("stick")) stick_checks(image);
    if (image && want("update")) update_checks();
    if (image && want("install")) install_checks();
    if (want("rhythm")) rhythm_checks();
    if (want("layers")) layer_checks();
    if (want("pad")) pad_checks();
    if (want("touch")) touch_checks();
    if (want("colours")) colour_checks();
    if (want("fxpage")) perf_checks();
    if (want("ans")) ans_checks();
    if (want("xen")) { gendy_checks(); sieve_checks(); cloud_checks(); upic_checks(); }
    if (want("inst")) inst_checks();
    if (image && want("doom")) doom_checks();
    if (want("splash")) splash_checks();
    if (want("ui")) ui_checks();
    printf("%d checks, %d failed\n", checks, failures);
    return failures;
}
