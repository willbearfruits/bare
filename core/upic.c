/* UPIC's page and its playing; see upic.h. Before each block the cursor moves (or follows the hand); every arc whose
   time span holds it sounds — a voice started where the cursor met it, bent to the line's pitch every other block —
   and one it has left is let go. Each arc remembers its segment, so the pitch is one division a block. */
#include "upic.h"
#include "synth.h"
#include "seq.h"
#include "platform.h"
#include "libc.h"

struct upic_state upic;
static uint32_t rate = 48000;
static volatile uint32_t step_q32 = 1;               /* cursor per frame (the page = 2^32) */
static uint8_t sounding[UPIC_ARCS]; static uint8_t base[UPIC_ARCS]; static uint16_t seg[UPIC_ARCS]; static int16_t bent[UPIC_ARCS];
static uint32_t blocks;

void upic_init(uint32_t r) {
    rate = r ? r : 48000;
    memset(&upic, 0, sizeof upic);
    upic.bars = 4; upic.seconds = 10; upic.scrub = -1;
    memset(sounding, 0, sizeof sounding);
}
void upic_all_off(void) {
    for (int a = 0; a < UPIC_ARCS; a++) if (sounding[a]) { synth_note_off_tag((uint16_t)(UPIC_TAG | a)); sounding[a] = 0; }
}
void upic_clear(void) {
    uint32_t st = plat_irq_save();
    upic_all_off(); upic.narcs = 0; upic.npts = 0; upic.changes++;
    plat_irq_restore(st);
}

uint32_t upic_frames_per_page(void) {
    if (!upic.bars) return (uint32_t)CLAMP(upic.seconds, 1, 120) * rate;
    uint32_t bpm_q16 = seq_link_q16 ? seq_link_q16 : (uint32_t)(seq.bpm ? seq.bpm : 120) << 16;
    return (uint32_t)(((uint64_t)upic.bars * 4 * 60 * rate << 16) / bpm_q16);
}
void upic_work(uint64_t now) {
    step_q32 = (uint32_t)MAX(1u, (0xFFFFFFFFu / MAX(1u, upic_frames_per_page())));
    if (upic.scrub >= 0 && (int64_t)(now - upic.scrub_ms) > 250) upic.scrub = -1;   /* the view was left mid-scrub */
}

int32_t upic_pitch_at(int a, uint16_t t) {
    const struct upic_arc *arc = &upic.d.arc[a];
    const struct upic_pt *p = upic.d.pt + arc->first;
    int k = MIN(seg[a], (uint16_t)(arc->n - 2));
    while (k > 0 && t < p[k].t) k--;                  /* the cursor may have gone back (scrubbing, a loop) */
    while (k < arc->n - 2 && t >= p[k + 1].t) k++;
    seg[a] = (uint16_t)k;
    int32_t dt = p[k + 1].t - p[k].t;
    if (dt <= 0) return p[k + 1].p;
    return p[k].p + (int32_t)(p[k + 1].p - p[k].p) * (int32_t)(t - p[k].t) / dt;
}

void upic_block(uint32_t n) {
    bool bend_now = (++blocks & 1) == 0;
    int32_t sk = upic.seek;
    if (sk) { upic.pos = (uint32_t)(sk - 1) << 16; upic.seek = 0; }
    int32_t sc = upic.scrub;
    bool live = upic.playing || sc >= 0;
    if (sc >= 0) upic.pos = (uint32_t)sc << 16;
    else if (upic.playing) upic.pos += step_q32 * n;
    uint16_t t = (uint16_t)(upic.pos >> 16);
    int na = upic.narcs;
    for (int a = 0; a < na; a++) {
        const struct upic_arc *arc = &upic.d.arc[a];
        if (arc->n < 2) continue;
        const struct upic_pt *p = upic.d.pt + arc->first;
        bool in = live && t >= p[0].t && t <= p[arc->n - 1].t;
        uint16_t tag = (uint16_t)(UPIC_TAG | a);
        if (!in) { if (sounding[a]) { synth_note_off_tag(tag); sounding[a] = 0; } continue; }
        int32_t pitch = upic_pitch_at(a, t);
        if (!sounding[a]) {                            /* met: a voice at the arc's pitch here */
            int note = CLAMP((pitch + 128) >> 8, 0, 127);
            synth_note_on((uint8_t)note, (uint8_t)CLAMP(arc->level, 1, 127), arc->sound, tag);
            base[a] = (uint8_t)note; bent[a] = 0; sounding[a] = 1;
        }
        int32_t b = CLAMP(pitch - base[a] * 256, -48 * 256, 48 * 256);
        if ((bend_now || !bent[a]) && b != bent[a]) { synth_tag_bend(tag, b); bent[a] = (int16_t)b; }
    }
    for (int a = na; a < UPIC_ARCS; a++) if (sounding[a]) { synth_note_off_tag((uint16_t)(UPIC_TAG | a)); sounding[a] = 0; }
}

/* ---- edits ---- */
int upic_add(const struct upic_pt *pts, int n, uint8_t sound, uint8_t level) {
    if (n < 2 || upic.narcs >= UPIC_ARCS || upic.npts + n > UPIC_POINTS) return -1;
    uint32_t st = plat_irq_save();
    int a = upic.narcs;
    memcpy(upic.d.pt + upic.npts, pts, (size_t)n * sizeof *pts);
    upic.d.arc[a] = (struct upic_arc){ upic.npts, (uint16_t)n, sound, level };
    upic.npts = (uint16_t)(upic.npts + n); seg[a] = 0; sounding[a] = 0;
    upic.narcs = (uint16_t)(a + 1);
    upic.changes++;
    plat_irq_restore(st);
    return a;
}
void upic_delete(int a) {
    if (a < 0 || a >= upic.narcs) return;
    uint32_t st = plat_irq_save();
    upic_all_off();                                   /* the arcs after it move down: their voices go first */
    struct upic_arc gone = upic.d.arc[a];
    memmove(upic.d.pt + gone.first, upic.d.pt + gone.first + gone.n, (size_t)(upic.npts - gone.first - gone.n) * sizeof(struct upic_pt));
    for (int i = a; i < upic.narcs - 1; i++) { upic.d.arc[i] = upic.d.arc[i + 1]; seg[i] = 0; }
    upic.narcs--; upic.npts = (uint16_t)(upic.npts - gone.n);
    for (int i = 0; i < upic.narcs; i++) if (upic.d.arc[i].first > gone.first) upic.d.arc[i].first = (uint16_t)(upic.d.arc[i].first - gone.n);
    upic.changes++;
    plat_irq_restore(st);
}
void upic_transpose(int32_t d) {
    uint32_t st = plat_irq_save();
    upic_all_off();
    for (int i = 0; i < upic.npts; i++) upic.d.pt[i].p = (uint16_t)CLAMP((int32_t)upic.d.pt[i].p + d, 0, 127 * 256);
    upic.changes++;
    plat_irq_restore(st);
}
void upic_mirror_time(void) {
    uint32_t st = plat_irq_save();
    upic_all_off();
    for (int a = 0; a < upic.narcs; a++) {             /* each arc's points reversed, so time still runs forward */
        struct upic_pt *p = upic.d.pt + upic.d.arc[a].first; int n = upic.d.arc[a].n;
        for (int i = 0; i < n / 2; i++) { struct upic_pt x = p[i]; p[i] = p[n - 1 - i]; p[n - 1 - i] = x; }
        for (int i = 0; i < n; i++) p[i].t = (uint16_t)(65535 - p[i].t);
        seg[a] = 0;
    }
    upic.changes++;
    plat_irq_restore(st);
}
void upic_mirror_pitch(void) {
    uint32_t st = plat_irq_save();
    upic_all_off();
    for (int i = 0; i < upic.npts; i++) upic.d.pt[i].p = (uint16_t)CLAMP(UPIC_LO + UPIC_HI - (int32_t)upic.d.pt[i].p, 0, 127 * 256);
    upic.changes++;
    plat_irq_restore(st);
}

uint32_t upic_bytes(void) { return upic.narcs ? sizeof upic.d : 0; }

/* a page read back: whatever doesn't hold together (out of range, too short, time running back) is left out */
void upic_loaded(int narcs, int npts) {
    uint32_t st = plat_irq_save();
    upic_all_off();
    npts = CLAMP(npts, 0, UPIC_POINTS); narcs = CLAMP(narcs, 0, UPIC_ARCS);
    int kept = 0;
    for (int a = 0; a < narcs; a++) {
        struct upic_arc arc = upic.d.arc[a];
        bool ok = arc.n >= 2 && arc.first + arc.n <= npts && arc.sound < P_COUNT;
        for (int i = 1; ok && i < arc.n; i++) ok = upic.d.pt[arc.first + i].t >= upic.d.pt[arc.first + i - 1].t;
        for (int i = 0; ok && i < arc.n; i++) ok = upic.d.pt[arc.first + i].p <= 127 * 256;
        if (ok) { upic.d.arc[kept] = arc; seg[kept] = 0; kept++; }
    }
    upic.narcs = (uint16_t)kept; upic.npts = (uint16_t)npts;
    upic.changes++;
    plat_irq_restore(st);
}
