/* DSP cost, in CPU cycles per output frame (one stereo sample), measured with rdtsc on the host.
   Built with the kernel's code-generation flags (i686, no SSE, integer only), so relative numbers carry over.
   Every figure is the fastest of several runs of the same deterministic workload, so a busy machine only adds noise
   upward and the minimum stays comparable between builds. Usage: bench [reps] */
#include "host.h"
#include "ui.h"
#include "app.h"
#include "synth.h"
#include "sampler.h"
#include "mix.h"
#include "fx.h"
#include "seq.h"
#include "stretch.h"
#include "keys.h"
#include <stdlib.h>
#include <string.h>

#define CHUNK 128
static int16_t buf[CHUNK * 2];
static int reps = 9;

static void settle(void) { synth_all_off(); for (int i = 0; i < 48 * 200 / CHUNK; i++) HOST_RENDER(buf, CHUNK); }

/* cycles for `frames` frames of whatever is sounding, rendered in HDA-sized chunks */
static uint64_t run(int frames) {
    uint64_t total = 0;
    for (int done = 0; done < frames; done += CHUNK) {
        uint64_t t0 = host_rdtsc();
        HOST_RENDER(buf, CHUNK);
        total += host_rdtsc() - t0;
    }
    return total;
}

static double idle_cost(void) {
    uint64_t best = ~0ull;
    for (int r = 0; r < reps; r++) { settle(); uint64_t t = run(48 * 100); if (t < best) best = t; }
    return (double)best / (48 * 100);
}

/* 8 voices of one preset, 30 ms from note-on */
static double preset_cost(int p, double idle) {
    uint64_t best = ~0ull;
    for (int r = 0; r < reps; r++) {
        settle();
        for (int v = 0; v < 8; v++) synth_note_on((uint8_t)(48 + v * 3), 100, (uint8_t)p, (uint16_t)(0x700 + v));
        uint64_t t = run(48 * 30);
        if (t < best) best = t;
    }
    return ((double)best / (48 * 30) - idle) / 8;
}

static double full_cost(void) {
    uint64_t best = ~0ull;
    for (int r = 0; r < reps; r++) {
        settle();
        for (int v = 0; v < 8; v++) {
            synth_note_on((uint8_t)(48 + v), 100, P_FM1, (uint16_t)(0x700 + v));
            synth_note_on((uint8_t)(55 + v), 100, P_FATSAW, (uint16_t)(0x710 + v));
            synth_note_on((uint8_t)(60 + v), 100, P_DRAWN, (uint16_t)(0x720 + v));
        }
        uint64_t t = run(48 * 100);
        if (t < best) best = t;
    }
    return (double)best / (48 * 100);
}

/* the first demo (BARE METAL) from its first step with a held chord, through the app (sequencer included) */
static double song_cost(void) {
    uint64_t best = ~0ull;
    for (int r = 0; r < reps; r++) {
        host_key('4', false); host_tap(KEY_ESC); host_run(100); settle();
        host_tap(KEY_F2); HOST_DEMO(0); host_run(1); host_tap(KEY_F1); host_run(1); play_show(0); host_key('4', true);
        host_run(1);
        uint64_t total = 0;
        for (int ms = 0; ms < 2000; ms++) {
            host_now_ms++;
            uint64_t t0 = host_rdtsc();
            host_audio_ms();
            total += host_rdtsc() - t0;
            app_step(host_now_ms);
        }
        if (total < best) best = total;
    }
    return (double)best / (2000 * 48);
}

/* stretcher: freeze, then the main-loop work that keeps its output flowing */
static double stretch_cost(void) {
    uint64_t best = ~0ull;
    for (int r = 0; r < reps; r++) {
        host_tap(KEY_F4); host_run(20);
        if (stretch.frozen) { host_tap(KEY_SPACE); host_run(20); }
        host_tap(KEY_SPACE); host_run(1);
        uint64_t total = 0;
        for (int ms = 0; ms < 1000; ms++) {
            host_now_ms++;
            host_audio_ms();
            uint64_t t0 = host_rdtsc();
            stretch_work();
            total += host_rdtsc() - t0;
        }
        if (total < best) best = total;
    }
    host_tap(KEY_SPACE); host_run(20);
    return (double)best / (1000 * 48);
}

/* two samples to measure: 16-bit at 48 kHz (interpolated) and 8-bit at 16 kHz (held) */
static void fill_samples(void) {
    for (int k = 0; k < 2; k++) {
        struct sample *s = &samples[k];
        if (!s->data) return;
        s->bits = k ? 8 : 16; s->rate_i = k ? 3 : 0; s->rate = sampler_rate_hz(s->rate_i);
        s->len = s->end = s->rate * 2 < sampler_capacity(k) ? s->rate * 2 : sampler_capacity(k);
        for (uint32_t i = 0; i < s->len; i++) {
            int32_t x = (int32_t)((i * 997u) % 2000) * 30 - 30000;
            if (k) ((int8_t *)s->data)[i] = (int8_t)(x >> 8); else ((int16_t *)s->data)[i] = (int16_t)x;
        }
        snprintf(s->name, sizeof s->name, k ? "8BIT 16K" : "16BIT 48K");
        s->gen++;
    }
}

#ifndef HB_BASELINE
#include "splash.h"
#include "ans.h"
#include "cloud.h"
#include "upic.h"
/* the boot's splash pieces: their sound's cost over the whole piece, on average and in the worst 128-frame chunk */
static void splash_cost(double idle) {
    for (int k = 0; k < SPLASHES; k++) {
        uint64_t best = ~0ull, best_peak = ~0ull;
        const int frames = 48 * 6400;
        for (int r = 0; r < 3; r++) {
            settle();
            splash_start(k);
            uint64_t total = 0, peak = 0;
            for (int done = 0; done < frames; done += CHUNK) {
                uint64_t t0 = host_rdtsc(); HOST_RENDER(buf, CHUNK); uint64_t d = host_rdtsc() - t0;
                total += d; if (d > peak) peak = d;
            }
            splash_skip();
            for (int i = 0; i < 48 * 400 / CHUNK; i++) HOST_RENDER(buf, CHUNK);
            if (total < best) best = total;
            if (peak < best_peak) best_peak = peak;
        }
        printf("  %-12s %7.1f cycles/frame on average, %7.1f in the worst chunk\n", splash_names[k], (double)best / frames - idle, (double)best_peak / CHUNK - idle);
    }
}
#endif

int main(int argc, char **argv) {
    if (argc > 1) reps = atoi(argv[1]);
    static struct fb_info fb; fb = host_fb(800, 600);
    app_init(&fb, 48000);
    double idle = idle_cost();
    printf("idle (no voices, echo on)            %7.1f cycles/frame\n", idle);
    printf("per voice, cycles per voice per frame (8 voices, idle subtracted):\n");
    double sum = 0;
    fill_samples();
    int np = 0;
    for (int p = 0; p < P_COUNT; p++) {
        if (synth_preset(p)->wave == WAVE_SAMPLE && !samples[synth_preset(p)->src].len) continue;
        double c = preset_cost(p, idle); printf("  %-10s %7.1f\n", synth_preset_name(p), c); sum += c; np++;
    }
    printf("  mean       %7.1f\n", sum / np);
    printf("24 voices (8 FM, 8 FAT SAW, 8 DRAWN)  %7.1f cycles/frame\n", full_cost());
    printf("song (first demo + held chord)       %7.1f cycles/frame\n", song_cost());
    for (int c = 0; c < MIX_CHANNELS; c++) mix.ch[c].reverb = 50;
    fx.filter_on = fx.drive_on = fx.crush_on = true;
    printf("  with the reverb and every insert on   %7.1f cycles/frame\n", song_cost());
    for (int c = 0; c < MIX_CHANNELS; c++) mix.ch[c].reverb = 0;
    fx.filter_on = fx.drive_on = fx.crush_on = false;
    printf("stretch work (window %u)           %7.1f cycles per output frame\n", stretch.win, stretch_cost());
#ifndef HB_BASELINE
    {                                                                   /* the ANS plate: every row sounding at once */
        for (int i = 0; i < ANS_ROWS * ANS_COLS; i++) ans.plate[i] = (uint8_t)(120 + (i * 37) % 120);
        ans.playing = true; settle(); for (int i = 0; i < 48 * 100 / CHUNK; i++) HOST_RENDER(buf, CHUNK);
        uint64_t best = ~0ull;
        for (int r = 0; r < reps; r++) { uint64_t t = run(48 * 200); if (t < best) best = t; }
        printf("ANS, all %d tones sounding               %7.1f cycles/frame\n", ANS_ROWS, (double)best / (48 * 200) - idle);
        ans.playing = false; ans_clear();
    }
    {                                                                   /* clouds: A dense and short, B gliding (voices included) */
        cloud_defaults(); clouds[0].density = 80; clouds[0].on = clouds[1].on = true;
        settle(); for (int i = 0; i < 48 * 300 / CHUNK; i++) HOST_RENDER(buf, CHUNK);
        uint64_t best = ~0ull;
        for (int r = 0; r < reps; r++) { uint64_t t = run(48 * 300); if (t < best) best = t; }
        printf("clouds A (50 a second) and B (gliding)   %7.1f cycles/frame, %d voices\n", (double)best / (48 * 300) - idle, synth_active_voices());
        cloud_defaults();
        synth_all_off(); settle();
    }
    {                                                                   /* UPIC: a full page, a dozen arcs under the cursor */
        upic_clear(); upic.bars = 0; upic.seconds = 8;
        for (int a = 0; a < UPIC_ARCS; a++) {
            struct upic_pt pt[8];
            for (int i = 0; i < 8; i++) pt[i] = (struct upic_pt){ (uint16_t)(a * 330 + i * 600), (uint16_t)((40 + (a * 7 + i * 3) % 40) * 256) };
            upic_add(pt, 8, P_GD1, 90);
        }
        upic.seek = 1; upic.playing = true; settle(); for (int i = 0; i < 48 * 300 / CHUNK; i++) HOST_RENDER(buf, CHUNK);
        uint64_t best = ~0ull;
        for (int r = 0; r < reps; r++) { uint64_t t = run(48 * 300); if (t < best) best = t; }
        printf("UPIC, %d arcs, %d voices sounding     %7.1f cycles/frame\n", upic.narcs, synth_active_voices(), (double)best / (48 * 300) - idle);
        upic.playing = false; upic_clear(); synth_all_off(); settle();
    }
    printf("the boot's splash, its sound (idle subtracted):\n");
    splash_cost(idle);
#endif
    return 0;
}
