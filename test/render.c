/* Offline audio renders with measurements. Writes WAVs to OUTDIR and prints level/clip/DC/stereo figures,
   plus the sequencer's timing accuracy measured from the audio itself.
   Usage: render OUTDIR */
#include "host.h"
#include "doomhost.h"
#include "inst.h"
#include "gendy.h"
#include "app.h"
#include "synth.h"
#include "seq.h"
#include "keys.h"
#include "touch.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *outdir;

struct stats { double peak, rms, dc, corr; long clips; long n; };

/* analyse a 16-bit stereo WAV written by host_wav_* */
static struct stats analyse(const char *path, int16_t **samples_out, long *frames_out) {
    struct stats s = { 0 };
    FILE *f = fopen(path, "rb"); if (!f) return s;
    fseek(f, 0, SEEK_END); long bytes = ftell(f) - 44; fseek(f, 44, SEEK_SET);
    long n = bytes / 4;
    int16_t *d = malloc((size_t)bytes);
    if (fread(d, 4, (size_t)n, f) != (size_t)n) n = 0;
    fclose(f);
    double sl = 0, sr = 0, slr = 0, sum = 0;
    for (long i = 0; i < n; i++) {
        double l = d[2 * i], r = d[2 * i + 1];
        double a = fabs(l) > fabs(r) ? fabs(l) : fabs(r);
        if (a > s.peak) s.peak = a;
        if (a >= 32767) s.clips++;
        sl += l * l; sr += r * r; slr += l * r; sum += (l + r) / 2;
    }
    s.n = n;
    s.rms = n ? sqrt((sl + sr) / (2.0 * n)) : 0;
    s.dc = n ? sum / n : 0;
    s.corr = (sl > 0 && sr > 0) ? slr / sqrt(sl * sr) : 1;
    if (samples_out) *samples_out = d; else free(d);
    if (frames_out) *frames_out = n;
    return s;
}

static double db(double x) { return x > 0 ? 20 * log10(x / 32768.0) : -999; }

static void report(const char *name, struct stats s) {
    printf("  %-14s peak %6.1f dBFS  rms %6.1f dBFS  clipped %5ld  dc %6.1f  L/R corr %.3f\n", name, db(s.peak), db(s.rms), s.clips, s.dc, s.corr);
}

static void path(char *out, size_t cap, const char *name) { snprintf(out, cap, "%s/%s.wav", outdir, name); }

static void run_pump(int ms) { for (int i = 0; i < ms; i++) { host_now_ms++; host_audio_pump_ms(); app_step(host_now_ms); } }

/* touchpad fingers on the TOUCH board: pad centres, and the z that gives a pressure (the page reads z 25..125) */
static int tx(int pad) { int x0, y0, x1, y1; touch_pad_rect(pad, &x0, &y0, &x1, &y1); return (x0 + x1) / 2; }
static int ty(int pad) { int x0, y0, x1, y1; touch_pad_rect(pad, &x0, &y0, &x1, &y1); return (y0 + y1) / 2; }
static int zp(int press) { return 25 + press * 100 / 255; }
static void lift(void) { for (int f = 0; f < 5; f++) host_finger(f, 0, 0, 0, 0); }
static void touch_play(void) {
    for (int ms = 0; ms < 4000; ms += 10) {                          /* OUT and IN-: a light touch, pressing harder */
        int pr = 40 + ms * 190 / 4000;
        host_finger(0, tx(TP_OUT), ty(TP_OUT), zp(pr), 0); host_finger(1, tx(TP_INV), ty(TP_INV), zp(pr), 0); run_pump(10);
    }
    lift(); run_pump(300);
    for (int ms = 0; ms < 3000; ms += 10) {                          /* one finger sliding from OUT across the gap to IN- */
        host_finger(0, tx(TP_OUT) + ms * (tx(TP_INV) - tx(TP_OUT)) / 3000, ty(TP_OUT), zp(170), 0); run_pump(10);
    }
    lift(); run_pump(300);
    host_finger(0, tx(TP_OUT), ty(TP_OUT), zp(140), 0); host_finger(1, tx(TP_INV), ty(TP_INV), zp(140), 0); run_pump(1200);
    host_finger(2, tx(TP_COMP), ty(TP_COMP), zp(140), 0); run_pump(1500);                 /* COMP joins */
    host_finger(2, tx(TP_GND), ty(TP_GND), zp(140), 0); run_pump(1500);                   /* then 0V instead */
    lift(); run_pump(300);
    host_finger(0, tx(TP_OUT), ty(TP_OUT), zp(140), 0); host_finger(1, tx(TP_C1), ty(TP_C1), zp(140), 0);
    host_finger(2, tx(TP_C2), ty(TP_C2), zp(140), 0); run_pump(2500);                     /* OUT, C1, C2: it wobbles */
    lift(); run_pump(300);
    touch.knob[TK_HUM] = 100;                                                              /* IN- alone, the mains up */
    host_finger(0, tx(TP_INV), ty(TP_INV), zp(150), 0); run_pump(1500);
    host_finger(1, tx(TP_OUT), ty(TP_OUT), zp(90), 0); run_pump(1500);
    lift(); run_pump(300);
    touch.knob[TK_HUM] = 15;
    for (int ms = 0; ms < 5000; ms += 10) {                          /* three fingers, their pressure swaying */
        int a = 120 + (int)(80 * sin(ms * 0.0021)), b = 130 + (int)(70 * sin(ms * 0.0033 + 1)), c = 60 + (int)(50 * sin(ms * 0.0051 + 2));
        host_finger(0, tx(TP_OUT), ty(TP_OUT), zp(a), 0); host_finger(1, tx(TP_INV), ty(TP_INV), zp(b), 0);
        host_finger(2, tx(TP_C2) - 1500, ty(TP_C2), zp(c), 0); run_pump(10);
    }
    lift(); run_pump(1000);
}

int main(int argc, char **argv) {
    outdir = argc > 1 ? argv[1] : ".";
    static struct fb_info fb; fb = host_fb(800, 600);
    app_init(&fb, 48000);
    host_run(200);
    char p[512];

    /* 1. every preset: C4 held 0.6 s, released, 0.6 s tail (echo off to hear the voice itself) */
    HOST_SET_ECHO(false);
    path(p, sizeof p, "presets"); host_wav_open(p);
    for (int pr = 0; pr < P_COUNT; pr++) {
        synth_note_on(60, 100, (uint8_t)pr, 0x700);
        host_run(600);
        synth_note_off_tag(0x700);
        host_run(600);
    }
    host_wav_close();
    printf("renders:\n");
    report("presets", analyse(p, 0, 0));
    HOST_SET_ECHO(true);

    /* 2. PLAY page: hold chords, strum both rows */
    path(p, sizeof p, "strum"); host_wav_open(p);
    const char *roots = "4152";
    for (const char *r = roots; *r; r++) {
        host_key((uint8_t)*r, true); host_run(150);
        for (const char *s = "asdfghjkl"; *s; s++) { host_tap((uint8_t)*s); host_run(60); }
        for (const char *s = "zxcvbnm"; *s; s++) { host_tap((uint8_t)*s); host_run(45); }
        host_run(300); host_key((uint8_t)*r, false); host_run(100);
    }
    host_run(1500);
    host_wav_close();
    report("strum", analyse(p, 0, 0));

    /* 3. the demo songs, 8 s each */
    const char *songs[2] = { "metal", "harbor" };
    for (int sidx = 0; sidx < 2; sidx++) {
        host_tap(KEY_F2); host_run(20); HOST_DEMO(sidx); host_run(20);
        path(p, sizeof p, songs[sidx]); host_wav_open(p);
        run_pump(8000);
        host_wav_close();
        host_tap(KEY_SPACE); host_run(500);
        report(songs[sidx], analyse(p, 0, 0));
    }

    /* 4. timing: the first demo, kick track alone, echo off; onsets measured from the audio */
    host_tap(KEY_F2); host_run(20); HOST_DEMO(0); host_run(20);
    host_tap(KEY_SPACE); host_run(300);
    for (int t = 0; t < SEQ_TRACKS; t++) seq.ch[t].mute = t != 2;
    HOST_SET_ECHO(false);
    host_run(500);
    path(p, sizeof p, "timing"); host_wav_open(p);
    host_tap(KEY_SPACE);
    run_pump(12000);
    host_wav_close();
    int16_t *d; long n;
    analyse(p, &d, &n);
    double onsets[256]; int no = 0; long quiet = 0;
    for (long i = 0; i < n && no < 256; i++) {
        int a = abs(d[2 * i]);
        if (a < 150) quiet++;
        else { if (a > 2000 && quiet > 48 * 30) onsets[no++] = (double)i / 48000.0; if (a > 2000) quiet = 0; }
    }
    double beat = 60.0 / seq.bpm, worst = 0, sq = 0; int ni = 0;
    for (int i = 1; i < no; i++) {
        double err = (onsets[i] - onsets[i - 1]) - beat;
        sq += err * err; ni++;
        if (fabs(err) > worst) worst = fabs(err);
    }
    printf("timing: %d kick onsets at %u BPM, beat %.3f ms: interval error rms %.3f ms, worst %.3f ms\n",
           no, seq.bpm, beat * 1000, ni ? sqrt(sq / ni) * 1000 : 0, worst * 1000);
    free(d);

    /* 4. the TOUCH page, played on the touchpad: about 24 s of the circuit */
    host_tap(KEY_SPACE); host_tap(KEY_ESC); host_run(300);
    HOST_SET_ECHO(false);
    host_tap(KEY_F9); host_run(300);
    path(p, sizeof p, "touch"); host_wav_open(p);
    touch_play();
    host_wav_close();
    report("touch", analyse(p, 0, 0));

    /* 5. GENDY: each patch plays A2, E3 and A3 coming in one by one; then a drone on the first, stirred from calm to a
       storm and back (the heights' and lengths' steps, as the GENDY view's pad moves them) */
    host_tap(KEY_ESC); host_run(300);
    path(p, sizeof p, "gendy"); host_wav_open(p);
    static const uint8_t chord[3] = { 45, 52, 57 };
    for (int k = 0; k < GENDY_PATCHES; k++) {
        for (int i = 0; i < 3; i++) { synth_note_on(chord[i], 100, (uint8_t)(P_GD1 + k), (uint16_t)(0x710 + i)); host_run(700); }
        host_run(1200);
        for (int i = 0; i < 3; i++) synth_note_off_tag((uint16_t)(0x710 + i));
        host_run(700);
    }
    struct gendy_patch keep = gendy_bank[0];
    synth_note_on(45, 100, P_GD1, 0x720);
    for (int t = 0; t <= 800; t++) { int s = 5 + (t < 400 ? t : 800 - t) * 90 / 400; gendy_bank[0].astep = (uint8_t)s; gendy_bank[0].dstep = (uint8_t)s; host_run(10); }
    synth_note_off_tag(0x720); host_run(900);
    gendy_bank[0] = keep;
    host_wav_close();
    report("gendy", analyse(p, 0, 0));

    /* 6. SHRUTI: Sa, Pa and the high Sa opened, the bellows pumped by hand (a push every 0.8 s), then left to breathe out */
    {
        int sr = -1; for (int i = 0; i < inst_count; i++) if (!strcmp(insts[i].name, "SHRUTI")) sr = i;
        if (sr >= 0) {
            inst_select(sr);
            path(p, sizeof p, "shruti"); host_wav_open(p);
            static const int reeds[3] = { 48, 55, 60 };
            for (int k = 0; k < 3; k++) { inst_note(600 + k, reeds[k] * 256, 105, true); inst_note(600 + k, 0, 0, false); }
            for (int t = 0; t < 14000; t += 16) { if (t < 9000 && t % 800 < 320) inst_pump(1300); host_run(16); }
            inst_clear(); host_run(1500);
            host_wav_close(); inst_select(-1);
            report("shruti", analyse(p, 0, 0));
        }
    }

    /* 7. Doom's music as BARE!'s voices play it: E1M1 of Freedoom (make freedoom), 40 s */
    FILE *wf = fopen("build/freedoom/freedoom-0.13.0/freedoom1.wad", "rb");
    if (wf) {
        static uint8_t events[1 << 20];
        fseek(wf, 0, SEEK_END); long n = ftell(wf); fseek(wf, 0, SEEK_SET);
        uint8_t *w = malloc((size_t)n);
        if (fread(w, 1, (size_t)n, wf) == (size_t)n) {
            uint32_t lumps = *(uint32_t *)(w + 4), dir = *(uint32_t *)(w + 8);
            for (uint32_t i = 0; i < lumps; i++) {
                const uint8_t *e = w + dir + 16 * i;
                if (strncmp((const char *)e + 8, "D_E1M1", 8)) continue;
                doomsnd_set_memory(events, sizeof events);
                doomsnd_music_volume(100);
                if (!doomsnd_music_load(w + *(uint32_t *)e, *(uint32_t *)(e + 4))) break;
                path(p, sizeof p, "doom-e1m1"); host_wav_open(p);
                doomsnd_music_play(false);
                host_run(40000);
                doomsnd_all_off(); host_run(1000);
                host_wav_close();
                report("doom-e1m1", analyse(p, 0, 0));
                break;
            }
        }
        free(w); fclose(wf);
    }
    return 0;
}
