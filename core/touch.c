#include "touch.h"
#include "tables.h"
#include "libc.h"

struct touch_state touch;
const char *const touch_pad_names[TOUCH_PADS] = { "OUT", "IN-", "C1", "COMP", "+9V", "IN+", "C2", "0V" };
const char *const touch_knob_names[TOUCH_KNOBS] = { "range", "gain", "crackle", "hum", "skin", "tone", "level" };
static const uint8_t knob_default[TOUCH_KNOBS] = { 50, 60, 40, 15, 50, 55, 70 };

/* Units. Voltages are Q15 of the supply. A conductance is written as g16: the corner frequency (in 1/16 Hz) it makes
   with a unit capacitor; a step is 1/96000 s, so it moves a node by k = 2π f / 96000 of the difference (Q16). */
#define RAIL    29000                        /* the op-amp's swing */
#define VCC     26000                        /* the +9V pad */
#define G_EARTH 8                            /* the body's own paths: to ground (0.5 Hz) */
#define HUM_V   26000                        /* what the body picks up from the mains wiring around it */
#define G_STAGE 3200                         /* how firmly the op-amp's inner stage holds against a finger on COMP */
#define KMAX    58982                        /* 0.9: no node moves further than this toward its neighbours in a step */
#define K_DC    69                           /* the output's DC blocker, 8 Hz at 48 kHz */

static uint32_t k_of(uint32_t g16) { return (uint32_t)(((uint64_t)g16 * 17569) >> 16); }

/* the circuit around the op-amp: IN+ gets half the output back (a trigger: it flips, then waits), C1 hangs behind IN−
   and C2 behind IN+. Capacitances in Q8 (256 = the unit); pads without one are driven: OUT, COMP, +9V, 0V. */
static const uint16_t cap_q8[TOUCH_PADS] = { [TP_INV] = 256, [TP_C1] = 384, [TP_NI] = 128, [TP_C2] = 256 };
static const struct { uint8_t to, from; uint32_t g16; } wires[] = {
    { TP_NI, TP_OUT, 19200 }, { TP_NI, TP_GND, 19200 },
    { TP_INV, TP_C1, 4800 },  { TP_C1, TP_INV, 4800 },
    { TP_NI, TP_C2, 6400 },   { TP_C2, TP_NI, 6400 },
    { TP_INV, TP_GND, 16 },   { TP_C1, TP_GND, 16 },  { TP_C2, TP_GND, 16 },
};
#define WIRES ((int)ARRAY_LEN(wires))

/* skin: the conductance of a fingertip from the lightest touch (the edge of the circuit starting) to a firm press, by press/16 */
static const uint16_t skin_g16[17] = { 80, 132, 219, 360, 595, 983, 1623, 2680, 4426, 5347, 6459, 7803, 9426, 11387, 13756, 16618, 20075 };
static const uint32_t exp2_q16[16] = { 65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752, 92682, 96785, 101070, 105545,
                                       110218, 115098, 120194, 125515 };
static uint32_t pow2_q8(int e16) {                                  /* 2^(e16/16), Q8; e16 from -64 to 176 */
    int n = e16 >> 4; uint32_t m = exp2_q16[e16 & 15];
    return n >= 8 ? m << (n - 8) : m >> (8 - n);
}

/* ---- the state: the audio interrupt's own ---- */
static int32_t v[TOUCH_PADS], v_op, dc_q23, lp, env;              /* env: fades the output out when the last finger lifts */
static uint16_t q[TOUCH_FINGERS];                                   /* each finger's contact, Q15: the crackle */
static uint32_t rng = 0x2545F491u, hum_ph;
static int quiet;

/* worked out once a block from the fingers and knobs */
struct contact { uint8_t f, pad; uint32_t k, kg; };                 /* k: into the pad's node; kg: for the glow */
static struct contact con[TOUCH_FINGERS * 4]; static int ncon;
static uint32_t wk[WIRES], w_pad[TOUCH_PADS], w_hum, w_ct, k_op, k_tone, gain_q8;
static uint8_t tpad[TOUCH_PADS]; static int ntpad;
static uint16_t p_drop[TOUCH_FINGERS], rec[TOUCH_FINGERS];
static int32_t pan, level_q12;
static uint32_t g_ant;                                              /* the body's coupling to the mains: the hum knob */

void touch_pad_rect(int pad, int *x0, int *y0, int *x1, int *y1) {
    int col = pad & 3, row = pad >> 2;
    *x0 = col * 8192 + 700; *x1 = (col + 1) * 8192 - 700;
    *y0 = row * 16384 + 1200; *y1 = (row + 1) * 16384 - 1200;
}

void touch_lift_all(void) { for (int f = 0; f < TOUCH_FINGERS; f++) touch.f[f].on = false; }

void touch_init(void) {
    memset(&touch, 0, sizeof touch);
    memcpy(touch.knob, knob_default, sizeof touch.knob);
    memset(v, 0, sizeof v); v[TP_VCC] = VCC; v_op = 0; dc_q23 = lp = env = 0; quiet = 1000;
}

static void prepare(void) {
    uint32_t range = pow2_q8((touch.knob[TK_RANGE] - 50) * 64 / 50), skin = pow2_q8((touch.knob[TK_SKIN] - 50) * 32 / 50);
    uint32_t G[TOUCH_PADS] = { 0 }, ksum[TOUCH_PADS] = { 0 };
    int32_t cx = 0, nf = 0;
    ncon = 0;
    for (int f = 0; f < TOUCH_FINGERS; f++) {
        const struct touch_finger *t = &touch.f[f];
        if (!t->on) { q[f] = 0; continue; }                         /* a new touch starts from no contact */
        int press = t->press, i = press >> 4;
        uint32_t g = skin_g16[i] + (uint32_t)(skin_g16[MIN(i + 1, 16)] - skin_g16[i]) * (uint32_t)(press & 15) / 16;
        g = g * skin >> 8;
        int r = t->size ? 1200 + t->size * 2300 / 255 : 1400 + press * 1600 / 255;
        uint32_t rr = (uint32_t)(r * r) >> 13;
        cx += t->x; nf++;
        for (int p = 0; p < TOUCH_PADS && ncon < (int)ARRAY_LEN(con); p++) {
            int x0, y0, x1, y1; touch_pad_rect(p, &x0, &y0, &x1, &y1);
            int ox = MIN(t->x + r, x1) - MAX(t->x - r, x0), oy = MIN(t->y + r, y1) - MAX(t->y - r, y0);
            if (ox <= 0 || oy <= 0) continue;
            uint32_t share = MIN((uint32_t)(ox * oy) / rr, 32768u);   /* how much of the fingertip lies on the pad */
            uint32_t gp = (uint32_t)((uint64_t)g * share >> 15);
            if (!gp) continue;
            G[p] += gp;
            uint32_t k = 0;
            if (cap_q8[p]) { k = k_of(gp * range >> 8) * 256 / cap_q8[p]; ksum[p] += k; }
            con[ncon++] = (struct contact){ (uint8_t)f, (uint8_t)p, k, k_of(gp) };
        }
        uint32_t light = (uint32_t)(255 - press) * (uint32_t)(255 - press);   /* a light touch keeps losing contact */
        p_drop[f] = (uint16_t)(touch.knob[TK_CRACKLE] * (400 * light / 65025 + 2) / 100);
        rec[f] = (uint16_t)(300 + (uint32_t)press * (uint32_t)press * 2700 / 65025);   /* 2 ms back, light; 0.2 ms, firm */
    }
    for (int w = 0; w < WIRES; w++) { wk[w] = k_of(wires[w].g16 * range >> 8) * 256 / cap_q8[wires[w].to]; ksum[wires[w].to] += wk[w]; }
    for (int p = 0; p < TOUCH_PADS; p++) {                           /* a node pulled too hard would overshoot: slow it */
        if (ksum[p] <= KMAX) continue;
        uint32_t s = (uint32_t)KMAX * 65536u / ksum[p];                  /* 3.9e9: still 32-bit */
        for (int w = 0; w < WIRES; w++) if (wires[w].to == p) wk[w] = wk[w] * s >> 16;
        for (int c = 0; c < ncon; c++) if (con[c].pad == p) con[c].k = (uint32_t)((uint64_t)con[c].k * s >> 16);
    }
    /* the body: each touched pad pulls it by its conductance, and so do the ground and the mains */
    g_ant = 16 + touch.knob[TK_HUM] * 20u;                          /* 1 Hz to 125 Hz */
    uint32_t gt = G_EARTH + g_ant; ntpad = 0;
    for (int p = 0; p < TOUCH_PADS; p++) { gt += G[p]; if (G[p]) tpad[ntpad++] = (uint8_t)p; }
    uint32_t inv = 0xFFFFFFFFu / gt;
    for (int p = 0; p < TOUCH_PADS; p++) w_pad[p] = (uint32_t)(((uint64_t)G[p] * inv) >> 16);
    w_hum = (uint32_t)(((uint64_t)g_ant * inv) >> 16);
    w_ct = G[TP_COMP] ? (uint32_t)(((uint64_t)G[TP_COMP] * (0xFFFFFFFFu / (G[TP_COMP] + G_STAGE))) >> 16) : 0;
    /* the op-amp: gain from 2 to 1024, one pole where its gain-bandwidth puts it */
    gain_q8 = pow2_q8(16 + touch.knob[TK_GAIN] * 144 / 100);
    k_op = MIN(62000u, 4289000u / MAX(gain_q8 >> 8, 1u));
    k_tone = MIN(65535u, pow2_q8(touch.knob[TK_TONE] * 101 / 100) * 67 / 10);   /* 200 Hz to open */
    level_q12 = touch.knob[TK_LEVEL] * 2900 / 100;
    if (nf) pan = (cx / nf - 16384) / 3;
}

/* The steps run in 32-bit arithmetic (the i386 build does 64-bit products in several instructions): coefficients in
   Q14, voltages held within ±40000, so every product stays below 2^31. */
bool touch_render(int32_t *l, int32_t *r, uint32_t n) {
    bool any = false;
    for (int f = 0; f < TOUCH_FINGERS; f++) any |= touch.f[f].on;
    if (any) quiet = 0;
    if (quiet > 40) { touch.sounding = false; return false; }         /* untouched and settled */
    prepare();
    int32_t wk14[WIRES], wp14[TOUCH_PADS], kc14[ARRAY_LEN(con)], kg14[ARRAY_LEN(con)];
    for (int w = 0; w < WIRES; w++) wk14[w] = (int32_t)(wk[w] >> 2);
    for (int t = 0; t < ntpad; t++) wp14[t] = (int32_t)(w_pad[tpad[t]] >> 2);
    for (int c = 0; c < ncon; c++) { kc14[c] = (int32_t)(con[c].k >> 2); kg14[c] = (int32_t)MIN(con[c].kg >> 2, 16383u); }
    int32_t wh14 = (int32_t)(w_hum >> 2), wct14 = (int32_t)(w_ct >> 2), kop14 = (int32_t)(k_op >> 2), kt14 = (int32_t)(k_tone >> 2);
    int32_t lim = (int32_t)((49152u << 8) / gain_q8), g8 = (int32_t)gain_q8;   /* past lim the op-amp is at its rail anyway */
    uint8_t af[TOUCH_FINGERS]; int naf = 0;
    for (int f = 0; f < TOUCH_FINGERS; f++) if (touch.f[f].on) af[naf++] = (uint8_t)f;
    uint32_t dph = touch.hum60 ? 2684355u : 2236962u, glow[TOUCH_PADS] = { 0 };
    int32_t loud = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t sum = 0;
        for (int s = 0; s < 2; s++) {
            hum_ph += dph;                                           /* the mains: a sine with some third in it */
            int32_t hum = (sine_q15[hum_ph >> 24] + sine_q15[(hum_ph * 3) >> 24] * 3 / 8) * HUM_V >> 15;
            for (int a = 0; a < naf; a++) {                          /* each contact drops out now and then, and comes back */
                int f = af[a];
                rng = rng * 1664525u + 1013904223u;
                if ((rng >> 16) < p_drop[f]) q[f] = (uint16_t)(q[f] * (rng & 0x7FFF) >> 15);
                else q[f] = (uint16_t)(q[f] + ((32767u - q[f]) * rec[f] >> 16));
            }
            int32_t acc = wh14 * hum;                                /* the body, where the currents balance */
            for (int t = 0; t < ntpad; t++) acc += wp14[t] * v[tpad[t]];
            int32_t body = acc >> 14, dv[TOUCH_PADS] = { 0 };
            for (int w = 0; w < WIRES; w++) dv[wires[w].to] += (wk14[w] * (v[wires[w].from] - v[wires[w].to])) >> 14;
            for (int c = 0; c < ncon; c++) {
                int pd = con[c].pad; int32_t d = body - v[pd], qq = q[con[c].f];
                if (kc14[c]) dv[pd] += ((kc14[c] * qq >> 15) * d) >> 14;
                if (s) glow[pd] += (uint32_t)(((kg14[c] * qq >> 15) * (d < 0 ? -d : d)) >> 14);
            }
            v[TP_INV] = CLAMP(v[TP_INV] + dv[TP_INV], -40000, 40000); v[TP_C1] = CLAMP(v[TP_C1] + dv[TP_C1], -40000, 40000);
            v[TP_NI] = CLAMP(v[TP_NI] + dv[TP_NI], -40000, 40000);    v[TP_C2] = CLAMP(v[TP_C2] + dv[TP_C2], -40000, 40000);
            /* the op-amp: the difference times its gain, into soft rails (x - 4x³/27, flat at ±1.5), dragged by a
               finger on COMP, then its pole */
            int32_t diff = v[TP_NI] - v[TP_INV];
            int32_t xi = diff > lim ? 49152 : diff < -lim ? -49152 : diff * g8 >> 8;
            int32_t xh = xi >> 1, x3 = ((xh * xh >> 14) * xh) >> 13;
            int32_t target = ((xi - x3 * 4 / 27) * RAIL) >> 15;
            target += (wct14 * (body - target)) >> 14;
            v[TP_COMP] = target;
            v_op += (kop14 * (target - v_op)) >> 14;
            v[TP_OUT] = v_op;
            sum += v_op;
        }
        int32_t y = sum / 2;                                         /* DC out, the speaker's top end, the level */
        dc_q23 += ((y * 256 - dc_q23) * K_DC) >> 16;
        int32_t hp = y - (dc_q23 >> 8);
        lp += ((hp - lp) * kt14) >> 14;
        env += any ? (32767 - env) >> 6 : -(env >> 9);              /* in over 1 ms; out over 10 ms, before the latch thumps */
        int32_t o = (((lp * level_q12) >> 12) * env) >> 15;
        l[i] = (o * (32768 - pan)) >> 15;
        r[i] = (o * (32768 + pan)) >> 15;
        loud = MAX(loud, o < 0 ? -o : o);
    }
    for (int p = 0; p < TOUCH_PADS; p++) {
        uint32_t gnow = MIN(glow[p] * 16 / n, 32767u), was = touch.glow[p] * 15u / 16u;
        touch.glow[p] = (uint16_t)MAX(gnow, was);
    }
    quiet = !any && loud < 48 ? quiet + 1 : 0;
    touch.sounding = true;
    return true;
}
