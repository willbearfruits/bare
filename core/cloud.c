/* The clouds' engine; see cloud.h. Before each audio block: every cloud that is on counts down to its next note
   (exponential waiting times from neglog_q8, which makes the stream Poisson) or, on a rhythm sieve, waits for the next
   sixteenth the sieve keeps and plays as many notes there as the density gives on average; the notes sounding slide
   (their bend, every other block) and end. A note is a synth voice tagged 0x800 | cloud << 6 | slot. */
#include "cloud.h"
#include "harmony.h"
#include "sieve.h"
#include "synth.h"
#include "seq.h"
#include "rhythm.h"
#include "tables.h"
#include "libc.h"

#define TAG_CLOUD 0x800
struct cloud clouds[CLOUDS];
struct cloud_mark cloud_marks[CLOUD_MARKS];
volatile uint32_t cloud_mark_n, cloud_frames;

static const struct cloud defaults[CLOUDS] = {
    /* on    sound   dens low high shape len spr glide lvl dyn wid  pitch rhythm */
    { false, P_PLUCK, 62, 55, 91, 0,  18, 40,  0,  96, 40, 80,  -1,   -1 },    /* pizzicati, everywhere at once */
    { false, P_GD3,   34, 38, 74, 1,  68, 50, 45,  90, 30, 70,  -1,   -1 },    /* glissandi, Pithoprakta's strings */
    { false, P_BELL,  22, 72, 98, 0,  40, 30,  0,  80, 50, 90,   0,   -1 },    /* bells on a sieve's scale */
    { false, P_HAT,   70, 60, 72, 0,   8, 20,  0, 100, 60, 60,  -1,    2 },    /* ticks on S3's sixteenths */
};

static uint32_t rate = 48000, rng = 0x434C4F55u;
static int32_t wait[CLOUDS];                                    /* frames to the next note (free clouds) */
static int32_t last_step = -1;                                  /* the sixteenth the rhythm sieves last looked at */
static uint32_t own_steps, own_q16, step_len_q16 = 6000u << 16, step_bpm_q16;   /* our sixteenths, frames into the next (Q16) */
static bool was_on[CLOUDS];
static struct note { bool on; uint8_t cloud; uint16_t tag; uint32_t left; int32_t bend_q16, dbend_q16; } notes[CLOUD_NOTES];
static uint32_t block_n;

static inline uint32_t rnd_of(uint32_t *r) { uint32_t x = *r; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *r = x; }
static inline uint32_t rnd(void) { return rnd_of(&rng); }
static inline int32_t gauss_of(uint32_t *r) {                    /* about N(0, 1) in Q12: four uniforms summed */
    int32_t s = 0; for (int i = 0; i < 4; i++) s += (int32_t)(rnd_of(r) >> 20);
    return (s - 8190) * 1774 / 1024;                             /* × sqrt(3): the sum's spread made one */
}

static const uint16_t nps_x100[21] = { 20, 28, 40, 56, 80, 112, 159, 224, 317, 448, 632, 893, 1262, 1783, 2518, 3557, 5024, 7096,
                                       10024, 14159, 20000 };
static const uint16_t len_ms[21] = { 20, 26, 33, 44, 57, 75, 98, 128, 167, 218, 283, 369, 482, 629, 821, 1070, 1398, 1824, 2379,
                                     3104, 4000 };
static uint32_t curve(const uint16_t *t, int v) { v = CLAMP(v, 0, 100); int k = v / 5, f = v % 5; return k == 20 ? t[20] : (t[k] * (5u - f) + t[k + 1] * (uint32_t)f) / 5; }
uint32_t cloud_per_second_x100(const struct cloud *c) { return curve(nps_x100, c->density); }
uint32_t cloud_length_ms(const struct cloud *c) { return curve(len_ms, c->length); }

void cloud_sanitize(struct cloud *c) {
    c->on = false;
    if (c->sound >= P_COUNT) c->sound = P_PLUCK;
    c->density = (uint8_t)MIN(c->density, 100); c->length = (uint8_t)MIN(c->length, 100);
    c->low = (uint8_t)CLAMP(c->low, 12, 120); c->high = (uint8_t)CLAMP(c->high, c->low, 120);
    c->shape = c->shape ? 1 : 0; c->spread = (uint8_t)MIN(c->spread, 100); c->glide = (uint8_t)MIN(c->glide, 100);
    c->level = (uint8_t)CLAMP(c->level, 1, 127); c->dyn = (uint8_t)MIN(c->dyn, 100); c->width = (uint8_t)MIN(c->width, 100);
    if (c->pitch_sieve < -1 || c->pitch_sieve > 3) c->pitch_sieve = -1;
    if (c->rhythm_sieve < -1 || c->rhythm_sieve > 3) c->rhythm_sieve = -1;
}

void cloud_init(uint32_t r) {
    rate = r ? r : 48000;
    memcpy(clouds, defaults, sizeof clouds);
    memset(notes, 0, sizeof notes); memset(wait, 0, sizeof wait); memset(was_on, 0, sizeof was_on);
    cloud_mark_n = 0; last_step = -1;
}

void cloud_defaults(void) {
    for (int k = 0; k < CLOUDS; k++) { clouds[k].on = false; cloud_stop(k); }
    memcpy(clouds, defaults, sizeof clouds);
}

void cloud_stop(int k) {
    for (int i = 0; i < CLOUD_NOTES; i++) if (notes[i].on && notes[i].cloud == k) { synth_note_off_tag(notes[i].tag); notes[i].on = false; }
}

/* one note's pitch, length, velocity, place and slide, drawn from cloud k's distributions */
void cloud_draw(int k, uint32_t *r, struct cloud_note *out) {
    const struct cloud *c = &clouds[k & 3];
    int32_t lo = c->low * 256, hi = MAX(c->high, c->low) * 256, p;
    if (c->shape) p = (lo + hi) / 2 + (int32_t)(((int64_t)gauss_of(r) * (hi - lo) / 4) >> 12);
    else p = lo + (int32_t)(((uint64_t)(rnd_of(r) >> 8) * (uint32_t)(hi - lo)) >> 24);
    p = CLAMP(p, lo, hi);
    if (c->pitch_sieve >= 0) p = sieve_snap_pitch(&sieves[c->pitch_sieve], p);
    else p = harmony_snap_q8(p);                              /* keys follow the chord: the clouds too */
    out->pitch = CLAMP(p, 0, 127 * 256);
    uint32_t ms = cloud_length_ms(c);
    int32_t z = (int32_t)(((int64_t)gauss_of(r) * c->spread * 12 * 256 / 100) >> 12);   /* lengths an octave apart, at 100 */
    ms = (uint32_t)(((uint64_t)ms * synth_bend_mul(CLAMP(z, -48 * 256, 48 * 256))) >> 16);
    out->ms = CLAMP(ms, 10u, 8000u);
    out->vel = CLAMP(c->level + (int)((gauss_of(r) * c->dyn * 40 / 100) >> 12), 1, 127);
    out->pan = (int)(((int64_t)((int32_t)(rnd_of(r) >> 16) - 32768) * c->width) >> 15);
    out->glide = (int32_t)(((int64_t)gauss_of(r) * c->glide * 12 * 256 / 100) >> 12);  /* 1/256 semitones a second */
}
uint32_t cloud_wait(int k, uint32_t *r, uint32_t rate_hz) {      /* frames to the next note of a Poisson stream */
    uint32_t mean = rate_hz * 100u / MAX(cloud_per_second_x100(&clouds[k & 3]), 1u);
    return (uint32_t)(((uint64_t)mean * neglog_q8[rnd_of(r) >> 24]) >> 8) + 1;
}

static void play(int k) {                                        /* one note of cloud k */
    const struct cloud *c = &clouds[k];
    int slot = -1; uint32_t least = 0xFFFFFFFFu;
    for (int i = 0; i < CLOUD_NOTES; i++) {                      /* a free slot, else the one nearest its end */
        if (!notes[i].on) { slot = i; break; }
        if (notes[i].left < least) { least = notes[i].left; slot = i; }
    }
    struct note *nt = &notes[slot];
    if (nt->on) synth_note_off_tag(nt->tag);
    struct cloud_note d; cloud_draw(k, &rng, &d);
    int32_t p = d.pitch, glide = d.glide; uint32_t ms = d.ms; int vel = d.vel;
    uint16_t tag = (uint16_t)(TAG_CLOUD | k << 6 | slot);
    synth_note_on_pan((uint8_t)(p >> 8), (uint8_t)vel, c->sound, tag, d.pan);
    nt->on = true; nt->cloud = (uint8_t)k; nt->tag = tag;
    nt->left = ms * (rate / 1000);
    nt->bend_q16 = (p & 255) << 16;
    nt->dbend_q16 = (int32_t)(((int64_t)glide << 16) / (int32_t)rate);
    if (p & 255) synth_tag_bend(tag, p & 255);
    struct cloud_mark *m = &cloud_marks[cloud_mark_n % CLOUD_MARKS];
    m->at = cloud_frames; m->len = nt->left; m->pitch = (int16_t)p; m->glide = (int16_t)CLAMP(glide, -32767, 32767);
    m->cloud = (uint8_t)k; m->vel = (uint8_t)vel;
    cloud_mark_n++;
}

void cloud_block(uint32_t n) {
    cloud_frames += n;
    /* the sixteenths: the rhythm section's while it plays, else our own at the tempo (frames counted, no division) */
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    if (bpm_q16 != step_bpm_q16) { step_bpm_q16 = bpm_q16; step_len_q16 = (uint32_t)(((uint64_t)rate * 60 << 32) / ((uint64_t)bpm_q16 * 4)); }
    for (own_q16 += n << 16; own_q16 >= step_len_q16; own_q16 -= step_len_q16) own_steps++;
    int32_t step = rhythm.playing ? (int32_t)(rhythm_steps_q16() >> 16) : (int32_t)own_steps;
    bool new_step = step != last_step;
    last_step = step;
    for (int k = 0; k < CLOUDS; k++) {
        const struct cloud *c = &clouds[k];
        if (!c->on) { was_on[k] = false; continue; }
        if (!was_on[k]) { was_on[k] = true; wait[k] = 0; }                 /* just on: at once */
        uint32_t nps = cloud_per_second_x100(c);
        if (c->rhythm_sieve >= 0) {                                        /* on the sieve's sixteenths: a Poisson count */
            if (!new_step || step < 0 || !sieve_has(&sieves[c->rhythm_sieve], step)) continue;
            uint32_t x100 = (uint32_t)(((uint64_t)nps * (step_len_q16 >> 8)) / ((uint64_t)rate << 8));   /* notes a step ×100 */
            int count = (int)(x100 / 100);
            if ((uint32_t)(rnd() % 100) < x100 % 100) count++;
            for (int i = 0; i < MIN(count, 6); i++) play(k);
            continue;
        }
        wait[k] -= (int32_t)n;
        for (int guard = 0; wait[k] <= 0 && guard < 8; guard++) {
            play(k);
            uint32_t mean = rate * 100u / MAX(nps, 1u);                   /* frames between notes, on average */
            wait[k] += (int32_t)(((uint64_t)mean * neglog_q8[rnd() >> 24]) >> 8) + 1;   /* a Poisson stream's waits */
        }
        if (wait[k] < -(int32_t)rate) wait[k] = 0;
    }
    /* the notes sounding: slide (every other block), end */
    bool bend_now = (++block_n & 1) == 0;
    for (int i = 0; i < CLOUD_NOTES; i++) {
        struct note *nt = &notes[i];
        if (!nt->on) continue;
        if (nt->left <= n) { synth_note_off_tag(nt->tag); nt->on = false; continue; }
        nt->left -= n;
        if (nt->dbend_q16) {
            nt->bend_q16 += nt->dbend_q16 * (int32_t)n;
            nt->bend_q16 = CLAMP(nt->bend_q16, -48 * 256 * 65536, 48 * 256 * 65536);
            if (bend_now) synth_tag_bend(nt->tag, nt->bend_q16 >> 16);
        }
    }
}
