/* A 70-second showcase, played by a script through the instrument on the host and encoded with ffmpeg — picture
   and sound come from the same simulated clock, so they line up exactly.  Usage: video OUT.mp4
   With --splash N (0..3) it records the boot's splash piece N instead, from the end of app_init, and a moment after;
   with --fx, the FX page played over a demo song; with --ans, the ANS page drawn on and played; with --lineage, the
   other LINEAGE views played; with --e1m1 IMAGE, SEQ's ⇧4 on the image's WAD; with --xen, the XENAKIS page; with --doom IMAGE, iddqd typed on PLAY and Doom played from the stick image (make doom-img); --shruti,
   --touch, --themes, --seq and --file IMAGE the SHRUTI, TOUCH, colour schemes, SEQ and FILE pages; --keys IMAGE the
   F keys set on FILE's KEYS view, played, and a tab dragged; --card TITLE LINE S a title card of S seconds in BARE!'s
   font (tools/showcase.py puts them all together). */
#include "host.h"
#include "app.h"
#include "keys.h"
#include "splash.h"
#include "gfx.h"
#include "text.h"
#include "touch.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>

#define W 1280
#define H 800
#define FPS 30

/* ---- a script: key and pointer events at times in ms, sorted before the run ---- */
enum { KEYEV, PTREV, TOUCHEV, TABEV };
struct ev { int t, type, a, b, c, n, f; };                    /* n: insertion order, so the sort is stable; f: a finger */
static struct ev evs[8192]; static int nev;
static void key(int t, uint8_t code, bool down) { evs[nev] = (struct ev){ t, KEYEV, code, down, 0, nev, 0 }; nev++; }
static void note(int t, uint8_t code, int dur) { key(t, code, true); key(t + dur, code, false); }
static void tap(int t, uint8_t code) { note(t, code, 40); }
static void pointer(int t, int x, int y, int buttons) { evs[nev] = (struct ev){ t, PTREV, x * 32768 / W, y * 32768 / H, buttons, nev, 0 }; nev++; }
static void finger(int t, int f, int tx, int ty, int z) { evs[nev] = (struct ev){ t, TOUCHEV, tx, ty, z, nev, f }; nev++; }
static void pad_touch(int t, int tx, int ty, int z) { finger(t, 0, tx, ty, z); }   /* finger 0 on the pad */
/* the pointer on the title bar, pct of the way from key a's tab to key b's (where they are when it happens) */
static void at_tabs(int t, int a, int b, int pct, int buttons) { evs[nev] = (struct ev){ t, TABEV, a, b, buttons, nev, pct }; nev++; }
static void strum(int t, const char *keys, int step, int dur) { for (int i = 0; keys[i]; i++) note(t + i * step, (uint8_t)keys[i], dur); }
static void chord(int t, uint8_t root, int dur) { note(t, root, dur); }
static void shift_tap(int t, uint8_t code) { key(t, KEY_LSHIFT, true); tap(t, code); key(t + 40, KEY_LSHIFT, false); }
static int cmp(const void *a, const void *b) { const struct ev *x = a, *y = b; return x->t != y->t ? x->t - y->t : x->n - y->n; }

static void script(void) {
    /* PLAY: chords and strums (the OM-108's buttons: 6 C, 7 G, 5 F on the MAJOR row, O A on the MINOR one; the strings
       on Z … /), then a strum with the pointer across the plate */
    chord(400, '6', 2200);                       strum(600, "zxcvbnm,./", 110, 60);    strum(1900, "/.,mnbvcxz", 80, 60);
    chord(2750, 'o', 2000);                      strum(2900, "zxcvbnm,.", 100, 60);    strum(3950, ".,mnb", 90, 60);
    chord(4900, '5', 1900);                      strum(5100, "zxcvbnm", 80, 60);       strum(5700, "zxcvbn", 90, 60);
    chord(6900, '7', 1500);                      strum(7000, "zxcvbnm,./", 70, 60);
    chord(8500, '6', 1400);
    for (int i = 0; i <= 44; i++) pointer(8600 + i * 16, 40 + i * 25, 400, i < 44 ? 1 : 0);
    /* SEQ: the HARBOR demo starts; back on PLAY, strum along with its chords (C, Am, F, G — a bar is 2.5 s at 96 BPM) */
    pointer(9600, 900, 600, 0);
    tap(9800, KEY_F2); shift_tap(10300, '2');                   /* ⇧2: the HARBOR demo */
    int bar0 = 10300;
    tap(bar0 + 2 * 2500 - 60, KEY_F1);
    chord(bar0 + 2 * 2500, '5', 2440); strum(bar0 + 2 * 2500, "zxcvbvcx", 312, 120);
    chord(bar0 + 3 * 2500, '7', 2440); strum(bar0 + 3 * 2500 + 312, "zxcvbnm,./", 156, 80);
    chord(bar0 + 4 * 2500, '6', 2440); strum(bar0 + 4 * 2500, "zxcvbnm,", 312, 120);
    chord(bar0 + 5 * 2500, 'o', 2400); strum(bar0 + 5 * 2500, "zxcvbnbvcxz", 208, 80);
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
    strum(tf + 300, "zcbm", 250, 220);
    tap(tf + 1500, KEY_PGDN); tap(tf + 2000, KEY_PGDN);
    for (int i = 0; i <= 20; i++) pointer(tf + 2400 + i * 30, 812 + i * 4, 136, i < 20 ? 1 : 0);
    strum(tf + 3200, "zcbm.", 200, 200);
    for (int i = 0; i < 6; i++) tap(tf + 4500 + i * 120, '=');
    strum(tf + 5400, "zcbm", 220, 250);
    /* STRETCH: stop the sequencer, freeze what was played, slow it further, play on top */
    int ts = tf + 7000;                                          /* 42300 */
    tap(ts, KEY_F4); tap(ts + 300, KEY_ESC); tap(ts + 500, KEY_SPACE);
    tap(ts + 3000, KEY_RIGHT); tap(ts + 3300, KEY_UP);
    chord(ts + 4200, '5', 1800); strum(ts + 4400, "zxcvb", 260, 200);
    /* TAPE: arm, record strums over the frozen pad, stop, rewind, play back with wow */
    int tt = ts + 6500;                                          /* 48800 */
    tap(tt, KEY_F6); tap(tt + 300, 'q'); tap(tt + 700, 'r');
    chord(tt + 900, '6', 1900); strum(tt + 1000, "zxcvbnm,.", 180, 100);
    chord(tt + 2900, '7', 1900); strum(tt + 3000, ".,mnbvcxz", 180, 100);
    tap(tt + 5000, 'r'); tap(tt + 5100, KEY_SPACE); shift_tap(tt + 5300, 'f');
    tap(tt + 5800, KEY_HOME); tap(tt + 6300, 'u');
    tap(tt + 6600, KEY_SPACE);
    /* MIX: the tape plays on through the mixer; the master filter swept down and up under strums */
    int tm = tt + 11500;                                         /* 60300 */
    tap(tm, KEY_F8);
    for (int i = 0; i < 14; i++) tap(tm + 300 + i * 50, KEY_RIGHT);                    /* past eleven strips and MASTER to FILTER */
    tap(tm + 1000, KEY_ENTER); tap(tm + 1100, KEY_DOWN);                                  /* on; its cutoff */
    chord(tm + 1200, '6', 1500); strum(tm + 1300, "zxcvbnm,./", 110, 80);
    for (int i = 0; i < 12; i++) tap(tm + 1400 + i * 120, '[');
    chord(tm + 2800, '7', 1500); strum(tm + 2900, "/.,mnbvcxz", 120, 80);
    for (int i = 0; i < 14; i++) tap(tm + 2950 + i * 110, ']');
    tap(tm + 4700, KEY_ENTER);                                                            /* filter off */
    /* PLAY, to finish */
    int te = tm + 5000;                                          /* 65300 */
    tap(te, KEY_F1);
    chord(te + 200, '6', 2600); strum(te + 300, "zxcvbnm,./", 60, 60); strum(te + 1000, "/.,mnbvcxz", 60, 60);
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
   camera's picture laid over it; then the live camera as the plate (1280x800: the plate spans x 48..1008, y 48..608,
   the history under it) */
static int plate_x(int col) { return 48 + col * 960 / 512; }
static int plate_y(int row) { return 608 - row * 560 / 360; }
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

/* the XENAKIS page, its views in the order of their music (1280x800: CLOUDS', SIEVES' and GENDY's pictures span y
   48..736, METASTASEIS' and UPIC's y 48..608 above their history; x 48..1008, GENDY's 24..1008): METASTASEIS's opening
   played, crossed and in the Modulor's proportions; clouds switched on, one steered, one written onto UPIC's page; the
   sieves' rhythm, examples, a formula typed, a sieve played as a scale; UPIC drawn on, played, scrubbed; GENDY's
   patches from the letter rows and a drone stirred by the mouse */
static void stroke(int t, int x0, int y0, int x1, int y1, int ms) {
    int steps = ms / 16;
    for (int i = 0; i <= steps; i++) pointer(t + i * 16, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps + (i % 7 == 3 ? 3 : 0), i < steps ? 1 : 0);
}
static void script_xen(void) {
    shift_tap(100, '='); shift_tap(160, '='); shift_tap(220, '=');           /* the master at 0 dB */
    tap(300, KEY_F12);                                                         /* METASTASEIS: the opening's fan */
    tap(800, KEY_SPACE);
    tap(3200, 'c');                                                            /* crossed: a curve appears */
    tap(5600, 'm');                                                            /* in the Modulor's proportions */
    tap(8200, KEY_SPACE);
    int c = 8600 - 12900;                                                      /* CLOUDS, as it was at 12900 */
    tap(c + 12900, KEY_F12);
    tap(c + 13300, '1'); tap(c + 14600, '2');
    for (int i = 0; i <= 120; i++) pointer(c + 16000 + i * 25, 300 + i * 4, 600 - (i % 60) * 7, i < 120 ? 1 : 0);   /* A steered by hand */
    tap(c + 19300, '4');
    tap(c + 20800, KEY_TAB); tap(c + 21000, KEY_ENTER);                        /* B written onto UPIC's page */
    tap(c + 22800, '0');
    tap(c + 23300, KEY_F12);                                                   /* SIEVES */
    tap(c + 23700, KEY_SPACE);                                                 /* the SIEVE rhythm */
    tap(c + 25200, KEY_TAB);                                                   /* S1: an example */
    tap(c + 26400, KEY_DOWN); tap(c + 26500, KEY_DOWN); tap(c + 26700, KEY_ENTER);   /* S3: 3@0|4@0 typed */
    for (int i = 0; i < 12; i++) tap(c + 26900 + i * 60, KEY_BACKSPACE);
    tap(c + 27700, '3'); shift_tap(c + 27900, '2'); tap(c + 28100, '0'); shift_tap(c + 28300, '\\'); tap(c + 28500, '4'); shift_tap(c + 28700, '2'); tap(c + 28900, '0');
    tap(c + 29300, KEY_ENTER);
    tap(c + 31000, KEY_UP); tap(c + 31100, KEY_UP); tap(c + 31200, KEY_TAB); tap(c + 31300, KEY_TAB); tap(c + 31400, KEY_TAB); tap(c + 31500, KEY_TAB); tap(c + 31600, KEY_TAB);
    tap(c + 31700, KEY_TAB);                                                   /* S1: the major scale, as a sieve */
    strum(c + 32400, "zxcvbnm,./", 200, 180);                                  /* played as a scale */
    tap(c + 35000, KEY_SPACE);
    tap(c + 35400, KEY_F12);                                                   /* UPIC (its page above the history) */
    stroke(c + 36000, 80, 497, 460, 212, 900);                                 /* glissandi drawn */
    stroke(c + 37100, 80, 497, 470, 414, 800);
    stroke(c + 38000, 80, 497, 450, 578, 800);
    for (int i = 0; i < 6; i++) { pointer(c + 39000 + i * 180, 560 + i * 60, 253 + (i % 3) * 49, 1); pointer(c + 39060 + i * 180, 560 + i * 60, 253 + (i % 3) * 49, 0); }   /* clicks: notes */
    tap(c + 40300, KEY_TAB);                                                   /* LINE */
    stroke(c + 40500, 600, 562, 950, 350, 500); stroke(c + 41200, 600, 350, 950, 562, 500);
    tap(c + 42000, KEY_HOME); tap(c + 42100, KEY_SPACE);                       /* played: 4 bars */
    tap(c + 50300, KEY_SPACE); tap(c + 50500, KEY_TAB); tap(c + 50600, KEY_TAB);   /* SCRUB: the hand plays it */
    for (int i = 0; i <= 120; i++) { int x = 60 + (i < 60 ? i : 120 - i) * 12; pointer(c + 51000 + i * 30, x, 334, i < 120 ? 1 : 0); }
    tap(c + 54700, KEY_TAB);                                                   /* back to the PEN */
    int g = 51000 - 300;                                                       /* GENDY, as it was at 300 */
    tap(g + 300, KEY_F12);
    note(g + 700, 'z', 1700); note(g + 1100, 'b', 1300);                       /* GENDY3: C3, G3 */
    tap(g + 2500, KEY_END); note(g + 2700, 'x', 1500); note(g + 3000, 'n', 1200);   /* S.709: its pitch walks */
    tap(g + 4400, KEY_END); note(g + 4600, 'z', 1800); note(g + 4800, 'c', 1600); note(g + 5000, 'b', 1400);   /* BREATH */
    tap(g + 6600, KEY_END); note(g + 6800, 'v', 1400); note(g + 7200, 'q', 1000);   /* STORM */
    tap(g + 8400, KEY_END); tap(g + 8600, KEY_SPACE);                          /* GENDY3 as a drone, stirred */
    for (int i = 0; i <= 150; i++) { int a = i <= 75 ? i : 150 - i; pointer(g + 9000 + i * 22, 84 + a * 820 / 75, 676 - a * 560 / 75, i < 150 ? 1 : 0); }
    tap(g + 12500, KEY_SPACE);
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

/* LINEAGE after ANS: REICH's three players phasing (a new pattern from the chord half way), CARLOS's alpha scale — a
   chord nearly pure, a run, the omnichord's chord compared, a gliding line —, RADIGUE's drone fading in, gliding to G
   and one partial detuned by a finger so it beats faster, MERZBOW's junk struck, scraped, fed back and its own bytes */
static void script_lineage(void) {
    shift_tap(100, '='); shift_tap(160, '='); shift_tap(220, '=');           /* the master at 0 dB */
    tap(300, KEY_F11); tap(700, KEY_F11);                                      /* ANS, then REICH */
    tap(1000, KEY_DOWN); tap(1100, KEY_RIGHT);                                 /* three players */
    for (int i = 0; i < 4; i++) tap(1200 + i * 60, KEY_DOWN);                  /* HOLD: a repeat */
    for (int i = 0; i < 7; i++) tap(1500 + i * 60, KEY_LEFT);
    tap(2000, KEY_SPACE);
    tap(8000, KEY_ENTER);                                                      /* a pattern from the omnichord's chord */
    tap(13500, KEY_SPACE);
    tap(14500, KEY_F11);                                                       /* CARLOS: alpha */
    note(15000, 'z', 1800); note(15050, 'n', 1750); note(15100, '/', 1700);    /* steps 0, 5, 9: 390 and 702 cents */
    strum(17200, "zxcvbnm,./", 160, 150);                                      /* the scale, a step a key */
    tap(19000, KEY_ENTER);                                                     /* the chord in alpha, then equal */
    tap(22600, KEY_SPACE);                                                     /* one voice, gliding */
    tap(22700, KEY_DOWN); tap(22760, KEY_DOWN); tap(22820, KEY_DOWN);
    for (int i = 0; i < 6; i++) tap(22900 + i * 50, KEY_RIGHT);               /* GLIDE: 250 ms */
    note(23400, 'z', 500); note(23900, 'n', 500); note(24400, '/', 800); note(25200, 'b', 600); note(25800, 'x', 1200);
    tap(27500, KEY_SPACE);
    tap(28000, KEY_F11);                                                       /* RADIGUE */
    tap(28200, KEY_DOWN); tap(28300, KEY_DOWN);                                /* FADE: 2 s */
    tap(28400, KEY_LEFT); tap(28500, KEY_LEFT); tap(28600, KEY_LEFT);
    tap(28700, KEY_UP);                                                        /* SWEEP: 10 s */
    tap(28800, KEY_LEFT); tap(28900, KEY_LEFT); tap(29000, KEY_LEFT);
    tap(29200, KEY_SPACE);
    tap(33000, 'b');                                                           /* the base glides to G2 */
    tap(35500, '2');                                                           /* partial 2, detuned by a finger */
    for (int t = 36000; t < 39600; t += 20) pad_touch(t, 17800 + (t - 36000) * 7 / 4, 9800, 80);
    pad_touch(39600, 0, 0, 0);
    tap(42000, KEY_SPACE);
    tap(44200, KEY_F11);                                                       /* MERZBOW */
    static const char hits[] = "qg.xp;[bk";
    for (int i = 0; hits[i]; i++) tap(44600 + i * 330, (uint8_t)hits[i]);
    for (int t = 47800; t < 51400; t += 20) {                                  /* a finger scraping, in strokes */
        int ph = (t - 47800) % 900;
        if (ph < 760) finger(t, 0, 3000 + ph * 34, 6000 + ((t - 47800) / 900) * 5000 + (ph % 120) * 20, 70 + ph / 20);
        else finger(t, 0, 0, 0, 0);
    }
    finger(51400, 0, 0, 0, 0);
    note(51800, KEY_SPACE, 2000);                                              /* fed back, held */
    tap(54200, KEY_ENTER); tap(56700, KEY_ENTER);                              /* its own bytes */
    tap(58000, KEY_BACKSPACE);
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
    tap(300, KEY_SPACE);
    for (int i = 0; i < 10; i++) {
        int t = 900 + i * 1600;
        chord(t, (uint8_t)"6859746859"[i], 1500); strum(t + 100, i % 2 ? "/.,mnbvcxz" : "zxcvbnm,./", 90, 60);
        shift_tap(t + 1450, 'h');
    }
    tap(17200, KEY_SPACE);
}

/* SEQ: the BARE METAL demo, its rows scrolling */
static void script_seq(void) { tap(300, KEY_F2); shift_tap(700, '1'); shift_tap(15500, '1'); }

/* SEQ's ⇧4: the E1M1 in the WAD on the stick (Freedoom's, on the doom image) arranged as breakcore, played */
static void script_e1m1(void) { tap(300, KEY_F2); shift_tap(900, '4'); tap(33000, KEY_SPACE); }

/* FILE: a project saved by name to the stick, then its other views: songs, MIDI, the keys, the log */
static void script_file(void) {
    tap(300, KEY_F7); tap(1500, 's');
    for (int i = 0; "showcase"[i]; i++) tap(2000 + i * 140, (uint8_t)"showcase"[i]);
    tap(3400, KEY_ENTER);
    tap(5200, KEY_TAB); tap(7200, KEY_TAB); tap(9200, KEY_TAB); tap(11700, KEY_TAB);
}

/* KEYS: on FILE's KEYS view F2 becomes SHRUTI and F3 UPIC; both played; then TOUCH's tab dragged onto F4 */
static void script_keys(void) {
    tap(300, KEY_F7); tap(1000, KEY_TAB); tap(1300, KEY_TAB); tap(1600, KEY_TAB);
    tap(2400, KEY_DOWN);                                                       /* F2: SEQ */
    for (int i = 0; i < 6; i++) tap(2900 + i * 380, KEY_LEFT);                 /* PLAY, off, the instruments: SHRUTI */
    tap(5600, KEY_DOWN);                                                       /* F3: WAVE */
    for (int i = 0; i < 13; i++) tap(6100 + i * 180, KEY_RIGHT);               /* the pages, then METASTASEIS … UPIC */
    tap(9300, KEY_F2);                                                         /* SHRUTI: three reeds and the bellows */
    tap(9900, 'z'); tap(10200, 'b'); tap(10500, ',');
    for (int t = 10800; t < 14800; t += 20) { int ph = (t - 10800) % 1600, x = ph < 800 ? 6000 + ph * 20 : 6000 + (1600 - ph) * 20; pad_touch(t, x, 16000, 80); }
    pad_touch(14800, 0, 0, 0); tap(15000, KEY_SPACE);
    tap(15600, KEY_F3); tap(16000, KEY_SPACE);                                 /* UPIC: its page plays */
    tap(18300, KEY_SPACE); tap(18600, KEY_F1);                                 /* PLAY: the omnichord, where F1 left it */
    chord(19000, '6', 1600); strum(19200, "zxcvbnm,./", 90, 60);
    at_tabs(21000, 8, 8, 0, 0); at_tabs(21400, 8, 8, 0, 1);                   /* TOUCH's tab, pressed, carried to F4's */
    for (int i = 1; i <= 20; i++) at_tabs(21400 + i * 60, 8, 3, i * 5, 1);
    at_tabs(23000, 8, 3, 100, 0);                                              /* let go: F4 TOUCH, F9 STRETCH */
    tap(24000, KEY_F4);
    tap(25500, KEY_F7);                                                        /* the KEYS view shows it */
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

/* Doom: iddqd typed on PLAY (the letters press chord buttons), its title, a new game in E1M1, F1 back to BARE! for a strum,
   and iddqd again: it waits at its menu */
static void word(int t, const char *w) { for (int i = 0; w[i]; i++) tap(t + i * 170, (uint8_t)w[i]); }
static void script_doom(void) {
    chord(300, '6', 1800); strum(400, "zxcvbnm,./", 90, 60);
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
    chord(b + 500, '7', 1900); strum(b + 600, "zxcvbnm,./", 80, 60); strum(b + 1500, "/.,mnbvcx", 70, 60);
    word(b + 2800, "iddqd");
    tap(b + 5200, KEY_ESC);
    note(b + 5700, KEY_UP, 1200); note(b + 6200, KEY_LCTRL, 800);
}

int main(int argc, char **argv) {
    int splash = -1, end_ms = END_MS;
    bool fxdemo = false, ansdemo = false, xendemo = false, instdemo = false, doomdemo = false, shrutidemo = false;
    bool touchdemo = false, themedemo = false, seqdemo = false, filedemo = false, keysdemo = false, lineagedemo = false, e1m1demo = false; const char *card_title = 0, *card_line = 0;
    if (argc > 3 && !strcmp(argv[1], "--splash")) { splash = atoi(argv[2]); argv += 2; argc -= 2; splash_mode = splash; end_ms = 9000; }
    if (argc > 2 && !strcmp(argv[1], "--fx")) { fxdemo = true; argv += 1; argc -= 1; end_ms = 23000; }
    if (argc > 2 && !strcmp(argv[1], "--ans")) { ansdemo = true; argv += 1; argc -= 1; end_ms = 26000; }
    if (argc > 2 && !strcmp(argv[1], "--xen")) { xendemo = true; argv += 1; argc -= 1; end_ms = 64000; }
    if (argc > 2 && !strcmp(argv[1], "--inst")) { instdemo = true; argv += 1; argc -= 1; end_ms = 25000; }
    if (argc > 2 && !strcmp(argv[1], "--lineage")) { lineagedemo = true; argv += 1; argc -= 1; end_ms = 60000; }
    if (argc > 2 && !strcmp(argv[1], "--shruti")) { shrutidemo = true; argv += 1; argc -= 1; end_ms = 22000; }
    if (argc > 2 && !strcmp(argv[1], "--touch")) { touchdemo = true; argv += 1; argc -= 1; end_ms = 20500; }
    if (argc > 2 && !strcmp(argv[1], "--themes")) { themedemo = true; argv += 1; argc -= 1; end_ms = 18000; }
    if (argc > 2 && !strcmp(argv[1], "--seq")) { seqdemo = true; argv += 1; argc -= 1; end_ms = 16000; }
    if (argc > 3 && !strcmp(argv[1], "--file")) {
        if (!host_disk_open(argv[2])) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
        filedemo = true; argv += 2; argc -= 2; end_ms = 14500;
    }
    if (argc > 3 && !strcmp(argv[1], "--e1m1")) {
        if (!host_disk_open(argv[2])) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
        e1m1demo = true; argv += 2; argc -= 2; end_ms = 34000;
    }
    if (argc > 3 && !strcmp(argv[1], "--keys")) {
        if (!host_disk_open(argv[2])) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
        keysdemo = true; argv += 2; argc -= 2; end_ms = 28000;
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
        else if (themedemo) script_themes(); else if (seqdemo) script_seq(); else if (filedemo) script_file(); else if (keysdemo) script_keys();
        else if (lineagedemo) script_lineage(); else if (e1m1demo) script_e1m1(); else script();
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
            else if (evs[e].type == TABEV) {
                int xa, wa, xb, wb; ui_tab(evs[e].a, &xa, &wa); ui_tab(evs[e].b, &xb, &wb);
                int ca = text_px(xa) + text_px(wa) / 2, cb = text_px(xb) + text_px(wb) / 2, px = ca + (cb - ca) * evs[e].f / 100;
                host_pointer(px * 32768 / W, text_font()->height / 2 * 32768 / H, evs[e].c);
            }
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
