/* A 70-second showcase, played by a script through the instrument on the host and encoded with ffmpeg — picture
   and sound come from the same simulated clock, so they line up exactly.  Usage: video OUT.mp4
   With --splash N (0..3) it records the boot's splash piece N instead, from the end of app_init, and a moment after;
   with --fx, the FX page played over a demo song; with --ans, the ANS page drawn on and played; with --xen, the
   XENAKIS page; with --doom IMAGE, iddqd typed on PLAY and Doom played from the stick image (make doom-img); --shruti,
   --touch, --themes, --seq and --file IMAGE the SHRUTI, TOUCH, colour schemes, SEQ and FILE pages; --card TITLE LINE S
   a title card of S seconds in BARE!'s font (tools/showcase.py puts them all together). */
#include "host.h"
#include "app.h"
#include "keys.h"
#include "splash.h"
#include "gfx.h"
#include "text.h"
#include "touch.h"
#include <stdlib.h>
#include <string.h>

#define W 1280
#define H 800
#define FPS 30

/* ---- a script: key and pointer events at times in ms, sorted before the run ---- */
enum { KEYEV, PTREV, TOUCHEV };
struct ev { int t, type, a, b, c, n, f; };                    /* n: insertion order, so the sort is stable; f: a finger */
static struct ev evs[8192]; static int nev;
static void key(int t, uint8_t code, bool down) { evs[nev] = (struct ev){ t, KEYEV, code, down, 0, nev, 0 }; nev++; }
static void note(int t, uint8_t code, int dur) { key(t, code, true); key(t + dur, code, false); }
static void tap(int t, uint8_t code) { note(t, code, 40); }
static void pointer(int t, int x, int y, int buttons) { evs[nev] = (struct ev){ t, PTREV, x * 32768 / W, y * 32768 / H, buttons, nev, 0 }; nev++; }
static void finger(int t, int f, int tx, int ty, int z) { evs[nev] = (struct ev){ t, TOUCHEV, tx, ty, z, nev, f }; nev++; }
static void pad_touch(int t, int tx, int ty, int z) { finger(t, 0, tx, ty, z); }   /* finger 0 on the pad */
static void strum(int t, const char *keys, int step, int dur) { for (int i = 0; keys[i]; i++) note(t + i * step, (uint8_t)keys[i], dur); }
static void chord(int t, uint8_t root, int dur) { note(t, root, dur); }
static void shift_tap(int t, uint8_t code) { key(t, KEY_LSHIFT, true); tap(t, code); key(t + 40, KEY_LSHIFT, false); }
static int cmp(const void *a, const void *b) { const struct ev *x = a, *y = b; return x->t != y->t ? x->t - y->t : x->n - y->n; }

static void script(void) {
    /* PLAY: chords and strums, then a strum with the pointer across the plate */
    chord(400, '4', 2200);                       strum(600, "asdfghjkl;'", 110, 60);   strum(1900, "zxcvbnm,./", 80, 60);
    tap(2650, 'w'); chord(2750, '7', 2000);      strum(2900, "asdfghjkl", 100, 60);    strum(3950, "lkjhg", 90, 60);
    tap(4800, 'q'); chord(4900, '3', 1900);      strum(5100, "zxcvbnm", 80, 60);       strum(5700, "asdfgh", 90, 60);
    chord(6900, '5', 1500);                      strum(7000, "asdfghjkl;'", 70, 60);
    chord(8500, '4', 1400);
    for (int i = 0; i <= 44; i++) pointer(8600 + i * 16, 60 + i * 11, 184, i < 44 ? 1 : 0);
    /* SEQ: the HARBOR demo starts; back on PLAY, strum along with its chords (C, Am, F, G — a bar is 2.5 s at 96 BPM) */
    pointer(9600, 900, 600, 0);
    tap(9800, KEY_F2); shift_tap(10300, '2');                   /* ⇧2: the HARBOR demo */
    int bar0 = 10300;
    tap(bar0 + 2 * 2500 - 60, KEY_F1);
    chord(bar0 + 2 * 2500, '3', 2440); strum(bar0 + 2 * 2500, "asdfgfds", 312, 120);
    chord(bar0 + 3 * 2500, '5', 2440); strum(bar0 + 3 * 2500 + 312, "asdfghjkl;'", 156, 80);
    chord(bar0 + 4 * 2500, '4', 2440); strum(bar0 + 4 * 2500, "zxcvbnm,", 312, 120);
    tap(bar0 + 5 * 2500 - 30, 'w'); chord(bar0 + 5 * 2500, '7', 2400); strum(bar0 + 5 * 2500, "asdfghgfdsa", 208, 80);
    tap(bar0 + 6 * 2500 - 30, 'q');
    /* WAVE: draw a cycle with the pen on page 6, play it from the keyboard rows, MORPH on page D, then page 8: grab
       what was just played and play it back as a sample */
    int tw = bar0 + 6 * 2500;                                    /* 25300 */
    tap(tw, KEY_F3); tap(tw + 250, KEY_TAB); tap(tw + 450, KEY_TAB);
    for (int i = 0; i <= 94; i++) {
        int x = 40 + i * 10, u = i * 1000 / 94;                  /* u: 0..1000 across the cycle */
        int s = (u < 500 ? 1 : -1) * (u % 500 < 60 ? u % 500 * 4 : u % 500 > 440 ? (500 - u % 500) * 4 : 240);
        int y = 384 - s + ((i * 37) % 7 - 3) * (u > 500 ? 6 : 0);
        pointer(tw + 600 + i * 16, x, y, i < 94 ? 1 : 0);
    }
    note(tw + 2400, 'z', 500); note(tw + 2900, 'c', 500); note(tw + 3400, 'b', 900);
    tap(tw + 4300, '\''); shift_tap(tw + 4400, KEY_TAB); shift_tap(tw + 4550, KEY_TAB);   /* MORPH, on page D */
    tap(tw + 4700, '4');                                         /* from wave 4 towards wave 8 */
    note(tw + 4900, 'x', 1500); note(tw + 5300, 'v', 1300);
    tap(tw + 6900, KEY_TAB); tap(tw + 7000, KEY_TAB); tap(tw + 7100, KEY_TAB);           /* page 8 */
    tap(tw + 7300, 'a');                                          /* grab the MORPH phrase */
    note(tw + 7700, 'z', 400); note(tw + 8100, 'c', 400); note(tw + 8500, 'b', 400); note(tw + 8900, ',', 700);
    pointer(tw + 9800, 1150, 600, 0);
    /* OPERATOR: the E.PIANO patch, a new algorithm, a slider dragged, feedback */
    int tf = tw + 10000;                                         /* 35300 */
    tap(tf, KEY_F5);
    strum(tf + 300, "adgj", 250, 220);
    tap(tf + 1500, KEY_PGDN); tap(tf + 2000, KEY_PGDN);
    for (int i = 0; i <= 20; i++) pointer(tf + 2400 + i * 30, 812 + i * 4, 136, i < 20 ? 1 : 0);
    strum(tf + 3200, "adgjl", 200, 200);
    for (int i = 0; i < 6; i++) tap(tf + 4500 + i * 120, '=');
    strum(tf + 5400, "zcbm", 220, 250);
    /* STRETCH: stop the sequencer, freeze what was played, slow it further, play on top */
    int ts = tf + 7000;                                          /* 42300 */
    tap(ts, KEY_F4); tap(ts + 300, KEY_ESC); tap(ts + 500, KEY_SPACE);
    tap(ts + 3000, KEY_RIGHT); tap(ts + 3300, KEY_UP);
    chord(ts + 4200, '3', 1800); strum(ts + 4400, "asdfg", 260, 200);
    /* TAPE: arm, record strums over the frozen pad, stop, rewind, play back with wow */
    int tt = ts + 6500;                                          /* 48800 */
    tap(tt, KEY_F6); tap(tt + 300, 'q'); tap(tt + 700, 'r');
    chord(tt + 900, '4', 1900); strum(tt + 1000, "asdfghjkl", 180, 100);
    chord(tt + 2900, '5', 1900); strum(tt + 3000, "lkjhgfdsa", 180, 100);
    tap(tt + 5000, 'r'); tap(tt + 5100, KEY_SPACE); shift_tap(tt + 5300, 'f');
    tap(tt + 5800, KEY_HOME); tap(tt + 6300, 'u');
    tap(tt + 6600, KEY_SPACE);
    /* MIX: the tape plays on through the mixer; the master filter swept down and up under strums */
    int tm = tt + 11500;                                         /* 60300 */
    tap(tm, KEY_F8);
    for (int i = 0; i < 13; i++) tap(tm + 300 + i * 50, KEY_RIGHT);                    /* past ten strips and MASTER to FILTER */
    tap(tm + 1000, KEY_ENTER); tap(tm + 1100, KEY_DOWN);                                  /* on; its cutoff */
    chord(tm + 1200, '4', 1500); strum(tm + 1300, "asdfghjkl;'", 110, 80);
    for (int i = 0; i < 12; i++) tap(tm + 1400 + i * 120, '[');
    chord(tm + 2800, '5', 1500); strum(tm + 2900, "zxcvbnm,./", 120, 80);
    for (int i = 0; i < 14; i++) tap(tm + 2950 + i * 110, ']');
    tap(tm + 4700, KEY_ENTER);                                                            /* filter off */
    /* PLAY, to finish */
    int te = tm + 5000;                                          /* 65300 */
    tap(te, KEY_F1);
    chord(te + 200, '4', 2600); strum(te + 300, "zxcvbnm,./", 60, 60); strum(te + 1000, "asdfghjkl;'", 60, 60);
}
#define END_MS 69500

/* the FX page, played over the HARBOR demo with the mouse on the pad and the number keys (1280x800: the pad spans
   x 24..944, y 160..752) */
static int pad_x(int v) { return 24 + v * 920 / 1000; }
static int pad_y(int v) { return 752 - v * 592 / 1000; }
static void script_fx(void) {
    tap(300, KEY_F2); shift_tap(700, '2');                                    /* HARBOR plays */
    tap(2600, KEY_F10);
    for (int i = 0; i <= 60; i++) { int v = i < 30 ? 500 - i * 15 : 50 + (i - 30) * 30; pointer(3200 + i * 50, pad_x(v), pad_y(550), i < 60 ? 1 : 0); }   /* FILTER: down, then up to high-pass */
    key(6500, '2', true);                                                     /* REPEAT, the finger sliding to shorter slices */
    for (int i = 0; i <= 40; i++) pointer(6500 + i * 50, pad_x(200 + i * 20), pad_y(150), i < 40 ? 1 : 0);
    key(8600, '2', false);
    note(9400, '5', 2200);                                                    /* GATE */
    note(12200, '7', 350);                                                    /* DUB: a throw */
    note(14200, '3', 900);                                                    /* REVERSE */
    note(15800, '8', 2000);                                                   /* FREEZE */
    note(18800, '4', 1800);                                                   /* TAPE STOP */
    tap(21000, KEY_ESC);
}

/* the ANS page: glissandi, a cluster and a drone drawn with the mouse, played; keys written into it as it plays; the
   camera's picture laid over it; then the live camera as the plate (1280x800: the plate spans x 48..1008, y 48..736) */
static int plate_x(int col) { return 48 + col * 960 / 512; }
static int plate_y(int row) { return 736 - row * 688 / 360; }
static void drag(int t, int c0, int r0, int c1, int r1, int ms) {
    int steps = ms / 16;
    for (int i = 0; i <= steps; i++) pointer(t + i * 16, plate_x(c0 + (c1 - c0) * i / steps), plate_y(r0 + (r1 - r0) * i / steps), i < steps ? 1 : 0);
}
static void script_ans(void) {
    key(300, KEY_LCTRL, true); tap(320, '-'); key(400, KEY_LCTRL, false);
    drag(900, 20, 50, 190, 250, 900);                            /* a glissando up */
    drag(2000, 210, 280, 320, 110, 700);                         /* one down */
    drag(2900, 350, 90, 350, 210, 300);                          /* a cluster */
    drag(3400, 5, 36, 505, 36, 1300);                            /* a drone on G2 */
    tap(5000, KEY_SPACE);                                        /* plays: 4 bars at 120, 8 s a pass */
    note(9000, 'z', 900); note(10000, 'c', 900); note(11000, 'b', 900); note(12000, ',', 1400);   /* written in as it plays */
    tap(13800, KEY_ENTER);                                       /* the camera's picture over it */
    tap(17000, '\\');                                            /* the live camera is the plate */
    tap(24500, '\\'); tap(25000, KEY_SPACE);
}

/* the XENAKIS page (1280x800: each view's picture spans x 48..1008, y 48..736; GENDY's x 24..1008): GENDY's patches
   from the letter rows and a drone stirred by the mouse; clouds switched on, one steered, one written onto UPIC's page;
   the sieves' rhythm, examples, a formula typed, a sieve played as a scale; UPIC drawn on, played, scrubbed */
static void stroke(int t, int x0, int y0, int x1, int y1, int ms) {
    int steps = ms / 16;
    for (int i = 0; i <= steps; i++) pointer(t + i * 16, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps + (i % 7 == 3 ? 3 : 0), i < steps ? 1 : 0);
}
static void script_xen(void) {
    shift_tap(100, '='); shift_tap(160, '='); shift_tap(220, '=');           /* the master at 0 dB */
    tap(300, KEY_F12);                                                         /* GENDY */
    note(700, 'z', 1700); note(1100, 'b', 1300);                               /* GENDY3: C3, G3 */
    tap(2500, KEY_END); note(2700, 'x', 1500); note(3000, 'n', 1200);          /* S.709: its pitch walks */
    tap(4400, KEY_END); note(4600, 'z', 1800); note(4800, 'c', 1600); note(5000, 'b', 1400);   /* BREATH */
    tap(6600, KEY_END); note(6800, 'v', 1400); note(7200, 'q', 1000);          /* STORM */
    tap(8400, KEY_END); tap(8600, KEY_SPACE);                                  /* GENDY3 as a drone, stirred */
    for (int i = 0; i <= 150; i++) { int a = i <= 75 ? i : 150 - i; pointer(9000 + i * 22, 84 + a * 820 / 75, 676 - a * 560 / 75, i < 150 ? 1 : 0); }
    tap(12500, KEY_SPACE);
    tap(12900, KEY_F12);                                                       /* CLOUDS */
    tap(13300, '1'); tap(14600, '2');
    for (int i = 0; i <= 120; i++) pointer(16000 + i * 25, 300 + i * 4, 600 - (i % 60) * 7, i < 120 ? 1 : 0);   /* A steered by hand */
    tap(19300, '4');
    tap(20800, KEY_TAB); tap(21000, KEY_ENTER);                                /* B written onto UPIC's page */
    tap(22800, '0');
    tap(23300, KEY_F12);                                                       /* SIEVES */
    tap(23700, KEY_SPACE);                                                     /* the SIEVE rhythm */
    tap(25200, KEY_TAB);                                                       /* S1: an example */
    tap(26400, KEY_DOWN); tap(26500, KEY_DOWN); tap(26700, KEY_ENTER);         /* S3: 3@0|4@0 typed */
    for (int i = 0; i < 12; i++) tap(26900 + i * 60, KEY_BACKSPACE);
    tap(27700, '3'); shift_tap(27900, '2'); tap(28100, '0'); shift_tap(28300, '\\'); tap(28500, '4'); shift_tap(28700, '2'); tap(28900, '0');
    tap(29300, KEY_ENTER);
    tap(31000, KEY_UP); tap(31100, KEY_UP); tap(31200, KEY_TAB); tap(31300, KEY_TAB); tap(31400, KEY_TAB); tap(31500, KEY_TAB); tap(31600, KEY_TAB);
    tap(31700, KEY_TAB);                                                       /* S1: the major scale, as a sieve */
    strum(32400, "zxcvbnm,./", 200, 180);                                      /* played as a scale */
    tap(35000, KEY_SPACE);
    tap(35400, KEY_F12);                                                       /* UPIC */
    stroke(36000, 80, 600, 460, 250, 900);                                     /* glissandi drawn */
    stroke(37100, 80, 600, 470, 500, 800);
    stroke(38000, 80, 600, 450, 700, 800);
    for (int i = 0; i < 6; i++) { pointer(39000 + i * 180, 560 + i * 60, 300 + (i % 3) * 60, 1); pointer(39060 + i * 180, 560 + i * 60, 300 + (i % 3) * 60, 0); }   /* clicks: notes */
    tap(40300, KEY_TAB);                                                       /* LINE */
    stroke(40500, 600, 680, 950, 420, 500); stroke(41200, 600, 420, 950, 680, 500);
    tap(42000, KEY_HOME); tap(42100, KEY_SPACE);                               /* played: 4 bars */
    tap(50300, KEY_SPACE); tap(50500, KEY_TAB); tap(50600, KEY_TAB);           /* SCRUB: the hand plays it */
    for (int i = 0; i <= 120; i++) { int x = 60 + (i < 60 ? i : 120 - i) * 12; pointer(51000 + i * 30, x, 400, i < 120 ? 1 : 0); }
}

/* the instruments from files (1280x800: a strip spans x 32..1000 at y 144; GRIDPADS' pads x 32..1000, y 80..592):
   GRIDPADS arpeggiating two held notes and a pad, SIEVHARP holding at random, STYLO played by a stylus, THEREMIN swept */
static void script_inst(void) {
    shift_tap(100, '='); shift_tap(160, '='); shift_tap(220, '=');
    tap(300, KEY_F1);                                                          /* GRIDPADS */
    key(700, 'z', true); key(1900, 'v', true); pointer(3000, 32 + 121 + 2 * 242, 80 + 64 + 1 * 128, 1);
    pointer(5400, 32 + 121 + 2 * 242, 80 + 64 + 1 * 128, 0); key(5500, 'z', false); key(5500, 'v', false);
    tap(6100, KEY_F1); tap(6200, KEY_F1);                                      /* past SHRUTI to SIEVHARP: held, at random */
    tap(6600, 'z'); tap(7000, 'b'); tap(7400, ','); tap(10200, KEY_SPACE);
    tap(10800, KEY_F1);                                                        /* STYLO: a stylus on the strip */
    static const int tune[] = { 0, 2, 4, 5, 7, 7, 9, 7, 5, 4, 2, 4, 0, -1 };
    for (int i = 0; tune[i] >= 0; i++) {
        int x = 32 + (tune[i] + 3) * 968 / 20 + 20;
        pointer(11300 + i * 380, x, 144, 1); pointer(11300 + i * 380 + 300, x, 144, 0);
    }
    tap(17000, KEY_F1);                                                        /* THEREMIN: swept by hand */
    for (int i = 0; i <= 300; i++) {
        int x = 100 + i * 2 + (i % 20 < 10 ? i % 10 : 10 - i % 10) * 3;
        pointer(17400 + i * 20, x, 144, i < 300 ? 1 : 0);
    }
    tap(24000, KEY_F1);                                                        /* back to the omnichord */
}

/* SHRUTI: Sa, Pa and the high Sa opened, the bellows pumped by a finger to and fro on the touchpad; Pa closed and Ma
   opened; left to breathe out; Enter held pumps it again */
static void script_shruti(void) {
    tap(300, KEY_F1); tap(500, KEY_F1);                                        /* the omnichord, GRIDPADS, SHRUTI */
    tap(1300, 'z'); tap(1700, 'b'); tap(2100, ',');
    for (int t = 2600, i = 0; t < 11000; t += 20, i++) {                       /* to and fro, 0.8 s a stroke */
        int ph = (t - 2600) % 1600, x = ph < 800 ? 6000 + ph * 20 : 6000 + (1600 - ph) * 20;
        pad_touch(t, x, 16000 + (ph < 800 ? 0 : 1500), 80);
    }
    pad_touch(11000, 0, 0, 0);
    tap(6500, 'b'); tap(6900, 'v');                                            /* Pa closes, Ma opens */
    note(15000, KEY_ENTER, 1800);
    tap(20500, KEY_SPACE);                                                     /* every reed closed */
}

/* TOUCH: fingers on the crackle box's pads, as `make render`'s touch.wav plays them */
static int ptx(int pad) { int x0, y0, x1, y1; touch_pad_rect(pad, &x0, &y0, &x1, &y1); return (x0 + x1) / 2; }
static int pty(int pad) { int x0, y0, x1, y1; touch_pad_rect(pad, &x0, &y0, &x1, &y1); return (y0 + y1) / 2; }
static int zp(int press) { return 25 + press * 100 / 255; }
static void lift_all(int t) { for (int f = 0; f < 5; f++) finger(t, f, 0, 0, 0); }
static void script_touch(void) {
    tap(300, KEY_F9);
    int t = 800;
    for (int ms = 0; ms < 3500; ms += 20) {                                   /* OUT and IN-, pressing harder */
        int pr = 40 + ms * 190 / 3500;
        finger(t + ms, 0, ptx(TP_OUT), pty(TP_OUT), zp(pr)); finger(t + ms, 1, ptx(TP_INV), pty(TP_INV), zp(pr));
    }
    t += 3500; lift_all(t); t += 300;
    for (int ms = 0; ms < 2600; ms += 20) finger(t + ms, 0, ptx(TP_OUT) + ms * (ptx(TP_INV) - ptx(TP_OUT)) / 2600, pty(TP_OUT), zp(170));
    t += 2600; lift_all(t); t += 300;
    finger(t, 0, ptx(TP_OUT), pty(TP_OUT), zp(140)); finger(t, 1, ptx(TP_INV), pty(TP_INV), zp(140)); t += 1200;
    finger(t, 2, ptx(TP_COMP), pty(TP_COMP), zp(140)); t += 1500;                           /* COMP joins */
    finger(t, 2, ptx(TP_GND), pty(TP_GND), zp(140)); t += 1500;                             /* then 0V instead */
    lift_all(t); t += 300;
    finger(t, 0, ptx(TP_OUT), pty(TP_OUT), zp(140)); finger(t, 1, ptx(TP_C1), pty(TP_C1), zp(140));
    finger(t, 2, ptx(TP_C2), pty(TP_C2), zp(140)); t += 2500;                               /* OUT, C1, C2: it wobbles */
    lift_all(t); t += 300;
    for (int ms = 0; ms < 5000; ms += 20) {                                   /* three fingers, their pressure swaying */
        int a = 120 + (ms / 7) % 160 - 80, b = 130 + (ms / 11) % 140 - 70, c = 60 + (ms / 5) % 100 - 50;
        finger(t + ms, 0, ptx(TP_OUT), pty(TP_OUT), zp(a < 40 ? 80 - a : a)); finger(t + ms, 1, ptx(TP_INV), pty(TP_INV), zp(b < 60 ? 120 - b : b));
        finger(t + ms, 2, ptx(TP_C2) - 1500, pty(TP_C2), zp(c < 10 ? 20 - c : c));
    }
    lift_all(t + 5000);
}

/* the ten colour schemes, a step every 1.6 s, over the rhythm section and strums on PLAY */
static void script_themes(void) {
    tap(300, 'r');
    for (int i = 0; i < 10; i++) {
        int t = 900 + i * 1600;
        chord(t, (uint8_t)"4637524637"[i], 1500); strum(t + 100, i % 2 ? "zxcvbnm,./" : "asdfghjkl;'", 90, 60);
        shift_tap(t + 1450, 'h');
    }
    tap(17200, 'r');
}

/* SEQ: the BARE METAL demo, its rows scrolling */
static void script_seq(void) { tap(300, KEY_F2); shift_tap(700, '1'); shift_tap(15500, '1'); }

/* FILE: a project saved by name to the stick, then its other views: songs, MIDI, the log */
static void script_file(void) {
    tap(300, KEY_F7); tap(1500, 's');
    for (int i = 0; "showcase"[i]; i++) tap(2000 + i * 140, (uint8_t)"showcase"[i]);
    tap(3400, KEY_ENTER);
    tap(5200, KEY_TAB); tap(7200, KEY_TAB); tap(9200, KEY_TAB);
}

/* a title card in BARE!'s own font, silent */
static void card(const char *title, const char *line) {
    int W_ = gfx_width(), H_ = gfx_height(), sc = 4;
    const struct font *f = &font_t12x24;
    gfx_noclip(); gfx_fill(0, 0, W_, H_, C_BG);
    int tw = gfx_text_width(title, f, sc);
    gfx_text((W_ - tw) / 2, H_ / 2 - f->height * sc, title, f, C_AMBER, -1, sc);
    int lw = gfx_text_width(line, f, 1);
    gfx_text((W_ - lw) / 2, H_ / 2 + f->height, line, f, C_TEXT, -1, 1);
    gfx_pointer(0, 0, PTR_HIDDEN);
    gfx_present();
}

/* Doom: iddqd typed on PLAY (the letters play strings), its title, a new game in E1M1, F1 back to BARE! for a strum,
   and iddqd again: it waits at its menu */
static void word(int t, const char *w) { for (int i = 0; w[i]; i++) tap(t + i * 170, (uint8_t)w[i]); }
static void script_doom(void) {
    chord(300, '4', 1800); strum(400, "asdfghjkl;'", 90, 60);
    word(2400, "iddqd");
    tap(9000, KEY_ESC); tap(9700, KEY_ENTER); tap(10300, KEY_ENTER); tap(10900, KEY_ENTER);
    int g = 13200;                                               /* E1M1 */
    note(g, KEY_UP, 1300);
    note(g + 1500, KEY_LCTRL, 900);
    note(g + 2600, KEY_RIGHT, 420); note(g + 3100, KEY_UP, 1600);
    note(g + 4800, KEY_LEFT, 700); note(g + 5600, KEY_LCTRL, 700); note(g + 5700, KEY_UP, 900);
    note(g + 6800, KEY_LEFT, 380); note(g + 7300, KEY_UP, 1800); note(g + 7500, KEY_LCTRL, 1200);
    note(g + 9400, KEY_RIGHT, 500); note(g + 10000, KEY_UP, 1400);
    int b = g + 11800;                                           /* back to BARE! */
    tap(b, KEY_F1);
    chord(b + 500, '5', 1900); strum(b + 600, "zxcvbnm,./", 80, 60); strum(b + 1500, "lkjhgfdsa", 70, 60);
    word(b + 2800, "iddqd");
    tap(b + 5200, KEY_ESC);
    note(b + 5700, KEY_UP, 1200); note(b + 6200, KEY_LCTRL, 800);
}

int main(int argc, char **argv) {
    int splash = -1, end_ms = END_MS;
    bool fxdemo = false, ansdemo = false, xendemo = false, instdemo = false, doomdemo = false, shrutidemo = false;
    bool touchdemo = false, themedemo = false, seqdemo = false, filedemo = false; const char *card_title = 0, *card_line = 0;
    if (argc > 3 && !strcmp(argv[1], "--splash")) { splash = atoi(argv[2]); argv += 2; argc -= 2; splash_mode = splash; end_ms = 9000; }
    if (argc > 2 && !strcmp(argv[1], "--fx")) { fxdemo = true; argv += 1; argc -= 1; end_ms = 23000; }
    if (argc > 2 && !strcmp(argv[1], "--ans")) { ansdemo = true; argv += 1; argc -= 1; end_ms = 26000; }
    if (argc > 2 && !strcmp(argv[1], "--xen")) { xendemo = true; argv += 1; argc -= 1; end_ms = 55500; }
    if (argc > 2 && !strcmp(argv[1], "--inst")) { instdemo = true; argv += 1; argc -= 1; end_ms = 25000; }
    if (argc > 2 && !strcmp(argv[1], "--shruti")) { shrutidemo = true; argv += 1; argc -= 1; end_ms = 22000; }
    if (argc > 2 && !strcmp(argv[1], "--touch")) { touchdemo = true; argv += 1; argc -= 1; end_ms = 20500; }
    if (argc > 2 && !strcmp(argv[1], "--themes")) { themedemo = true; argv += 1; argc -= 1; end_ms = 18000; }
    if (argc > 2 && !strcmp(argv[1], "--seq")) { seqdemo = true; argv += 1; argc -= 1; end_ms = 16000; }
    if (argc > 3 && !strcmp(argv[1], "--file")) {
        if (!host_disk_open(argv[2])) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
        filedemo = true; argv += 2; argc -= 2; end_ms = 12500;
    }
    if (argc > 5 && !strcmp(argv[1], "--card")) { card_title = argv[2]; card_line = argv[3]; end_ms = atoi(argv[4]) * 1000; argv += 4; argc -= 4; }
    if (argc > 3 && !strcmp(argv[1], "--doom")) {
        if (!host_disk_open(argv[2])) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
        doomdemo = true; argv += 2; argc -= 2; end_ms = 34000;
    }
    const char *out = argc > 1 ? argv[1] : "showcase.mp4";
    char wav[512], silent[512], cmd[2048];
    snprintf(wav, sizeof wav, "%s.wav", out); snprintf(silent, sizeof silent, "%s.video.mp4", out);
    static struct fb_info fb; fb = host_fb(W, H);
    app_init(&fb, 48000);
    if (card_title) card(card_title, card_line);
    else if (splash < 0) {
        host_run(500);
        if (fxdemo) script_fx(); else if (ansdemo) script_ans(); else if (xendemo) script_xen(); else if (instdemo) script_inst();
        else if (doomdemo) script_doom(); else if (shrutidemo) script_shruti(); else if (touchdemo) script_touch();
        else if (themedemo) script_themes(); else if (seqdemo) script_seq(); else if (filedemo) script_file(); else script();
    }
    qsort(evs, (size_t)nev, sizeof evs[0], cmp);
    /* nearest-neighbour 2x: every UI pixel keeps its own chroma sample in 4:2:0, so thin coloured lines stay crisp */
    snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgr0 -s %dx%d -r %d -i - "
             "-vf scale=%d:%d:flags=neighbor -c:v libx264 -preset slow -crf 20 -tune animation -pix_fmt yuv420p '%s'",
             W, H, FPS, W * 2, H * 2, silent);
    FILE *vid = popen(cmd, "w");
    if (!vid) { perror("ffmpeg"); return 1; }
    host_wav_open(wav);
    uint64_t t0 = host_now_ms; int frame = 0, e = 0;
    while ((int)(host_now_ms - t0) < end_ms) {
        int t = (int)(host_now_ms - t0);
        for (; e < nev && evs[e].t <= t; e++) {
            if (evs[e].type == KEYEV) host_key((uint8_t)evs[e].a, evs[e].b);
            else if (evs[e].type == TOUCHEV) host_finger(evs[e].f, evs[e].a, evs[e].b, evs[e].c, 0);
            else host_pointer(evs[e].a, evs[e].b, evs[e].c);
        }
        host_now_ms++;
        host_audio_ms();
        if (!card_title) app_step(host_now_ms);                    /* a card stays as drawn */
        while ((int64_t)frame * 1000 / FPS <= (int64_t)(host_now_ms - t0)) { fwrite(fb.addr, 4, (size_t)W * H, vid); frame++; }
    }
    host_wav_close();
    if (pclose(vid)) { fprintf(stderr, "ffmpeg failed\n"); return 1; }
    snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -i '%s' -i '%s' -c:v copy -c:a aac -b:a 256k -shortest -movflags +faststart '%s' && rm '%s' '%s'",
             silent, wav, out, silent, wav);
    if (system(cmd)) { fprintf(stderr, "muxing failed\n"); return 1; }
    printf("%s: %d frames, %.1f s\n", out, frame, frame / (double)FPS);
    return 0;
}
