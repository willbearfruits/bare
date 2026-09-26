#include "audio.h"
#include "synth.h"
#include "seq.h"
#include "rhythm.h"
#include "stretch.h"
#include "tape.h"
#include "sampler.h"
#include "mix.h"
#include "fx.h"
#include "touch.h"
#include "link.h"
#include "splash.h"
#include "perf.h"
#include "ans.h"
#include "cloud.h"
#include "upic.h"
#include "inst.h"
#include "doomhost.h"
#include "platform.h"
#include "midi.h"
#include "libc.h"
#include "env.h"

static uint32_t rate = 48000;
static uint64_t frames_done;

/* ---- stereo ping-pong echo: a dotted eighth at the sequencer tempo, repeats darken like tape ---- */
#define ECHO_LEN 65536                          /* power of two: 1.36 s at 48 kHz, a dotted eighth down to 40 BPM */
#define XFADE    2048                           /* a tempo change crossfades to the new delay instead of jumping */
static int16_t echo_l[ECHO_LEN], echo_r[ECHO_LEN];
static uint32_t echo_pos, delay_cur, delay_new, xf;
static int32_t damp_l, damp_r;
static bool echo_on = true;

static uint32_t echo_target(void) {
    uint32_t bpm = seq.bpm ? seq.bpm : 120;
    uint32_t d = rate * 15 * fx_echo_div_q4[fx.echo_div % FX_ECHO_DIVS] / bpm;   /* sixteenths of a beat: 60 s / bpm / 4 each */
    return d < 64 ? 64 : d > ECHO_LEN - 1 ? ECHO_LEN - 1 : d;
}

static uint32_t echo_silent;                   /* frames the lines have held nothing but zeros */

static void echo(const int32_t *send, int32_t *L, int32_t *R, uint32_t n) {
    if (!echo_on && echo_silent > ECHO_LEN) return;             /* off, and the repeats have died away */
    if (xf == 0 && delay_new != delay_cur) xf = 1;              /* start crossfading to the new time */
    int32_t loudest = 0, fb = fx.echo_feedback * 324;           /* the repeats: Q15, 37 = the old fixed 0.37 */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = (echo_pos - delay_cur) & (ECHO_LEN - 1);
        int32_t dl = echo_l[a], dr = echo_r[a];
        if (xf) {
            uint32_t b = (echo_pos - delay_new) & (ECHO_LEN - 1);
            dl += ((echo_l[b] - dl) * (int32_t)xf) >> 11;
            dr += ((echo_r[b] - dr) * (int32_t)xf) >> 11;
            if (++xf > XFADE) { xf = 0; delay_cur = delay_new; }
        }
        /* the dry mix goes into the left line; each line feeds the other, through a gentle low-pass */
        damp_l += ((dr - damp_l) * 15700) >> 15;
        damp_r += ((dl - damp_r) * 15700) >> 15;
        int32_t in = echo_on ? send[i] : 0;
        int32_t wl = in + ((damp_l * fb) >> 15), wr = (damp_r * fb) >> 15;
        wl = CLAMP(wl, -32767, 32767); wr = CLAMP(wr, -32767, 32767);
        echo_l[echo_pos] = (int16_t)wl; echo_r[echo_pos] = (int16_t)wr;
        loudest |= wl | wr;
        echo_pos = (echo_pos + 1) & (ECHO_LEN - 1);
        L[i] += (dl * 7000) >> 15;
        R[i] += (dr * 7000) >> 15;
    }
    echo_silent = loudest ? 0 : echo_silent + n;
}

void audio_set_echo(bool on) { echo_on = on; }
bool audio_echo(void) { return echo_on; }
uint32_t audio_echo_ms(void) { return delay_cur * 1000 / rate; }

/* ---- master: volume, DC removal, a peak limiter, and a soft knee for what gets past it ---- */
/* Volume in 2 dB steps, 0 to -40 dB, then mute. It comes before the limiter, so turning down also means less limiting;
   the tape and the stretcher record before it. -6 dB at boot: laptop speakers driven at the codec's full level
   distort on full-scale peaks. */
static const int16_t vol_q15[21] = { 32767, 26029, 20675, 16423, 13045, 10362, 8231, 6538, 5193, 4125, 3277, 2603, 2068,
                                     1642, 1305, 1036, 823, 654, 519, 413, 328 };
static int vol_step = 3;
static bool muted;
void audio_volume_step(int d) { vol_step = CLAMP(vol_step - d, 0, 20); muted = false; }

/* 1-BIT: what the PC speaker plays (synth_mono_freq: the newest strum note, else the held chord arpeggiated), as a square
   wave through the sound chip — laptops route their beeper into the sound chip, so this is the version they can play */
static bool onebit; static uint32_t onebit_phase;
void audio_set_onebit(bool on) { onebit = on; }
void audio_toggle_mute(void) { muted = !muted; }
void audio_set_mute(bool on) { muted = on; }
int  audio_volume_index(void) { return vol_step; }
void audio_set_volume_index(int i) { vol_step = CLAMP(i, 0, 20); }
int  audio_volume_db(void) { return -2 * vol_step; }
bool audio_muted(void) { return muted; }

static int32_t dc_l, dc_r;                      /* Q8 running mean, ~1 Hz */
static int32_t lim_gain = 32767;                /* Q15 */
/* look-ahead: the output runs LOOK frames late, so the limiter sees a peak before it plays it and its gain is already
   down when the peak goes out. The ring holds the LOOK frames in flight plus a block. */
#define LOOK 32
static int32_t la_l[64], la_r[64];
static uint32_t la_w;
static int32_t peak_l, peak_r;
#define LIM_THRESH 23198                        /* -3 dBFS: below the knee, so steady limiting stays linear */
#define KNEE       26029                        /* -2 dBFS: the knee only rounds what overshoots within a block */
#define KNEE_SPAN  (32767 - KNEE)
static int16_t knee_tab[1536];                  /* KNEE + KNEE_SPAN * tanh(d / KNEE_SPAN), d in steps of 16 */

static void build_knee(void) {
    for (int i = 0; i < 1536; i++) {
        /* tanh(u) ≈ u (27 + u²) / (27 + 9 u²), exact enough up to u = 3 where it reaches 1; u in Q12 */
        int64_t u = (int64_t)i * 16 * 4096 / KNEE_SPAN, u2 = u * u >> 12;
        int64_t t = u * (27 * 4096 + u2) / (27 * 4096 + 9 * u2);
        if (t > 4096) t = 4096;
        knee_tab[i] = (int16_t)(KNEE + KNEE_SPAN * t / 4096);
    }
}
/* unity up to KNEE (-2 dBFS), then a tanh curve that approaches full scale */
static inline int16_t knee(int32_t x) {
    int32_t a = x < 0 ? -x : x;
    if (a > KNEE) { int32_t d = (a - KNEE) >> 4; a = d < 1536 ? knee_tab[d] : 32767; }
    return (int16_t)(x < 0 ? -a : a);
}

/* ---- scope ---- */
static int16_t scope_l[AUDIO_SCOPE_LEN], scope_r[AUDIO_SCOPE_LEN];
static uint32_t scope_head;
void audio_scope_lr(const int16_t **l, const int16_t **r, uint32_t *head) { *l = scope_l; *r = scope_r; *head = scope_head; }
int audio_peak(int ch) { return ch ? peak_r : peak_l; }

int audio_limiter_q15(void) { return lim_gain; }

void audio_init(uint32_t r) {
    rate = r ? r : 48000;
    mix_init();
    touch_init();
    perf_init(rate);
    ans_init(rate);
    cloud_init(rate);
    upic_init(rate);
    fx_init(rate);
    synth_init(rate);
    rhythm_init(rate);
    memset(echo_l, 0, sizeof echo_l); memset(echo_r, 0, sizeof echo_r);
    echo_pos = 0; xf = 0; delay_cur = delay_new = rate * 3 / 10;
    build_knee();
    frames_done = 0; scope_head = 0;
}
uint32_t audio_rate(void) { return rate; }
uint64_t audio_frames(void) { return frames_done; }

/* ---- the input: frames from the driver wait in a FIFO; the render takes them once a few ms have gathered ---- */
#define IN_FIFO 4096                            /* frames, power of two */
#define IN_PRIME 256
static int16_t in_fifo[IN_FIFO * 2];
static volatile uint32_t in_w, in_r;
static bool in_primed;
void audio_input_push(const int16_t *lr, uint32_t n) {
    for (uint32_t i = 0; i < n && in_w - in_r < IN_FIFO; i++, in_w++) {
        in_fifo[(in_w & (IN_FIFO - 1)) * 2] = lr[2 * i]; in_fifo[(in_w & (IN_FIFO - 1)) * 2 + 1] = lr[2 * i + 1];
    }
}
uint32_t audio_input_latency(void) { return IN_PRIME; }
static bool input_pull(int32_t *l, int32_t *r, uint32_t n) {
    if (mix.input < 0) { in_r = in_w; in_primed = false; return false; }
    uint32_t have = in_w - in_r;
    if (!in_primed) { if (have < IN_PRIME) return false; in_primed = true; }
    if (have > IN_PRIME * 4) in_r = in_w - IN_PRIME;          /* fallen behind: drop the oldest, keep the delay short */
    else if (have < n) { mix.in_xruns++; in_primed = false; return false; }
    for (uint32_t i = 0; i < n; i++, in_r++) {
        const int16_t *f = &in_fifo[(in_r & (IN_FIFO - 1)) * 2];
        if (mix.in_mono) l[i] = r[i] = (f[0] + f[1]) >> 1; else { l[i] = f[0]; r[i] = f[1]; }
    }
    return true;
}

/* One channel of the mixer: its fader and pan (ramped from the last block's gains), into the master if it is heard,
   into the capture bus (what the stretcher and the tape take) unless it is muted, and into the echo send; its meter.
   The input, unheard, still reaches the sampler (SMP_IN) and a tape track set to record the input. */
static inline int32_t clamp17(int32_t x) { return x > 131071 ? 131071 : x < -131071 ? -131071 : x; }
static int32_t rsend[SYNTH_BLOCK];            /* the reverb's send bus, like the echo's */
static void mix_block(int c, int32_t *l, int32_t *r, uint32_t n, int32_t *ml, int32_t *mr, int32_t *cl, int32_t *cr, int32_t *send, bool post) {
    struct mix_channel *ch = &mix.ch[c];
    int32_t g = mix_gain_q12(ch->db);
    int32_t tl = ch->pan > 0 ? g * (100 - ch->pan) / 100 : g, tr = ch->pan < 0 ? g * (100 + ch->pan) / 100 : g;
    int32_t gl = ch->gl, gr = ch->gr, dl = ramp_step(gl, tl, (int)n), dr = ramp_step(gr, tr, (int)n);
    bool heard = mix_heard(c), cap = cl && !ch->mute;
    int te = perf_throw_mask >> c & 1 ? perf_echo_throw : 0, tv = perf_throw_mask >> c & 1 ? perf_rev_throw : 0;   /* DUB, FREEZE */
    int32_t es = heard ? MIN(100, ch->echo + te) * 256 / 100 : 0, rs = heard ? MIN(100, ch->reverb + tv) * 256 / 100 : 0, pl = 0, pr = 0;
    /* the usual case (heard, captured, full echo send) gets a loop without the branches */
#define CH_LOOP(BODY) for (uint32_t i = 0; i < n; i++, gl += dl, gr += dr) {                           \
        int32_t x = (clamp17(l[i]) * gl) >> 12, y = (clamp17(r[i]) * gr) >> 12;                          \
        BODY                                                                                             \
        int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;                                                \
        if (ax > pl) pl = ax; if (ay > pr) pr = ay;                                                      \
    }
    if (heard && cap && es == 256 && !rs && !post) { CH_LOOP(ml[i] += x; mr[i] += y; send[i] += (x + y) >> 1; cl[i] += x; cr[i] += y;) }
    else { CH_LOOP(if (heard) { ml[i] += x; mr[i] += y; send[i] += ((x + y) * es) >> 9; rsend[i] += ((x + y) * rs) >> 9; }
                   if (cap) { cl[i] += x; cr[i] += y; } if (post) { l[i] = x; r[i] = y; }) }
#undef CH_LOOP
    ch->gl = tl; ch->gr = tr;
    ch->vu_l = MAX(0, ch->vu_l - ((ch->vu_l * (int32_t)n) >> 10) - 1); if (pl > ch->vu_l) ch->vu_l = MIN(pl, 32767);
    ch->vu_r = MAX(0, ch->vu_r - ((ch->vu_r * (int32_t)n) >> 10) - 1); if (pr > ch->vu_r) ch->vu_r = MIN(pr, 32767);
}
static void mix_idle(int c, uint32_t n) {
    struct mix_channel *ch = &mix.ch[c];
    ch->vu_l = MAX(0, ch->vu_l - ((ch->vu_l * (int32_t)n) >> 10) - 1); ch->vu_r = MAX(0, ch->vu_r - ((ch->vu_r * (int32_t)n) >> 10) - 1);
}

static void render_block(int16_t *out, uint32_t n) {
    int32_t bl[SYNTH_BUSES][SYNTH_BLOCK], br[SYNTH_BUSES][SYNTH_BLOCK];
    int32_t L[SYNTH_BLOCK], R[SYNTH_BLOCK], cl[SYNTH_BLOCK], cr[SYNTH_BLOCK], send[SYNTH_BLOCK], xl[SYNTH_BLOCK], xr[SYNTH_BLOCK];
    int32_t il[SYNTH_BLOCK], ir[SYNTH_BLOCK];
    size_t bytes = n * sizeof L[0];
    memset(L, 0, bytes); memset(R, 0, bytes); memset(cl, 0, bytes); memset(cr, 0, bytes); memset(send, 0, bytes); memset(rsend, 0, bytes);
    static const uint8_t bus_ch[SYNTH_BUSES] = { CH_PLAY, CH_SEQ, CH_RHYTHM, CH_UPIC, CH_CLOUD, CH_DOOM };
    uint32_t active = synth_render(bl, br, n);               /* a bit per bus that has voices */
    if (doomsnd_render(bl[BUS_DOOM], br[BUS_DOOM], n, active >> BUS_DOOM & 1)) active |= 1u << BUS_DOOM;   /* its effects */
    for (int b = 0; b < SYNTH_BUSES; b++)
        if (active >> b & 1) mix_block(bus_ch[b], bl[b], br[b], n, L, R, cl, cr, send, false); else mix_idle(bus_ch[b], n);
    if (touch_render(xl, xr, n)) mix_block(CH_TOUCH, xl, xr, n, L, R, cl, cr, send, false); else mix_idle(CH_TOUCH, n);
    if (ans_render(xl, xr, n)) mix_block(CH_ANS, xl, xr, n, L, R, cl, cr, send, false); else mix_idle(CH_ANS, n);
    bool in = input_pull(il, ir, n);
    if (in) {
        sampler_tap(SMP_IN, il, ir, n);                       /* the sampler takes the input before its fader, */
        if (perf.source == PERF_INPUT) perf_block(il, ir, n); /* the FX page on the input alone */
        mix_block(CH_INPUT, il, ir, n, L, R, cl, cr, send, true);    /* the tape after it */
    } else mix_idle(CH_INPUT, n);
    stretch_capture(cl, cr, n);                              /* what you play (and the input, when heard), dry */
    if (stretch.frozen) {
        memset(xl, 0, bytes); memset(xr, 0, bytes);
        stretch_pull(xl, xr, n);
        sampler_tap(SMP_FREEZE, xl, xr, n);
        mix_block(CH_STRETCH, xl, xr, n, L, R, cl, cr, send, false);
    } else mix_idle(CH_STRETCH, n);
    if (tape_process(cl, cr, in ? il : 0, in ? ir : 0, xl, xr, n)) mix_block(CH_TAPE, xl, xr, n, L, R, 0, 0, send, false);
    else mix_idle(CH_TAPE, n);                               /* the tape records the rest (or the input alone) and plays back */
    echo(send, L, R, n);
    fx_reverb(rsend, L, R, n);
    if (perf.source == PERF_ALL) perf_block(L, R, n);   /* the FX page, on everything heard */
    fx_master(L, R, n);                        /* filter, drive, crush, where they are on */
    sampler_tap(SMP_OUT, L, R, n);             /* resampling: what you hear, before the master volume */
    splash_audio(L, R, n);                     /* the boot's splash, while it sounds */
    /* volume, DC removal, and the block's peak for the limiter */
    int32_t pl = 0, pr = 0, mv = muted ? 0 : vol_q15[vol_step];
    for (uint32_t i = 0; i < n; i++) {
        L[i] = (int32_t)(((int64_t)L[i] * mv) >> 15); R[i] = (int32_t)(((int64_t)R[i] * mv) >> 15);
        dc_l += ((L[i] << 8) - dc_l + 4096) >> 13; dc_r += ((R[i] << 8) - dc_r + 4096) >> 13;   /* rounded: no dead zone */
        int32_t l = L[i] - (dc_l >> 8), r = R[i] - (dc_r >> 8);
        L[i] = l; R[i] = r;
        if (l < 0) l = -l; if (r < 0) r = -r;
        if (l > pl) pl = l; if (r > pr) pr = r;
    }
    for (uint32_t i = 0; i < n; i++) { la_l[la_w & 63] = L[i]; la_r[la_w & 63] = R[i]; la_w++; }
    /* limiter: the peak over this block's output and the LOOK frames after it sets the gain the block ramps to, so no
       frame is louder than the threshold when it goes out; it recovers over ~170 ms */
    int32_t peak = 0;
    for (uint32_t k = 1; k <= LOOK + n; k++) {
        int32_t a = la_l[(la_w - k) & 63], b = la_r[(la_w - k) & 63];
        if (a < 0) a = -a; if (b < 0) b = -b;
        if (a > peak) peak = a; if (b > peak) peak = b;
    }
    for (uint32_t i = 0; i < n; i++) { L[i] = la_l[(la_w - LOOK - n + i) & 63]; R[i] = la_r[(la_w - LOOK - n + i) & 63]; }
    int32_t g0 = lim_gain, g1 = g0 + ((32767 - g0) >> 8);
    if (peak > LIM_THRESH && (int64_t)peak * g1 > (int64_t)LIM_THRESH * 32767) g1 = (int32_t)((int64_t)LIM_THRESH * 32767 / peak);
    int32_t dg = ramp_step(g0, g1, (int)n), g = g0;
    lim_gain = g1;
    if (peak < 65536) {                         /* the usual case: 32-bit products */
        for (uint32_t i = 0; i < n; i++, g += dg) {
            int16_t l = knee((L[i] * g) >> 15), r = knee((R[i] * g) >> 15);
            out[2 * i] = l; out[2 * i + 1] = r;
            scope_l[(scope_head + i) & (AUDIO_SCOPE_LEN - 1)] = l; scope_r[(scope_head + i) & (AUDIO_SCOPE_LEN - 1)] = r;
        }
    } else {
        for (uint32_t i = 0; i < n; i++, g += dg) {
            int16_t l = knee((int32_t)(((int64_t)L[i] * g) >> 15)), r = knee((int32_t)(((int64_t)R[i] * g) >> 15));
            out[2 * i] = l; out[2 * i + 1] = r;
            scope_l[(scope_head + i) & (AUDIO_SCOPE_LEN - 1)] = l; scope_r[(scope_head + i) & (AUDIO_SCOPE_LEN - 1)] = r;
        }
    }
    if (onebit) {
        uint32_t f = synth_mono_freq(), step = f ? (uint32_t)(((uint64_t)f << 32) / rate) : 0;
        int16_t a = (int16_t)((12000 * mv) >> 15);
        for (uint32_t i = 0; i < n; i++) {
            onebit_phase += step;
            int16_t v = !f ? 0 : (onebit_phase & 0x80000000u) ? a : (int16_t)-a;
            out[2 * i] = out[2 * i + 1] = v;
            scope_l[(scope_head + i) & (AUDIO_SCOPE_LEN - 1)] = scope_r[(scope_head + i) & (AUDIO_SCOPE_LEN - 1)] = v;
        }
    }
    scope_head = (scope_head + n) & (AUDIO_SCOPE_LEN - 1);
    /* output meters, from the block peaks (~85 ms fall) */
    peak_l -= (peak_l * (int32_t)n) >> 12; peak_r -= (peak_r * (int32_t)n) >> 12;
    pl = MIN(32767, (int32_t)(((int64_t)pl * g1) >> 15)); pr = MIN(32767, (int32_t)(((int64_t)pr * g1) >> 15));
    if (pl > peak_l) peak_l = pl; if (pr > peak_r) peak_r = pr;
}

void audio_render(int16_t *out, uint32_t frames) {
    delay_new = echo_target();
    /* Link: when this call's first frame will be heard (now, and the time the sound card takes) */
    int64_t heard_us = lnk.on ? (int64_t)plat_us() + (int64_t)plat_audio_latency() * 1000000 / rate : 0;
    uint32_t done = 0;
    while (frames) {
        if (lnk.on) link_audio_block(heard_us + (int64_t)done * 1000000 / rate, MIN(frames, (uint32_t)SYNTH_BLOCK));
        seq_run_events();                       /* everything due at exactly this frame */
        rhythm_run_events();
        uint32_t n = MIN(seq_next_event(), rhythm_next_event());
        if (n > frames) n = frames;
        if (n > SYNTH_BLOCK) n = SYNTH_BLOCK;
        if (n == 0) n = 1;
        cloud_block(n);                         /* the clouds' notes: begin, slide, end */
        upic_block(n);                          /* UPIC's cursor and the arcs it meets */
        inst_block(n);                          /* the instrument showing: its chords, glides, arpeggio */
        doomsnd_block(n);                       /* Doom's music */
        render_block(out, n);
        seq_advance(n); rhythm_advance(n); midi_clock_run(n);
        frames_done += n; done += n;
        out += 2 * n; frames -= n;
    }
}
