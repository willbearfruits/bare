#include "sampler.h"
#include "synth.h"
#include "stretch.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

struct sample samples[SAMPLE_SLOTS];
struct sampler_state sampler;
static uint32_t out_rate = 48000;

static const uint8_t rate_num[SAMPLE_RATES] = { 1, 2, 1, 1, 1 }, rate_den[SAMPLE_RATES] = { 1, 3, 2, 3, 6 };
uint32_t sampler_rate_hz(int i) { i = CLAMP(i, 0, SAMPLE_RATES - 1); return out_rate * rate_num[i] / rate_den[i]; }
uint32_t sampler_capacity(int slot) { return samples[slot].bits == 8 ? sampler.slot_bytes : sampler.slot_bytes / 2; }

static void blank(struct sample *s, int i) {
    void *d = s->data;
    uint32_t gen = s->gen;
    memset(s, 0, sizeof *s);
    s->data = d; s->gen = gen + 1;
    s->bits = 16; s->rate = out_rate; s->root = 60; s->level = 100; s->attack_ms = 1; s->release_ms = 120;
    snfmt(s->name, sizeof s->name, "SAMPLE %d", i + 1);
}

void sampler_init(uint32_t rate, uint32_t pool_bytes) {
    out_rate = rate ? rate : 48000;
    memset(&sampler, 0, sizeof sampler);
    sampler.rec_bits = 16; sampler.threshold = 600;
    uint32_t per = (pool_bytes / SAMPLE_SLOTS) & ~15u;
    uint8_t *pool = per ? plat_alloc(per * SAMPLE_SLOTS) : 0;
    if (pool) sampler.slot_bytes = per;
    for (int i = 0; i < SAMPLE_SLOTS; i++) { samples[i].data = pool ? pool + (size_t)i * per : 0; blank(&samples[i], i); }
    if (pool) logf("sampler: %d slots of %u KiB (%u s at 16 bit)", SAMPLE_SLOTS, per >> 10, per / 2 / out_rate);
    else logf("sampler: no memory for samples");
}

static inline int32_t get(const struct sample *s, uint32_t i) {
    return s->bits == 8 ? (int32_t)((const int8_t *)s->data)[i] * 256 : ((const int16_t *)s->data)[i];
}
static inline void put(struct sample *s, uint32_t i, int32_t x) {
    x = CLAMP(x, -32767, 32767);
    if (s->bits == 8) { int32_t q = (x + (x >= 0 ? 128 : -128)) / 256; ((int8_t *)s->data)[i] = (int8_t)CLAMP(q, -127, 127); }
    else ((int16_t *)s->data)[i] = (int16_t)x;
}
int32_t sampler_frame(const struct sample *s, uint32_t i) { return i < s->len ? get(s, i) : 0; }

void sampler_span(const struct sample *s, uint32_t from, uint32_t to, int32_t *lo, int32_t *hi) {
    int32_t a = 32767, b = -32768;
    if (to > s->len) to = s->len;
    uint32_t step = to - from > 64 ? (to - from) / 64 : 1;       /* long spans: 64 looks are enough to draw */
    for (uint32_t i = from; i < to; i += step) { int32_t x = get(s, i); if (x < a) a = x; if (x > b) b = x; }
    if (a > b) a = b = 0;
    *lo = a; *hi = b;
}

/* an edit takes the slot away from the voices first: they would read frames being rewritten */
static void edit_begin(struct sample *s) { s->busy = true; synth_kill_preset(P_SMP1 + (int)(s - samples)); }
static void edit_end(struct sample *s) { s->gen++; s->busy = false; }

/* ---- recording ---- */
/* The tap averages the frames between two of the slot's frames (a box filter: the lower rates keep some of the
   aliasing that made the old samplers sound the way they did), and stores them at the slot's depth. */
static uint32_t phase_q16, ratio_q16;
static int32_t acc; static uint32_t acc_n;
static volatile bool finished;
static struct sample before;                             /* the slot as it was when armed: a recording that never
                                                            started gives it back */
static const int32_t recip_q15[8] = { 0, 32768, 16384, 10923, 8192, 6554, 5461, 4681 };

bool sampler_listening(int source) { return sampler.state != SMP_IDLE && sampler.source == source; }

void sampler_tap(int source, const int32_t *l, const int32_t *r, uint32_t n) {
    if (sampler.state == SMP_IDLE || source != sampler.source) return;
    struct sample *s = &samples[sampler.slot];
    uint32_t cap = sampler_capacity(sampler.slot);
    int32_t pk = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = (l[i] + r[i]) >> 1;
        x = CLAMP(x, -32767, 32767);
        int32_t a = x < 0 ? -x : x; if (a > pk) pk = a;
        if (sampler.state == SMP_ARMED) {
            if (a < sampler.threshold) continue;
            sampler.state = SMP_RECORDING;
        }
        acc += x; acc_n++;
        phase_q16 += ratio_q16;
        if (phase_q16 < 65536) continue;
        phase_q16 -= 65536;
        if (sampler.rec_len < cap) put(s, sampler.rec_len++, (acc * recip_q15[acc_n & 7]) >> 15);
        acc = 0; acc_n = 0;
        if (sampler.rec_len >= cap) { sampler.state = SMP_IDLE; finished = true; break; }
    }
    sampler.vu -= sampler.vu * (int32_t)n >> 10; if (pk > sampler.vu) sampler.vu = pk;
}

void sampler_record(int slot) {
    if (!sampler.slot_bytes) return;
    sampler_stop();
    struct sample *s = &samples[slot];
    edit_begin(s);
    before = *s;
    s->bits = sampler.rec_bits; s->rate_i = sampler.rec_rate_i; s->rate = sampler_rate_hz(s->rate_i);
    s->len = s->start = s->end = s->loop_start = 0; s->loop = false;
    static const char *const src_names[SMP_SOURCES] = { "OUT", "FREEZE", "IN" };
    snfmt(s->name, sizeof s->name, "%s %d", src_names[sampler.source % SMP_SOURCES], slot + 1);
    phase_q16 = 0; acc = 0; acc_n = 0;
    ratio_q16 = (uint32_t)(((uint64_t)s->rate << 16) / out_rate);
    uint32_t st = plat_irq_save();
    sampler.slot = (uint8_t)slot; sampler.rec_len = 0; finished = false;
    sampler.state = SMP_ARMED;
    plat_irq_restore(st);
}

/* after a recording: drop the silence at the end, bring the peak up to -1 dB, and give the slot back */
static void finish(void) {
    struct sample *s = &samples[sampler.slot];
    if (!sampler.rec_len) { uint32_t gen = s->gen; *s = before; s->gen = gen; s->busy = true; edit_end(s); return; }
    uint32_t e = sampler.rec_len;
    for (s->len = e; e > 0; e--) { int32_t x = get(s, e - 1); if (x > 200 || x < -200) break; }
    s->len = e; s->start = 0; s->end = e; s->loop_start = 0;
    edit_end(s);
    if (e) sampler_normalize(sampler.slot);
    logf("sampler: slot %d recorded %u frames at %u Hz, %d bit", sampler.slot + 1, e, s->rate, s->bits);
}

void sampler_stop(void) {
    uint32_t st = plat_irq_save();
    bool was = sampler.state != SMP_IDLE;
    sampler.state = SMP_IDLE;
    plat_irq_restore(st);
    if (was || finished) { finished = false; finish(); }
}

void sampler_work(void) { if (finished) { finished = false; finish(); } }

/* ---- the stretcher's ring ---- */
bool sampler_grab(int slot) {
    if (!sampler.slot_bytes) return false;
    uint32_t start, len, ring_len;
    stretch_hold(true);                                  /* the ring holds still while we copy out of it */
    const int16_t *ring = stretch_ring(&ring_len);
    bool found = stretch_phrase(&start, &len);
    struct sample *s = &samples[slot];
    if (found) {
        if (sampler.state != SMP_IDLE && sampler.slot == slot) sampler_stop();
        edit_begin(s);
        s->bits = sampler.rec_bits; s->rate_i = sampler.rec_rate_i; s->rate = sampler_rate_hz(s->rate_i);
        uint32_t cap = sampler_capacity(slot), ratio = (uint32_t)(((uint64_t)s->rate << 16) / out_rate), ph = 0, o = 0;
        int32_t a = 0; uint32_t an = 0;
        for (uint32_t i = 0; i < len && o < cap; i++) {
            a += ring[(start + i) % ring_len]; an++;
            ph += ratio;
            if (ph < 65536) continue;
            ph -= 65536;
            put(s, o++, (a * recip_q15[an & 7]) >> 15); a = 0; an = 0;
        }
        s->len = s->end = o; s->start = s->loop_start = 0; s->loop = false;
        snfmt(s->name, sizeof s->name, "GRAB %d", slot + 1);
        edit_end(s);
        sampler_normalize(slot);
    }
    stretch_hold(false);
    return found;
}

bool sampler_to_stretch(int slot) {
    struct sample *s = &samples[slot];
    if (s->busy || s->end <= s->start) return false;
    uint32_t ring_len;
    stretch_freeze(false);
    stretch_hold(true);
    int16_t *ring = stretch_ring(&ring_len);
    /* back to the output rate: the ring is what the stretcher reads, at the rate everything plays */
    uint32_t step = (uint32_t)(((uint64_t)s->rate << 16) / out_rate), n = 0;
    for (uint64_t p = (uint64_t)s->start << 16; n < ring_len && (uint32_t)(p >> 16) < s->end; n++, p += step)
        ring[n] = (int16_t)get(s, (uint32_t)(p >> 16));
    for (uint32_t i = n; i < ring_len && i < n + STRETCH_MAX_WIN; i++) ring[i] = 0;   /* a short sample: silence after it */
    stretch_freeze_region(0, n);
    stretch_hold(false);
    return true;
}

/* ---- edits ---- */
void sampler_set_format(int slot, int bits, int rate_i) {
    struct sample *s = &samples[slot];
    if (bits != 8) bits = 16;
    rate_i = CLAMP(rate_i, 0, SAMPLE_RATES - 1);
    if (!s->len) { s->bits = (uint8_t)bits; s->rate_i = (uint8_t)rate_i; s->rate = sampler_rate_hz(rate_i); s->gen++; return; }
    edit_begin(s);
    uint32_t new_rate = sampler_rate_hz(rate_i);
    /* the rate: resample in place (down: front to back; up: back to front, so nothing is read after it's written) */
    if (new_rate != s->rate) {
        uint32_t n = MIN((uint32_t)((uint64_t)s->len * new_rate / s->rate), sampler_capacity(slot));
        uint32_t step = (uint32_t)(((uint64_t)s->rate << 16) / new_rate);
        if (new_rate < s->rate) for (uint32_t i = 0; i < n; i++) put(s, i, get(s, (uint32_t)(((uint64_t)i * step) >> 16)));
        else for (uint32_t i = n; i-- > 0;) put(s, i, get(s, (uint32_t)(((uint64_t)i * step) >> 16)));
        uint32_t k = (uint32_t)(((uint64_t)new_rate << 16) / s->rate);
        s->start = (uint32_t)(((uint64_t)s->start * k) >> 16); s->end = (uint32_t)(((uint64_t)s->end * k) >> 16);
        s->loop_start = (uint32_t)(((uint64_t)s->loop_start * k) >> 16);
        s->len = n; s->rate = new_rate;
    }
    s->rate_i = (uint8_t)rate_i;
    /* the depth: 16 → 8 packs forwards, 8 → 16 unpacks backwards; 8 → 16 may not fit, the end goes */
    if (bits != s->bits) {
        if (bits == 8) {
            int16_t *src = s->data; int8_t *dst = s->data;
            for (uint32_t i = 0; i < s->len; i++) { int32_t x = src[i]; dst[i] = (int8_t)CLAMP((x + (x >= 0 ? 128 : -128)) / 256, -127, 127); }
        } else {
            uint32_t n = MIN(s->len, sampler.slot_bytes / 2);
            int8_t *src = s->data; int16_t *dst = s->data;
            for (uint32_t i = n; i-- > 0;) dst[i] = (int16_t)(src[i] * 256);
            s->len = n;
        }
        s->bits = (uint8_t)bits;
    }
    if (s->end > s->len) s->end = s->len;
    if (s->start >= s->end) s->start = 0;
    if (s->loop_start >= s->end) s->loop_start = s->start;
    edit_end(s);
}

void sampler_trim(int slot) {
    struct sample *s = &samples[slot];
    if (s->end <= s->start || (s->start == 0 && s->end == s->len)) return;
    edit_begin(s);
    uint32_t n = s->end - s->start;
    if (s->bits == 8) memmove(s->data, (int8_t *)s->data + s->start, n);
    else memmove(s->data, (int16_t *)s->data + s->start, n * 2);
    s->loop_start = s->loop_start > s->start ? s->loop_start - s->start : 0;
    s->len = s->end = n; s->start = 0;
    edit_end(s);
}

void sampler_normalize(int slot) {
    struct sample *s = &samples[slot];
    int32_t peak = 1;
    for (uint32_t i = 0; i < s->len; i++) { int32_t a = get(s, i); if (a < 0) a = -a; if (a > peak) peak = a; }
    if (peak >= 29000 || peak < 8) return;
    edit_begin(s);
    int32_t g = (int32_t)(29205LL * 4096 / peak);        /* to -1 dBFS, Q12 */
    for (uint32_t i = 0; i < s->len; i++) put(s, i, (get(s, i) * g) >> 12);
    edit_end(s);
}

void sampler_reverse(int slot) {
    struct sample *s = &samples[slot];
    if (s->end <= s->start) return;
    edit_begin(s);
    for (uint32_t a = s->start, b = s->end - 1; a < b; a++, b--) { int32_t t = get(s, a); put(s, a, get(s, b)); put(s, b, t); }
    edit_end(s);
}

void sampler_clear(int slot) {
    struct sample *s = &samples[slot];
    if (sampler.state != SMP_IDLE && sampler.slot == slot) { uint32_t st = plat_irq_save(); sampler.state = SMP_IDLE; plat_irq_restore(st); finished = false; }
    edit_begin(s);
    blank(s, slot);
    edit_end(s);
}

void sampler_copy(int from, int to) {
    if (from == to || !sampler.slot_bytes) return;
    struct sample *d = &samples[to], *s = &samples[from];
    edit_begin(d);
    void *dd = d->data; uint32_t gen = d->gen;
    *d = *s; d->data = dd; d->gen = gen; d->busy = true;
    memcpy(d->data, s->data, s->bits == 8 ? s->len : s->len * 2);
    edit_end(d);
}

bool sampler_begin_write(int slot) {
    struct sample *s = &samples[slot];
    if (!sampler.slot_bytes) return false;
    if (sampler.state != SMP_IDLE && sampler.slot == slot) sampler_stop();
    edit_begin(s);
    s->len = s->start = s->end = s->loop_start = 0; s->loop = false;
    return true;
}
bool sampler_write(int slot, const int16_t *f, uint32_t n) {
    struct sample *s = &samples[slot];
    uint32_t cap = sampler_capacity(slot);
    for (uint32_t i = 0; i < n; i++) { if (s->len >= cap) return false; put(s, s->len++, f[i]); }
    return s->len < cap;
}
void sampler_end_write(int slot, const char *name) {
    struct sample *s = &samples[slot];
    s->end = s->len;
    if (name && *name) snfmt(s->name, sizeof s->name, "%s", name);
    edit_end(s);
}

void sampler_restore(int slot, const struct sample *m, const void *frames) {
    struct sample *s = &samples[slot];
    edit_begin(s);
    void *d = s->data; uint32_t gen = s->gen;
    *s = *m; s->data = d; s->gen = gen; s->busy = true;
    if (d) memcpy(d, frames, s->len * (s->bits / 8u));
    edit_end(s);
}
