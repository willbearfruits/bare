/* The ANS page's instrument; see ans.h. Each tone is a sine at C × 2^(row/72) with its own phase; per block the slit's
   column (between two columns, a blend of both) gives every row a target loudness, the rows ease towards it over a
   few milliseconds, and only the rows sounding are rendered. A level that follows the square root of the summed
   powers keeps one thin scratch and a whole camera picture about as loud as each other. */
#include "ans.h"
#include "seq.h"
#include "platform.h"
#include "tables.h"
#include "libc.h"

struct ans_state ans;
static uint32_t rate;
static uint32_t incs[2][ANS_ROWS]; static volatile int inc_cur; static int inc_octave = -1;
static uint32_t phase[ANS_ROWS];
static int32_t amp[ANS_ROWS];                            /* the rows' loudness now, 0..255 Q8 */
static volatile uint8_t held[ANS_ROWS];                  /* keys and notes held: their brightness */
static volatile uint32_t step_q32 = 1;                   /* columns a frame, Q32 (the main loop keeps it) */
static uint64_t pos_q32;
static int32_t gain_q8 = 1792;
static uint32_t last_frame;

static uint32_t isqrt(uint32_t v) {
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}

static void make_incs(int octave, uint32_t *t) {           /* C of the octave (C0 = 16.352 Hz), then 72 steps an octave */
    t[0] = (uint32_t)(((uint64_t)(4186u << octave) << 24) / rate);
    for (int i = 1; i < 72; i++) t[i] = (uint32_t)(((uint64_t)t[i - 1] * 1084128701u) >> 30);
    for (int i = 72; i < ANS_ROWS; i++) t[i] = t[i - 72] * 2;
}

void ans_init(uint32_t r) {
    rate = r ? r : 48000;
    uint8_t *plate = ans.plate;
    memset(&ans, 0, sizeof ans);
    ans.plate = plate ? plate : plat_alloc(ANS_ROWS * ANS_COLS);
    ans.bars = 4; ans.seconds = 8; ans.octave = 2; ans.level = 80; ans.look = ANS_LIGHT; ans.thresh = 110;
    make_incs(ans.octave, incs[0]); inc_cur = 0; inc_octave = ans.octave;
    memset(amp, 0, sizeof amp); memset((void *)held, 0, sizeof held);
    for (int i = 0; i < ANS_ROWS; i++) phase[i] = (uint32_t)i * 2654435761u;
    memset(ans.dirty, 0xFF, sizeof ans.dirty);
}

void ans_mark(int col) { if (col >= 0 && col < ANS_COLS) ans.dirty[col >> 5] |= 1u << (col & 31); }

uint32_t ans_frames_per_pass(void) {
    if (!ans.bars) return (uint32_t)CLAMP(ans.seconds, 1, 240) * rate;
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    return (uint32_t)(((uint64_t)ans.bars * 4 * 60 * rate << 16) / bpm_q16);
}

void ans_note(int row, uint8_t vel) {
    if (row < 0 || row >= ANS_ROWS) return;
    held[row] = vel ? (uint8_t)(120 + vel) : 0;
}

/* a camera picture into the plate: its light above the black level, or its edges; the top of the picture is high.
   Laid over what is drawn (the brighter wins), or, live, in place of it */
static void picture(const uint8_t *luma, int w, int h, bool over) {
    uint8_t lut[256]; int th = MIN(ans.thresh, 250);
    for (int v = 0; v < 256; v++) lut[v] = (uint8_t)(v > th ? (v - th) * 255 / (255 - th) : 0);
    for (int col = 0; col < ANS_COLS; col++) {
        int x = col * w / ANS_COLS, xa = MAX(x - 1, 0), xb = MIN(x + 1, w - 1);
        uint8_t *dst = ans.plate + col * ANS_ROWS;
        for (int row = 0; row < ANS_ROWS; row++) {
            int y = (h - 1) - row * h / ANS_ROWS, ya = MAX(y - 1, 0), yb = MIN(y + 1, h - 1), v;
            if (ans.look == ANS_EDGES) {
                int gx = luma[y * w + xb] - luma[y * w + xa], gy = luma[yb * w + x] - luma[ya * w + x];
                v = MIN(255, ((gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy)) * 2);
            } else v = luma[y * w + x];
            dst[row] = over ? MAX(dst[row], lut[v]) : lut[v];
        }
    }
    memset(ans.dirty, 0xFF, sizeof ans.dirty);
}

bool ans_snap(void) {
    const uint8_t *l; int w, h;
    if (!plat_camera_frame(&l, &w, &h)) return false;
    picture(l, w, h, true);
    return true;
}
void ans_clear(void) { memset(ans.plate, 0, ANS_ROWS * ANS_COLS); memset(ans.dirty, 0xFF, sizeof ans.dirty); }

void ans_work(uint64_t now) {
    (void)now;
    step_q32 = MAX(1u, (uint32_t)(((uint64_t)ANS_COLS << 32) / MAX(1u, ans_frames_per_pass())));
    if (ans.octave != inc_octave) {                          /* the other table, then switch to it */
        int next = inc_cur ^ 1;
        make_incs(CLAMP(ans.octave, 1, 4), incs[next]);
        inc_cur = next; inc_octave = ans.octave;
    }
    if (ans.camera == ANS_CAM_LIVE) {
        const uint8_t *l; int w, h;
        uint32_t f = plat_camera_frame(&l, &w, &h);
        if (f && f != last_frame) { last_frame = f; picture(l, w, h, false); }
    }
}

bool ans_render(int32_t *l, int32_t *r, uint32_t n) {
    if (!ans.plate) return false;
    bool play = ans.playing;
    int32_t sk = ans.seek;
    if (sk) { pos_q32 = (uint64_t)((sk - 1) % ANS_COLS) << 32; ans.seek = 0; }
    uint32_t c0 = (uint32_t)(pos_q32 >> 32) % ANS_COLS, fr = (uint32_t)(pos_q32 >> 16) & 0xFFFF, c1 = (c0 + 1) % ANS_COLS;
    const uint8_t *p0 = ans.plate + c0 * ANS_ROWS, *p1 = ans.plate + c1 * ANS_ROWS;
    static uint8_t tgt[ANS_ROWS];
    uint32_t sum = 0; bool any = false;
    for (int k = 0; k < ANS_ROWS; k++) {
        uint32_t b = play ? (p0[k] * (65536 - fr) + p1[k] * fr) >> 16 : 0, h = held[k];
        if (h > b) b = h;
        uint32_t t = b * b / 255;
        tgt[k] = (uint8_t)t; sum += t * t;
        any |= t || amp[k];
    }
    if (play) {                                              /* keys held while it plays: written under the slit */
        uint32_t end = (uint32_t)((pos_q32 + (uint64_t)step_q32 * n) >> 32) % ANS_COLS;
        for (uint32_t c = c0; ; c = (c + 1) % ANS_COLS) {
            uint8_t *col = ans.plate + c * ANS_ROWS; bool wrote = false;
            for (int k = 0; k < ANS_ROWS; k++) if (held[k] && held[k] > col[k]) { col[k] = held[k]; wrote = true; }
            if (wrote) ans.dirty[c >> 5] |= 1u << (c & 31);
            if (c == end) break;
        }
        pos_q32 += (uint64_t)step_q32 * n;
        if ((pos_q32 >> 32) >= ANS_COLS) pos_q32 -= (uint64_t)ANS_COLS << 32;
        ans.pos_q16 = (uint32_t)(pos_q32 >> 16);
    }
    if (!any) { gain_q8 = 1792; return false; }
    uint32_t norm = isqrt(sum);                              /* the plate's loudness, whatever its density */
    int32_t gt = (int32_t)(7000u * 256 / MAX(norm, 255u)) * ans.level / 100;
    gain_q8 += (gt - gain_q8) >> 3;
    const uint32_t *inc = incs[inc_cur];
    for (uint32_t i = 0; i < n; i++) l[i] = r[i] = 0;
    for (int k = 0; k < ANS_ROWS; k++) {
        int32_t a0 = amp[k], a1 = a0 + (((int32_t)tgt[k] << 8) - a0) / 6;
        if (!a0 && !a1) continue;
        if (a1 < 64 && !tgt[k]) a1 = 0;
        amp[k] = a1;
        int32_t s0 = (a0 >> 8) * gain_q8 >> 8, s1 = (a1 >> 8) * gain_q8 >> 8, ds = (s1 - s0) / (int32_t)n, s = s0;
        uint32_t ph = phase[k], d = inc[k];
        int32_t *side = k & 1 ? r : l, *other = k & 1 ? l : r;    /* rows alternate a little left and right */
        for (uint32_t i = 0; i < n; i++, s += ds) {
            int32_t v = (sine_q15_8192[ph >> 19] * s) >> 15;
            side[i] += v; other[i] += (v * 5) >> 3;
            ph += d;
        }
        phase[k] = ph;
    }
    return true;
}
