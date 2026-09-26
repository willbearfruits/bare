/* Stretcher quality: play something, freeze it, and compare what comes out with what went in — level, how much of
   the output is still the played pitch, DC, stereo. Usage: stretchq OUTDIR */
#include "host.h"
#include "app.h"
#include "synth.h"
#include "stretch.h"
#include "keys.h"
#include <math.h>
#include <stdlib.h>

static double rms_of(const int16_t *d, size_t frames, int ch) {
    double s = 0; for (size_t i = 0; i < frames; i++) { double x = d[2 * i + ch]; s += x * x; } return sqrt(s / frames);
}
static int16_t *read_wav(const char *p, size_t *frames) {
    FILE *f = fopen(p, "rb"); fseek(f, 0, SEEK_END); long b = ftell(f) - 44; fseek(f, 44, SEEK_SET);
    int16_t *d = malloc((size_t)b); *frames = fread(d, 4, (size_t)b / 4, f); fclose(f); return d;
}

static void trial(const char *dir, const char *name, int preset, const int *notes, int nn) {
    char dry_p[512], wet_p[512];
    snprintf(dry_p, sizeof dry_p, "%s/stretch_%s_dry.wav", dir, name); snprintf(wet_p, sizeof wet_p, "%s/stretch_%s.wav", dir, name);
    if (stretch.frozen) { HOST_FREEZE(); host_run(50); }
    stretch.mix = 100;
    for (int i = 0; i < nn; i++) synth_note_on((uint8_t)notes[i], 100, (uint8_t)preset, (uint16_t)(0x770 + i));
    host_run(1000);
    host_wav_open(dry_p); host_run(500); host_wav_close();          /* the held sound, as played */
    HOST_FREEZE();                                                   /* freeze while it sounds (it works on every page) */
    host_run(1);                                                     /* (keys are handled by the next main-loop pass) */
#ifndef HB_BASELINE
    /* hold the read window on the sustained end of the phrase, so the level compares like with like */
    stretch.stay = true; stretch.pos = (stretch.region_start + stretch.region_len - stretch.win) % stretch.cap_len;
#endif
    host_run(100);
    for (int i = 0; i < nn; i++) synth_note_off_tag((uint16_t)(0x770 + i));
    host_run(1500);                                                  /* the dry sound is gone: only the stretch remains */
    host_wav_open(wet_p); host_run(3000); host_wav_close();
    size_t nd, nw; int16_t *d = read_wav(dry_p, &nd), *w = read_wav(wet_p, &nw);
    double dr = rms_of(d, nd, 0), wl = rms_of(w, nw, 0), wr = rms_of(w, nw, 1), sum = 0, sl = 0, sr = 0, dc = 0;
    for (size_t i = 0; i < nw; i++) { double l = w[2 * i], r = w[2 * i + 1]; sum += l * r; sl += l * l; sr += r * r; dc += l; }
    printf("  %-6s dry %6.1f dBFS  stretched L %6.1f R %6.1f dBFS (%+.1f dB)  dc %6.1f  L/R corr %+.3f\n", name,
           20 * log10(dr / 32768), 20 * log10(wl / 32768), 20 * log10(wr / 32768), 20 * log10((wl + wr) / 2 / dr), dc / nw, sum / sqrt(sl * sr));
    free(d); free(w);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    static struct fb_info fb; fb = host_fb(800, 600);
    app_init(&fb, 48000);
    HOST_SET_ECHO(false);
    host_tap(KEY_F4); host_run(50);
    printf("stretcher (window %u, x%u):\n", stretch.win, stretch.factor);
    static const int a4[] = { 69 }, chord[] = { 48, 52, 55, 60 };
    trial(dir, "sine", P_DRAWN, a4, 1);
    trial(dir, "chord", P_FATSAW, chord, 4);
    return 0;
}
