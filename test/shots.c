/* Screenshots of every page at a given resolution, plus UI cost: framebuffer pixels written per second and
   main-loop cycles per frame while the first demo plays and a chord is held.
   Usage: shots W H OUTDIR [prefix]; HOST_THEME=N draws them in colour scheme N. */
#include "host.h"
#include "disk.h"
#include "xen.h"
#include "lineage.h"
#include "meta.h"
#include "drone.h"
#include "phase.h"
#include "upic.h"
#include "inst.h"
#include "synth.h"
#include "app.h"
#include "keys.h"
#include "gfx.h"
#ifndef HB_BASELINE
#include "fkeys.h"
#endif
#include <stdlib.h>
#include <string.h>

extern uint64_t text_px_written __attribute__((weak));   /* baseline (patched) text layer */
extern uint64_t gfx_px_written __attribute__((weak));    /* current graphics layer */
static uint64_t px_written(void) { return &gfx_px_written ? gfx_px_written : &text_px_written ? text_px_written : 0; }

static struct fb_info fb;
static const char *outdir, *prefix = "";

static void shot(const char *name) {
    char path[512]; snprintf(path, sizeof path, "%s/%s%s.ppm", outdir, prefix, name);
    host_write_ppm(path, &fb);
}

/* run ms of simulated time and report UI cost over it */
static void measure(const char *name, int ms) {
    uint64_t px0 = px_written(), cyc = 0; int frames = 0;
    for (int i = 0; i < ms; i++) {
        host_now_ms++;
        host_audio_ms();
        uint64_t t0 = host_rdtsc();
        app_step(host_now_ms);
        uint64_t dt = host_rdtsc() - t0;
        if (host_now_ms % 16 == 0) { cyc += dt; frames++; }
        else cyc += dt;
    }
    double px_s = (double)(px_written() - px0) * 1000.0 / ms;
    printf("  %-10s fb %8.0f KB/s   %6.0f Kcycles/frame\n", name, px_s * 4 / 1024, frames ? (double)cyc / frames / 1000 : 0);
}

static void page_key(uint8_t k) { host_tap(k); host_run(40); }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: shots W H OUTDIR [prefix]\n"); return 1; }
    int w = atoi(argv[1]), h = atoi(argv[2]); outdir = argv[3]; if (argc > 4) prefix = argv[4];
    host_log = getenv("HOST_LOG") != 0;
    fb = host_fb(w, h);
    const char *stick = getenv("HOST_IMAGE");                 /* a stick image: FILE shows its projects (a few are saved) */
    if (stick && !host_disk_open(stick)) { fprintf(stderr, "cannot open %s\n", stick); return 1; }
    host_pointer(16384, 16384, 0);
    app_init(&fb, 48000);
#ifndef HB_BASELINE
    if (getenv("HOST_THEME")) gfx_theme(atoi(getenv("HOST_THEME")));
#endif
    host_run(300);
    printf("%dx%d\n", w, h);

    /* start the first demo, hold C major, strum */
    page_key(KEY_F2); HOST_DEMO(0); host_run(40);
    page_key(KEY_F1);
    host_key('6', true); host_key('h', true); host_run(200);                     /* C and its 7th: Cmaj7 */
    const char *strum = "zxcvbn";
    for (const char *s = strum; *s; s++) { host_key((uint8_t)*s, true); host_run(60); host_key((uint8_t)*s, false); host_run(30); }
    host_run(100);
    shot("play");
    measure("play", 2000);
    host_key('6', false); host_key('h', false);
#ifndef HB_BASELINE
    host_tap(KEY_CAPS); host_key('a', true); host_key('d', true); host_run(300);   /* keyboard mode: C and E held */
    shot("play-keyboard");
    host_key('a', false); host_key('d', false); host_tap(KEY_CAPS); host_run(100);
#endif
    /* the instruments from files, each played: F1 again steps to them */
    for (int k = 1; k <= inst_count; k++) {
        play_show(k); host_run(60);
        const struct inst *in = &insts[k - 1];
        if (in->strip_hi) host_finger(0, 9000 + k * 3000, 16000, 90, 0);
        else if (in->pads_w) { host_finger(0, 6000, 26000, 80, 0); host_finger(1, 20000, 12000, 80, 0); }
        else { host_key('z', true); host_key('c', true); host_key('b', true); }
        if (in->bellows) inst_pump(32767);                   /* air in the bellows */
        host_run(900);
        char nm[24]; snprintf(nm, sizeof nm, "inst-%s", in->name);
        for (char *c = nm; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        shot(nm);
        host_finger(0, 0, 0, 0, 0); host_finger(1, 0, 0, 0, 0); host_key('z', false); host_key('c', false); inst_latch(false); host_run(100);
    }
    play_show(0); host_run(50);
    page_key(KEY_F2); host_run(200); shot("seq"); measure("seq", 2000);
    page_key(KEY_F3); host_run(200); shot("wave"); measure("wave", 2000);
    host_tap(KEY_TAB); host_run(200); shot("wave-harmonics");
    host_tap(KEY_TAB); host_run(200); shot("wave-draw");
    host_tap(KEY_TAB); host_tap('a'); host_key('z', true); host_key('b', true); host_run(400);   /* grab the strum, play it */
    shot("wave-sample"); measure("wave-sample", 2000);
    host_key('z', false); host_key('b', false); host_tap(KEY_TAB);
    page_key(KEY_F4); host_tap(KEY_SPACE); host_run(1200); shot("stretch"); measure("stretch", 2000);
    host_tap(KEY_SPACE);
    page_key(HOST_KEY_FM); host_tap('a'); host_run(300); shot("fm"); measure("fm", 2000);
    if (stick) {
#ifndef HB_BASELINE
        static const char *const names[] = { "FIRST LIGHT", "HARBOR SKETCH", "SHRUTI DRONE", "UPIC STUDY 2" };
        for (int i = 0; i < 4; i++) disk_save_slot(i, names[i]);
#endif
    }
    page_key(HOST_KEY_FILE); host_run(200); shot("file"); measure("file", 2000);
    page_key(HOST_KEY_TAPE); host_run(200); shot("tape"); measure("tape", 2000);
#ifndef HB_BASELINE
    host_tap('q'); host_tap('r'); host_key('6', true);                          /* a few seconds on track 1, played back */
    for (int i = 0; i < 12; i++) { host_tap((uint8_t)"zxcvbnm,./zx"[i]); host_run(250); }
    host_key('6', false); host_tap('r'); host_tap(KEY_SPACE); host_tap('q'); host_tap(KEY_HOME); host_tap(KEY_SPACE); host_run(1500);
    shot("tape-playing"); measure("tape-playing", 2000);
    host_tap(KEY_SPACE);
    page_key(KEY_F8); host_run(200); shot("mix"); measure("mix", 2000);
    page_key(KEY_F9);                                                               /* two fingers on the pad, a key held */
    host_finger(0, 4096, 8192, 110, 0); host_finger(1, 12288, 8192, 110, 0); host_key('c', true); host_run(600);
    shot("touch"); measure("touch", 2000);
    host_finger(0, 0, 0, 0, 0); host_finger(1, 0, 0, 0, 0); host_key('c', false);
    page_key(KEY_F10);                                                              /* a repeat held, a second finger on the filter */
    host_key('2', true); host_finger(0, 24000, 9000, 110, 0); host_run(400);
    host_finger(1, 9000, 13000, 100, 0); host_run(500);
    shot("fx"); measure("fx", 2000);
    host_finger(0, 0, 0, 0, 0); host_finger(1, 0, 0, 0, 0); host_key('2', false); host_run(100);
    page_key(KEY_F11);                                                              /* the camera's picture, a glissando, playing */
    host_tap(KEY_ENTER); host_run(100);
    for (int i = 0; i <= 40; i++) { host_pointer((w * 3 / 16 + i * w / 90) * 32768 / w, (h * 3 / 4 - i * h / 110) * 32768 / h, 1); host_run(16); }
    host_pointer((w * 3 / 16 + 40 * w / 90) * 32768 / w, (h * 3 / 4 - 40 * h / 110) * 32768 / h, 0); host_run(40);
    host_tap(KEY_SPACE); host_run(1500);
    shot("ans"); measure("ans", 2000);
    host_tap(KEY_SPACE); host_run(50);
    static const char *const lin_shots[LV_COUNT] = { [LV_REICH] = "lin-reich", [LV_CARLOS] = "lin-carlos", [LV_RADIGUE] = "lin-radigue",
                                                     [LV_MERZBOW] = "lin-merzbow" };
    for (int v = LV_REICH; v < LV_COUNT; v++) {
        lineage_goto(v); host_run(200);
#ifndef HB_BASELINE
        if (v == LV_CARLOS) { host_key('z', true); host_key('c', true); host_key('b', true); host_run(400); }   /* a chord in alpha */
        if (v == LV_REICH) {                                     /* three players, the second and third pulling ahead */
            reich.players = 3; reich.hold = 1; reich.move = 2; host_tap(KEY_SPACE); host_run(3000);
        }
        if (v == LV_RADIGUE) {                                   /* ninety seconds of the drone, the base gliding down to G1 half way */
            radigue.fade_s = 5; host_tap(KEY_SPACE); host_run(40000);
            radigue.sweep_s = 30; drone_sweep_to(31000); host_run(50000);
        }
        if (v == LV_MERZBOW) {                                   /* 4 s: a finger scraping in strokes, junk struck, feedback at the end */
            for (int i = 0; i < 200; i++) {
                if (i % 30 == 0) host_tap((uint8_t)"qg.xp;"[i / 30 % 6]);
                if (i == 150) host_key(KEY_SPACE, true);
                if (i % 50 < 42) host_finger(0, 3000 + abs((i * 900) % 52000 - 26000), 5000 + i * 110 + (i % 7) * 400, 70 + i % 50, 0);
                else host_finger(0, 0, 0, 0, 0);
                host_run(20);
            }
        }
#endif
        shot(lin_shots[v]); measure(lin_shots[v], 1000);
#ifndef HB_BASELINE
        if (v == LV_CARLOS) { host_key('z', false); host_key('c', false); host_key('b', false); }
        if (v == LV_RADIGUE) drone_defaults();
        if (v == LV_REICH) phase_defaults();
        if (v == LV_MERZBOW) { host_key(KEY_SPACE, false); host_finger(0, 0, 0, 0, 0); host_tap(KEY_BACKSPACE); }
#endif
        host_run(100);
    }
    page_key(KEY_F12);                                                              /* XENAKIS: METASTASEIS, playing */
#ifndef HB_BASELINE
    meta.pos = (uint32_t)20000 << 16; meta.playing = true; host_run(1500);
    shot("xen-meta"); measure("xen-meta", 2000);
    meta.playing = false; host_run(100);
#endif
    xen_goto(XV_GENDY); host_run(40);                                               /* GENDY, a chord held */
    host_key('z', true); host_key('b', true); host_run(600);
    shot("xen-gendy"); measure("xen-gendy", 2000);
    host_key('z', false); host_key('b', false); host_run(50);
    xen_goto(XV_UPIC); host_run(40);                                                                 /* UPIC: a fan, a run, a cloud */
    upic_clear(); upic.bars = 4;
    for (int i = 0; i < 12; i++) {
        struct upic_pt f[3] = { { 2000, 60 * 256 }, { (uint16_t)(20000 + i * 900), (uint16_t)((38 + i * 4) * 256) }, { (uint16_t)(30000 + i * 700), (uint16_t)((40 + i * 4) * 256) } };
        upic_add(f, 3, (uint8_t)(P_GD1 + i % 4), 100);
    }
    struct upic_pt run_[16]; for (int i = 0; i < 8; i++) { run_[2 * i] = (struct upic_pt){ (uint16_t)(36000 + i * 2400), (uint16_t)((72 + i * 2) * 256) }; run_[2 * i + 1] = (struct upic_pt){ (uint16_t)(36000 + i * 2400 + 2399), (uint16_t)((72 + i * 2) * 256) }; }
    upic_add(run_, 16, P_BELL, 110);
    xen_goto(XV_CLOUDS); host_run(40);                                                              /* cloud B written onto the page */
    host_tap(KEY_TAB); host_tap(KEY_ENTER); host_tap(KEY_TAB); host_tap(KEY_TAB); host_tap(KEY_TAB); host_run(40);
    xen_goto(XV_UPIC); host_run(40); host_tap(KEY_SPACE); host_run(3100);
    shot("xen-upic"); measure("xen-upic", 2000);
    host_tap(KEY_SPACE); host_run(50);
    xen_goto(XV_CLOUDS); host_tap('1'); host_tap('2'); host_tap('4'); host_run(5200);                 /* three clouds */
    shot("xen-clouds"); measure("xen-clouds", 2000);
    host_tap('0'); host_run(50);
    xen_goto(XV_SIEVES); host_tap(KEY_DOWN); host_tap(KEY_TAB); host_tap(KEY_SPACE); host_run(2300);   /* the SIEVE rhythm playing */
    shot("xen-sieves"); measure("xen-sieves", 2000);
    host_tap(KEY_SPACE); host_run(50);
    /* the keys made one's own (F2 SHRUTI, F3 UPIC, F6 off, F9 CLOUDS): FILE's KEYS view, then UPIC's tab being dragged */
    static const char keys[] = "F2 SHRUTI\nF3 UPIC\nF6 off\nF9 CLOUDS\n";
    char err[80]; fkeys_parse(keys, (int)sizeof keys - 1, fkeys, err, sizeof err);
    page_key(KEY_F2); host_run(100);
    file_show_keys(); page_key(KEY_F7); host_tap(KEY_DOWN); host_tap(KEY_DOWN); host_run(200);
    shot("file-keys");
    int tx, tw; ui_tab(2, &tx, &tw);
    int py = text_font()->height / 2 * 32768 / h, px0 = (text_px(tx) + 4) * 32768 / w;
    host_pointer(px0, py, 1); host_run(40);
    host_pointer(px0 - 8 * text_font()->width * 32768 / w, py, 1); host_run(40);
    host_pointer(px0 - 14 * text_font()->width * 32768 / w, py, 1); host_run(40);
    shot("keys-drag");
    host_pointer(px0 - 14 * text_font()->width * 32768 / w, 20000, 0); host_run(40);   /* let go below the bar: nothing moves */
    fkeys_default(fkeys);
#endif
    host_key('4', false);
    return 0;
}
