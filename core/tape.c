#include "tape.h"
#include "libc.h"
#include "platform.h"
#include "audio.h"
#include "log.h"
#include "tables.h"
#include "undo.h"

struct tape_state tape;
static uint32_t rate = 48000;
static uint32_t k_low, k_high;              /* one-pole coefficients, Q12 */
static uint32_t lfo1, lfo2, lfo1_inc, lfo2_inc;
static uint32_t noise = 0x1234567u;

#define PEAKS (TAPE_BLOCK >> TAPE_PEAK_SHIFT)            /* peaks per block */
static int16_t *pool;                        /* the blocks' frames */
static uint8_t *peaks;                       /* PEAKS per block: the loudest |frame| >> 7 in each 512 */
static uint16_t *free_stack, *dirty;         /* blocks ready to use; blocks freed but not yet cleared */
static uint32_t nfree, ndirty;               /* nfree: taken in the interrupt, given back under irq_save */
static uint32_t comp_mix, comp_in;           /* how far behind the head a recording lands: the output's delay (and the input's) */

/* 10^(dB/20) - 1 in Q12 for -12..+12 dB: what a shelf adds to (or takes from) its band */
static const int16_t shelf_gain_q12[25] = {
    -3067, -2942, -2801, -2643, -2465, -2266, -2043, -1793, -1512, -1196, -842, -445, 0,
    500, 1061, 1690, 2396, 3188, 4077, 5074, 6193, 7448, 8857, 10437, 12210 };

uint32_t tape_rate(void) { return rate; }
uint32_t tape_free_seconds(void) { return (uint32_t)((uint64_t)nfree * TAPE_BLOCK / rate); }

void tape_init(uint32_t r, uint32_t pool_bytes) {
    rate = r ? r : 48000;
    memset(&tape, 0, sizeof tape);
    for (int t = 0; t < TAPE_TRACKS; t++) tape.tr[t].level = 80;
    tape.speed_q12 = 4096; tape.wow = 25; tape.hiss = true;
    k_low  = (uint32_t)(4096ull * 6283 * 250 / (1000ull * rate));          /* 2*pi*fc/rate */
    k_high = (uint32_t)(4096ull * 6283 * 3000 / (1000ull * rate));
    lfo1_inc = (uint32_t)((700ull << 32) / (1000ull * rate));                /* wow 0.7 Hz */
    lfo2_inc = (uint32_t)((6300ull << 32) / (1000ull * rate));               /* flutter 6.3 Hz */
    /* the pool: as many blocks as fit, up to what 8 full tracks could use */
    uint32_t maps = TAPE_TRACKS * TAPE_SPANS * 2, per = TAPE_BLOCK * 2 + PEAKS + 4;
    uint32_t blocks = pool_bytes > maps ? (pool_bytes - maps) / per : 0;
    if (blocks > TAPE_TRACKS * TAPE_SPANS) blocks = TAPE_TRACKS * TAPE_SPANS;
    uint16_t *map = blocks >= 8 ? plat_alloc(maps) : 0;
    pool = map ? plat_alloc(blocks * TAPE_BLOCK * 2) : 0;
    peaks = pool ? plat_alloc(blocks * PEAKS) : 0;
    free_stack = peaks ? plat_alloc(blocks * 2) : 0;
    dirty = free_stack ? plat_alloc(blocks * 2) : 0;
    if (!dirty) { logf("tape: no memory for a tape"); return; }
    memset(map, 0xFF, maps);
    for (int t = 0; t < TAPE_TRACKS; t++) tape.tr[t].map = map + (size_t)t * TAPE_SPANS;
    for (uint32_t b = 0; b < blocks; b++) free_stack[b] = (uint16_t)(blocks - 1 - b);
    nfree = tape.blocks_free = tape.blocks = blocks; ndirty = 0;
    tape.len = TAPE_SPANS << TAPE_BLOCK_SHIFT;
    logf("tape: %u blocks, %u s of mono sound over %d tracks (%u MiB)", blocks, tape_free_seconds(), TAPE_TRACKS,
         (uint32_t)(((uint64_t)blocks * per + maps) >> 20));
}

static inline int16_t *block_ptr(uint16_t b) { return pool + ((size_t)b << TAPE_BLOCK_SHIFT); }
static inline int32_t rd(const struct tape_track *tr, uint32_t p) {
    uint16_t b = tr->map[p >> TAPE_BLOCK_SHIFT];
    return b == TAPE_NO_BLOCK ? 0 : block_ptr(b)[p & (TAPE_BLOCK - 1)];
}
int16_t tape_sample(int t, uint32_t p) { return p < tape.tr[t].used ? (int16_t)rd(&tape.tr[t], p) : 0; }

/* the block under p for writing, taken from the pool if the span has none (its frames and peaks are already clear) */
static uint16_t take_block(struct tape_track *tr, uint32_t p) {
    uint32_t s = p >> TAPE_BLOCK_SHIFT;
    if (s >= TAPE_SPANS) return TAPE_NO_BLOCK;
    uint16_t b = tr->map[s];
    if (b != TAPE_NO_BLOCK) return b;
    uint32_t st = plat_irq_save();
    if (nfree) { b = free_stack[--nfree]; tape.blocks_free = nfree; tr->map[s] = b; }
    plat_irq_restore(st);
    if (b == TAPE_NO_BLOCK) tape.full = true;
    return b;
}

static uint8_t peak_of(int t, uint32_t from, uint32_t to, bool coarse) {
    const struct tape_track *tr = &tape.tr[t];
    if (to > tr->used) to = tr->used;
    if (from >= to) return 0;
    uint32_t pk = 0;
    if (!coarse && to - from < (1u << TAPE_PEAK_SHIFT)) {      /* closer than the peaks go: the frames themselves */
        for (uint32_t p = from; p < to; p++) { int32_t a = rd(tr, p); if (a < 0) a = -a; if ((uint32_t)a >> 7 > pk) pk = (uint32_t)a >> 7; }
        return (uint8_t)MIN(pk, 255u);
    }
    for (uint32_t c = from >> TAPE_PEAK_SHIFT; c <= (to - 1) >> TAPE_PEAK_SHIFT; c++) {
        uint16_t b = tr->map[c / PEAKS];
        if (b != TAPE_NO_BLOCK && peaks[(size_t)b * PEAKS + c % PEAKS] > pk) pk = peaks[(size_t)b * PEAKS + c % PEAKS];
    }
    return (uint8_t)pk;
}
uint8_t tape_peak(int t, uint32_t from, uint32_t to) { return peak_of(t, from, to, false); }
uint8_t tape_peak_coarse(int t, uint32_t from, uint32_t to) { return peak_of(t, from, to, true); }

static inline int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : x; }
static inline int32_t absi(int32_t x) { return x < 0 ? -x : x; }
/* peak meters move once per block: jump up to the block's peak, fall ~21 ms per e-fold */
static inline void meter(int32_t *vu, int32_t peak, uint32_t n) { *vu -= (*vu * (int32_t)n) >> 10; if (*vu < 0) *vu = 0; if (peak > *vu) *vu = peak; }

/* one frame onto a track, and its peak (a peak cell starts over when the head enters it) */
static void put(struct tape_track *tr, uint32_t w, int32_t x) {
    uint16_t b = take_block(tr, w);
    if (b == TAPE_NO_BLOCK) return;
    uint32_t o = w & (TAPE_BLOCK - 1);
    block_ptr(b)[o] = (int16_t)x;
    uint8_t *pk = &peaks[(size_t)b * PEAKS + (o >> TAPE_PEAK_SHIFT)], a = (uint8_t)MIN(absi(x) >> 7, 255);
    if (!(o & ((1u << TAPE_PEAK_SHIFT) - 1))) { *pk = a; tr->gen++; } else if (a > *pk) *pk = a;
    if (w + 1 > tr->used) tr->used = w + 1;
}

bool tape_process(const int32_t *l, const int32_t *r, const int32_t *il, const int32_t *ir, int32_t *out_l, int32_t *out_r, uint32_t n) {
    if (!tape.len) return false;
    static bool was_recording;
    int32_t pk_in = 0, pk_out = 0, pk_tr[TAPE_TRACKS] = { 0 };
    bool rolling = tape.playing;
    if (tape.recording && !was_recording) { comp_mix = plat_audio_latency(); comp_in = comp_mix + audio_input_latency(); tape.full = false; }
    was_recording = tape.recording;
    if (!rolling) {
        for (uint32_t i = 0; i < n; i++) { int32_t a = absi(sat16((l[i] + r[i]) >> 1)); if (a > pk_in) pk_in = a; }
        meter(&tape.vu_in, pk_in, n); meter(&tape.vu_out, 0, n);
        for (int t = 0; t < TAPE_TRACKS; t++) meter(&tape.tr[t].vu, 0, n);
        return false;
    }
    /* each track's gain to the left and right: its level, then the balance */
    bool solo = false; for (int t = 0; t < TAPE_TRACKS; t++) solo |= tape.tr[t].solo;
    int32_t gl[TAPE_TRACKS], gr[TAPE_TRACKS];
    for (int t = 0; t < TAPE_TRACKS; t++) {
        const struct tape_track *tr = &tape.tr[t];
        int32_t g = tr->level * 328;                                                       /* Q15 */
        gl[t] = tr->pan > 0 ? g * (100 - tr->pan) / 100 : g; gr[t] = tr->pan < 0 ? g * (100 + tr->pan) / 100 : g;
    }
    uint32_t lo = 0, hi = tape.len;                                                         /* where the loop turns */
    if (tape.loop) { if (tape.loop_out > tape.loop_in) { lo = tape.loop_in; hi = tape.loop_out; } else if (tape.used) hi = tape.used; }
    for (uint32_t i = 0; i < n; i++) {
        out_l[i] = out_r[i] = 0;
        int32_t in_m = sat16((l[i] + r[i]) >> 1), in_i = il ? sat16((il[i] + ir[i]) >> 1) : 0;
        if (absi(in_m) > pk_in) pk_in = absi(in_m);
        /* head speed (Q16): nominal, plus wow & flutter on playback */
        uint32_t sp = (uint32_t)tape.speed_q12 << 4;
        if (tape.wow && !tape.recording) {
            lfo1 += lfo1_inc; lfo2 += lfo2_inc;
            int32_t m = (sine_q15[lfo1 >> 24] * 3 + sine_q15[lfo2 >> 24]) >> 2;             /* +-32767 */
            sp = (uint32_t)((int32_t)sp + (int32_t)(((int64_t)sp * m * tape.wow) >> 27));    /* wow 100 = about +-2.5 % */
        }
        uint32_t p = tape.pos;
        int32_t pbl = 0, pbr = 0, pbm = 0;
        for (int t = 0; t < TAPE_TRACKS; t++) {
            struct tape_track *tr = &tape.tr[t];
            if (tr->mute || (solo && !tr->solo) || (tape.recording && tr->arm) || p + 1 >= tr->used) continue;
            int32_t s0 = rd(tr, p), s1 = rd(tr, p + 1);
            int32_t s = s0 + (((s1 - s0) * (int32_t)tape.frac) >> 16);
            /* two shelves: a low-passed copy for the bass shelf, the remainder above ~3 kHz for the treble shelf */
            tr->lp_low  += ((s - tr->lp_low) * (int32_t)k_low) >> 12;
            tr->lp_high += ((s - tr->lp_high) * (int32_t)k_high) >> 12;
            int32_t hp = s - tr->lp_high;
            s += (tr->lp_low * shelf_gain_q12[tr->low + 12]) >> 12;
            s += (hp * shelf_gain_q12[tr->high + 12]) >> 12;
            s = sat16(s);
            if (absi(s) > pk_tr[t]) pk_tr[t] = absi(s);
            pbl += (s * gl[t]) >> 15; pbr += (s * gr[t]) >> 15; pbm += (s * tr->level * 41) >> 12;
        }
        if (tape.recording) {
            for (int t = 0; t < TAPE_TRACKS; t++) {
                struct tape_track *tr = &tape.tr[t];
                if (!tr->arm) continue;
                int32_t x = (tr->source == TAPE_SRC_IN ? in_i : in_m) + (tape.bounce ? pbm : 0);
                if (x > 20000) x = 20000 + (x - 20000) / 3; else if (x < -20000) x = -20000 + (x + 20000) / 3;   /* tape saturation */
                uint32_t comp = tr->source == TAPE_SRC_IN ? comp_in : comp_mix;
                if (p < comp && !(tape.loop && p >= lo)) continue;                           /* before the start of the tape */
                uint32_t w = p - comp;
                if (tape.loop && p >= lo && w < lo) w += hi - lo;                             /* heard before the loop turned */
                put(tr, w, sat16(x));
                if (absi(x) > pk_tr[t]) pk_tr[t] = absi(x);
                if (tr->used > tape.used) tape.used = tr->used;
            }
        }
        if (tape.hiss) { noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5; int32_t h = ((int32_t)(noise >> 16) - 32768) >> 9; pbl += h; pbr += h; }
        if (absi(pbl) > pk_out) pk_out = absi(pbl);
        out_l[i] = pbl; out_r[i] = pbr;
        /* move the tape */
        if (tape.recording) { p++; tape.frac = 0; }                                             /* recording runs at normal speed */
        else { uint32_t acc = tape.frac + sp; p += acc >> 16; tape.frac = (uint16_t)acc; }
        if (p >= hi) {
            if (tape.loop) p = lo;
            else { p = hi; tape.playing = false; tape.recording = false; }                      /* the end of the tape */
        }
        tape.pos = p;
        if (!tape.playing) break;
    }
    meter(&tape.vu_in, pk_in, n); meter(&tape.vu_out, pk_out, n);
    for (int t = 0; t < TAPE_TRACKS; t++) meter(&tape.tr[t].vu, pk_tr[t], n);
    return true;
}

void tape_seek(int64_t delta) {
    int64_t np = (int64_t)tape.pos + delta;
    if (np < 0) np = 0; if (np > (int64_t)tape.len) np = tape.len;
    tape.spin = delta < 0 ? -12 : 12;
    tape.pos = (uint32_t)np; tape.frac = 0;
}
void tape_seek_to(uint32_t s) { tape_seek((int64_t)s - (int64_t)tape.pos); }

static void recount(void) { uint32_t u = 0; for (int i = 0; i < TAPE_TRACKS; i++) if (tape.tr[i].used > u) u = tape.tr[i].used; tape.used = u; }
static bool busy(int t) { return tape.recording && tape.tr[t].arm; }

/* a block leaves a track: it is cleared in tape_work before it goes back to the pool */
static void release(struct tape_track *tr, uint32_t s) {
    uint16_t b = tr->map[s];
    if (b == TAPE_NO_BLOCK) return;
    tr->map[s] = TAPE_NO_BLOCK;
    dirty[ndirty++] = b;
}
/* the peaks of cells [c0, c1) of a block, from its frames */
static void repeak(uint16_t b, uint32_t c0, uint32_t c1) {
    const int16_t *f = block_ptr(b);
    for (uint32_t c = c0; c < c1; c++) {
        int32_t m = 0;
        for (uint32_t k = c << TAPE_PEAK_SHIFT; k < (c + 1) << TAPE_PEAK_SHIFT; k++) { int32_t a = absi(f[k]); if (a > m) m = a; }
        peaks[(size_t)b * PEAKS + c] = (uint8_t)MIN(m >> 7, 255);
    }
}

bool tape_erase(int t) {
    if (t < 0 || t >= TAPE_TRACKS || !tape.len || busy(t)) return false;
    struct tape_track *tr = &tape.tr[t];
    for (uint32_t s = 0; s < TAPE_SPANS; s++) release(tr, s);
    tr->used = 0; tr->gen++;
    recount();
    return true;
}

bool tape_erase_range(int t, uint32_t from, uint32_t to) {
    if (t < 0 || t >= TAPE_TRACKS || !tape.len || busy(t)) return false;
    struct tape_track *tr = &tape.tr[t];
    if (to > tr->used) to = tr->used;
    if (from >= to) return true;
    for (uint32_t s = from >> TAPE_BLOCK_SHIFT; s <= (to - 1) >> TAPE_BLOCK_SHIFT; s++) {
        uint32_t a = MAX(from, s << TAPE_BLOCK_SHIFT), z = MIN(to, (s + 1) << TAPE_BLOCK_SHIFT);
        if (a == s << TAPE_BLOCK_SHIFT && z == (s + 1) << TAPE_BLOCK_SHIFT) { release(tr, s); continue; }   /* all of it */
        uint16_t b = tr->map[s];
        if (b == TAPE_NO_BLOCK) continue;
        memset(block_ptr(b) + (a & (TAPE_BLOCK - 1)), 0, (z - a) * 2);
        repeak(b, (a & (TAPE_BLOCK - 1)) >> TAPE_PEAK_SHIFT, (((z - 1) & (TAPE_BLOCK - 1)) >> TAPE_PEAK_SHIFT) + 1);
    }
    if (to >= tr->used) tr->used = from;
    tr->gen++;
    recount();
    return true;
}

/* frames [from, to) of one track onto another at `at`, in pieces; backwards when a track copies onto itself later on */
bool tape_copy(int ft, uint32_t from, uint32_t to, int tt, uint32_t at) {
    if (ft < 0 || tt < 0 || ft >= TAPE_TRACKS || tt >= TAPE_TRACKS || !tape.len || busy(tt)) return false;
    struct tape_track *src = &tape.tr[ft], *dst = &tape.tr[tt];
    if (to > src->used) to = src->used;
    if (from >= to) return true;
    uint32_t n = MIN(to - from, tape.len - MIN(at, tape.len));
    static int16_t buf[4096];
    bool back = ft == tt && at > from, ok = true;
    for (uint32_t done = 0; done < n && ok;) {
        uint32_t c = MIN(4096u, n - done), off = back ? n - done - c : done;
        for (uint32_t k = 0; k < c; k++) buf[k] = (int16_t)rd(src, from + off + k);
        for (uint32_t k = 0; k < c && ok; k++) {
            uint32_t w = at + off + k;
            uint16_t b = buf[k] ? take_block(dst, w) : dst->map[w >> TAPE_BLOCK_SHIFT];   /* silence needs no block */
            if (b == TAPE_NO_BLOCK) { if (buf[k]) ok = false; continue; }
            block_ptr(b)[w & (TAPE_BLOCK - 1)] = buf[k];
        }
        done += c;
    }
    for (uint32_t s = at >> TAPE_BLOCK_SHIFT; s <= (at + n - 1) >> TAPE_BLOCK_SHIFT && s < TAPE_SPANS; s++)
        if (dst->map[s] != TAPE_NO_BLOCK) repeak(dst->map[s], 0, PEAKS);
    if (at + n > dst->used) dst->used = at + n;
    dst->gen++;
    recount();
    return ok;
}

void tape_restore_block(int t, uint32_t s, const int16_t *f, uint32_t used) {
    struct tape_track *tr = &tape.tr[t];
    if (!f) release(tr, s);
    else { int16_t *b = tape_block_for_load(t, s); if (b) { memcpy(b, f, TAPE_BLOCK * 2); repeak(tr->map[s], 0, PEAKS); } }
    tr->used = used; tr->gen++;
    recount();
}

/* A take's undo: the blocks the armed tracks are about to be written into, saved from the main loop a span ahead of
   the head, so the audio interrupt never waits on a copy. */
static bool take_open; static uint32_t take_upto[TAPE_TRACKS];
static void take_ahead(void) {
    uint32_t back = plat_audio_latency() + audio_input_latency();
    for (int t = 0; t < TAPE_TRACKS; t++) {
        if (!tape.tr[t].arm) continue;
        uint32_t from = (tape.pos > back ? tape.pos - back : 0) >> TAPE_BLOCK_SHIFT, to = MIN((tape.pos >> TAPE_BLOCK_SHIFT) + 2, TAPE_SPANS);
        if (tape.loop && tape.loop_out > tape.loop_in) from = MIN(from, tape.loop_in >> TAPE_BLOCK_SHIFT);
        for (uint32_t s = MAX(from, take_upto[t]); s < to; s++) undo_save(U_TAPE, t << 12 | (int)s);
        if (tape.loop && tape.loop_out > tape.loop_in) for (uint32_t s = tape.loop_in >> TAPE_BLOCK_SHIFT; s <= MIN((tape.loop_in >> TAPE_BLOCK_SHIFT) + 1, TAPE_SPANS - 1); s++) undo_save(U_TAPE, t << 12 | (int)s);
        take_upto[t] = MAX(take_upto[t], to);
    }
}
void tape_take_begin(uint64_t now) {
    undo_begin(U_TAPE, -1, "the take", now);
    take_open = true;
    for (int t = 0; t < TAPE_TRACKS; t++) take_upto[t] = 0;
    take_ahead();
}

/* one freed block a pass: cleared, then back in the pool */
void tape_work(void) {
    if (take_open) { if (tape.recording) take_ahead(); else { undo_end(); take_open = false; } }
    if (!ndirty) return;
    uint16_t b = dirty[--ndirty];
    memset(block_ptr(b), 0, TAPE_BLOCK * 2);
    memset(peaks + (size_t)b * PEAKS, 0, PEAKS);
    uint32_t st = plat_irq_save();
    free_stack[nfree++] = b; tape.blocks_free = nfree;
    plat_irq_restore(st);
}

bool tape_write(int t, uint32_t at, const int16_t *f, uint32_t n) {
    if (t < 0 || t >= TAPE_TRACKS || !tape.len || busy(t)) return false;
    struct tape_track *tr = &tape.tr[t];
    uint16_t last = TAPE_NO_BLOCK;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t w = at + i;
        if (w >= tape.len) return false;
        uint16_t b = take_block(tr, w);
        if (b == TAPE_NO_BLOCK) return false;
        block_ptr(b)[w & (TAPE_BLOCK - 1)] = f[i];
        if (b != last) { if (last != TAPE_NO_BLOCK) repeak(last, 0, PEAKS); last = b; }
    }
    if (last != TAPE_NO_BLOCK) repeak(last, 0, PEAKS);
    if (at + n > tr->used) tr->used = at + n;
    tr->gen++;
    recount();
    return true;
}

int tape_spans(int t, uint16_t *spans, int max) {
    int n = 0;
    for (uint32_t s = 0; s < TAPE_SPANS && n < max; s++) if (tape.tr[t].map[s] != TAPE_NO_BLOCK) spans[n++] = (uint16_t)s;
    return n;
}
const int16_t *tape_block(int t, uint32_t s) { uint16_t b = tape.tr[t].map[s]; return b == TAPE_NO_BLOCK ? 0 : block_ptr(b); }

int16_t *tape_block_for_load(int t, uint32_t s) {
    struct tape_track *tr = &tape.tr[t];
    if (s >= TAPE_SPANS || !tape.len) return 0;
    if (tr->map[s] != TAPE_NO_BLOCK) return block_ptr(tr->map[s]);
    uint16_t b = TAPE_NO_BLOCK;
    uint32_t st = plat_irq_save();
    if (nfree) { b = free_stack[--nfree]; tape.blocks_free = nfree; }
    else if (ndirty) b = dirty[--ndirty];                   /* not cleared yet: every frame is about to be read in */
    if (b != TAPE_NO_BLOCK) tr->map[s] = b;
    plat_irq_restore(st);
    return b == TAPE_NO_BLOCK ? 0 : block_ptr(b);
}

void tape_clear_all(void) {
    tape.playing = tape.recording = false;
    for (int t = 0; t < TAPE_TRACKS; t++) { for (uint32_t s = 0; s < TAPE_SPANS && tape.len; s++) release(&tape.tr[t], s); tape.tr[t].used = 0; tape.tr[t].gen++; }
    tape.used = 0; tape.pos = 0;
}

void tape_loaded(int t, uint32_t used) {
    struct tape_track *tr = &tape.tr[t];
    for (uint32_t s = 0; s < TAPE_SPANS; s++) if (tr->map[s] != TAPE_NO_BLOCK) repeak(tr->map[s], 0, PEAKS);
    tr->used = used; tr->gen++;
    recount();
}
