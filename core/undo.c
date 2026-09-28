#include "undo.h"
#include "meta.h"
#include "ans.h"
#include "seq.h"
#include "wave.h"
#include "fm.h"
#include "gendy.h"
#include "sieve.h"
#include "cloud.h"
#include "upic.h"
#include "sampler.h"
#include "tape.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

/* A record: one thing as it was, its bytes packed in the arena in the order records were made. Records of one action
   share a group number and sit together; `redo` marks the ones undoing made. */
struct rec { uint8_t kind, redo; uint16_t group; int32_t index; uint32_t aux, off, size; };
#define MAX_RECS 4096
static struct rec recs[MAX_RECS];
static int nrec;
static uint8_t *arena; static uint32_t cap, top;
static uint16_t seqno, cur;                        /* the action being recorded */
static bool open, dead;                            /* dead: it outgrew the arena and can't be undone */
static struct { uint16_t group; char what[24]; } names[64];
static int last_kind = -1, last_index; static uint64_t last_time; static char last_what[24];
static bool same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

struct song_snap { uint8_t order[SEQ_ORDER]; uint8_t song_len, lpb; uint16_t bpm; struct seq_channel ch[SEQ_TRACKS]; char title[24]; };

void undo_init(uint32_t bytes) {
    arena = bytes ? plat_alloc(bytes) : 0;
    cap = arena ? bytes : 0; top = 0; nrec = 0; open = false;
    logf("undo: %u KiB", cap >> 10);
}
uint32_t undo_capacity(void) { return cap; }

static void name_group(uint16_t g, const char *what) { names[g % 64].group = g; snfmt(names[g % 64].what, sizeof names[0].what, "%s", what); }
static const char *group_name(uint16_t g) { return names[g % 64].group == g ? names[g % 64].what : "the last change"; }

/* the records of a group: [a, b) */
static bool group_range(uint16_t g, bool redo, int *a, int *b) {
    int i = 0;
    while (i < nrec && !(recs[i].group == g && recs[i].redo == redo)) i++;
    if (i == nrec) return false;
    int j = i; while (j < nrec && recs[j].group == g && recs[j].redo == redo) j++;
    *a = i; *b = j;
    return true;
}
static void remove_range(int a, int b) {
    uint32_t from = recs[a].off, to = b < nrec ? recs[b].off : top, cut = to - from;
    memmove(arena + from, arena + to, top - to); top -= cut;
    for (int i = b; i < nrec; i++) recs[i].off -= cut;
    memmove(&recs[a], &recs[b], (size_t)(nrec - b) * sizeof *recs); nrec -= b - a;
}
static void remove_group(uint16_t g, bool redo) { int a, b; if (group_range(g, redo, &a, &b)) remove_range(a, b); }
/* room for `need` bytes: the oldest actions go, but never `keep` or `keep2` */
static bool make_room(uint32_t need, uint16_t keep, uint16_t keep2) {
    while (top + need > cap || nrec >= MAX_RECS) {
        int i = 0;
        while (i < nrec && (recs[i].group == keep || recs[i].group == keep2)) i++;
        if (i == nrec) return false;
        uint16_t g = recs[i].group; bool r = recs[i].redo;
        int a, b; if (!group_range(g, r, &a, &b)) return false;
        remove_range(a, b);
    }
    return true;
}

/* ---- what each kind is, and putting it back ---- */
static uint32_t size_of(int kind, int index, uint32_t *aux) {
    *aux = 0;
    switch (kind) {
    case U_PATTERN: return sizeof(struct seq_pattern);
    case U_SONG:    return sizeof(struct song_snap);
    case U_WAVE:    return sizeof(struct wave_slot);
    case U_FM:      return sizeof(struct fm_patch);
    case U_SAMPLE:  { const struct sample *s = &samples[index]; return sizeof *s + s->len * (s->bits / 8u); }
    case U_SMETA:   return sizeof(struct sample);
    case U_TAPE:    *aux = tape.tr[index >> 12].used; return tape_block(index >> 12, (uint32_t)index & 0xFFF) ? TAPE_BLOCK * 2 : 0;
    case U_ANS:     return ans.plate ? ANS_ROWS * ANS_COLS : 0;
    case U_GENDY:   return sizeof(struct gendy_patch);
    case U_SIEVE:   return SIEVE_TEXT + 1;
    case U_CLOUD:   return sizeof(struct cloud);
    case U_UPIC:    return 4 + sizeof(struct upic_arc) * upic.narcs + sizeof(struct upic_pt) * upic.npts;   /* what the page uses */
    case U_META:    return sizeof meta.fam;
    }
    return 0;
}
static void capture(int kind, int index, uint8_t *d) {
    switch (kind) {
    case U_PATTERN: memcpy(d, &seq_pat[index], sizeof seq_pat[0]); break;
    case U_SONG: {
        struct song_snap s; memcpy(s.order, seq.order, sizeof s.order); s.song_len = seq.song_len; s.lpb = seq.lpb; s.bpm = seq.bpm;
        memcpy(s.ch, seq.ch, sizeof s.ch); memcpy(s.title, seq.title, sizeof s.title);
        memcpy(d, &s, sizeof s); break; }
    case U_WAVE: memcpy(d, &wave_bank[index], sizeof wave_bank[0]); break;
    case U_FM:   memcpy(d, &fm_bank[index], sizeof fm_bank[0]); break;
    case U_SAMPLE: { const struct sample *s = &samples[index]; memcpy(d, s, sizeof *s); memcpy(d + sizeof *s, s->data, s->len * (s->bits / 8u)); break; }
    case U_SMETA: memcpy(d, &samples[index], sizeof samples[0]); break;
    case U_TAPE: { const int16_t *b = tape_block(index >> 12, (uint32_t)index & 0xFFF); if (b) memcpy(d, b, TAPE_BLOCK * 2); break; }
    case U_ANS:  if (ans.plate) memcpy(d, ans.plate, ANS_ROWS * ANS_COLS); break;
    case U_GENDY: memcpy(d, &gendy_bank[index & 3], sizeof gendy_bank[0]); break;
    case U_SIEVE: memcpy(d, sieves[index & 3].text, SIEVE_TEXT); d[SIEVE_TEXT] = sieves[index & 3].unit; break;
    case U_CLOUD: memcpy(d, &clouds[index & 3], sizeof clouds[0]); break;
    case U_UPIC: {
        uint16_t na = upic.narcs, np = upic.npts;
        memcpy(d, &na, 2); memcpy(d + 2, &np, 2);
        memcpy(d + 4, upic.d.arc, sizeof(struct upic_arc) * na); memcpy(d + 4 + sizeof(struct upic_arc) * na, upic.d.pt, sizeof(struct upic_pt) * np);
        break; }
    case U_META: memcpy(d, meta.fam, sizeof meta.fam); break;
    }
}
static void restore(const struct rec *r) {
    const uint8_t *d = arena + r->off;
    switch (r->kind) {
    case U_PATTERN: memcpy(&seq_pat[r->index], d, sizeof seq_pat[0]); break;
    case U_SONG: {
        struct song_snap s; memcpy(&s, d, sizeof s);
        memcpy(seq.order, s.order, sizeof s.order); seq.song_len = s.song_len; seq.lpb = s.lpb; seq.bpm = s.bpm;
        memcpy(seq.ch, s.ch, sizeof s.ch); memcpy(seq.title, s.title, sizeof s.title);
        if (seq.ord >= seq.song_len) seq.ord = 0;
        break; }
    case U_WAVE: memcpy(&wave_bank[r->index], d, sizeof wave_bank[0]); break;
    case U_FM:   memcpy(&fm_bank[r->index], d, sizeof fm_bank[0]); break;
    case U_SAMPLE: { struct sample m; memcpy(&m, d, sizeof m); sampler_restore(r->index, &m, d + sizeof m); break; }
    case U_SMETA: {                                            /* the settings: the markers, loop, root, levels */
        struct sample m, *s = &samples[r->index]; memcpy(&m, d, sizeof m);
        if (m.len == s->len && m.bits == s->bits) { s->start = m.start; s->end = m.end; s->loop_start = m.loop_start; s->loop = m.loop; s->oneshot = m.oneshot;
                                                    s->root = m.root; s->level = m.level; s->attack_ms = m.attack_ms; s->release_ms = m.release_ms; }
        break; }
    case U_TAPE: tape_restore_block(r->index >> 12, (uint32_t)r->index & 0xFFF, r->size ? (const int16_t *)d : 0, r->aux); break;
    case U_ANS:  if (ans.plate && r->size) { memcpy(ans.plate, d, ANS_ROWS * ANS_COLS); memset(ans.dirty, 0xFF, sizeof ans.dirty); } break;
    case U_GENDY: memcpy(&gendy_bank[r->index & 3], d, sizeof gendy_bank[0]); break;
    case U_SIEVE: {                                              /* the text and unit; compiled again (the audio reads it) */
        struct sieve *s = &sieves[r->index & 3];
        memcpy(s->text, d, SIEVE_TEXT); s->text[SIEVE_TEXT - 1] = 0; s->unit = d[SIEVE_TEXT];
        if (!sieve_compile(s)) s->ok = false;
        sieve_changes++;
        break; }
    case U_UPIC: {                                               /* the page as it was: the audio side reads it */
        uint16_t na, np; memcpy(&na, d, 2); memcpy(&np, d + 2, 2);
        na = (uint16_t)MIN(na, UPIC_ARCS); np = (uint16_t)MIN(np, UPIC_POINTS);
        uint32_t st = plat_irq_save();
        upic_all_off();
        memcpy(upic.d.arc, d + 4, sizeof(struct upic_arc) * na); memcpy(upic.d.pt, d + 4 + sizeof(struct upic_arc) * na, sizeof(struct upic_pt) * np);
        upic.narcs = na; upic.npts = np; upic.changes++;
        plat_irq_restore(st);
        break; }
    case U_CLOUD: { bool on = clouds[r->index & 3].on; memcpy(&clouds[r->index & 3], d, sizeof clouds[0]); clouds[r->index & 3].on = on; break; }   /* playing or not stays */
    case U_META: memcpy(meta.fam, d, sizeof meta.fam); meta_compile(); break;
    }
}

/* a record of how this thing is now, in group g */
static bool push(int kind, int index, uint16_t g, bool redo, uint16_t keep) {
    uint32_t aux, size = size_of(kind, index, &aux);
    if (!make_room(size, g, keep)) return false;
    capture(kind, index, arena + top);
    recs[nrec++] = (struct rec){ (uint8_t)kind, redo, g, index, aux, top, size };
    top += size;
    return true;
}

bool undo_saved(int kind, int index) {
    for (int i = nrec - 1; i >= 0 && recs[i].group == cur && !recs[i].redo; i--) if (recs[i].kind == kind && recs[i].index == index) return true;
    return false;
}

void undo_begin(int kind, int index, const char *what, uint64_t now) {
    if (!cap) return;
    /* the same thing again within a second: still the same action (a stroke, a held key) */
    bool more = last_kind == kind && last_index == index && now - last_time < 1000 && same(last_what, what) && nrec && recs[nrec - 1].group == cur && !recs[nrec - 1].redo;
    last_kind = kind; last_index = index; last_time = now; snfmt(last_what, sizeof last_what, "%s", what);
    open = true;
    if (more) return;
    for (int i = nrec - 1; i >= 0; i--) if (recs[i].redo) { int a, b; if (group_range(recs[i].group, true, &a, &b)) { remove_range(a, b); i = nrec; } }   /* a new action: no redo */
    cur = ++seqno; dead = false;
    name_group(cur, what);
}
void undo_save(int kind, int index) {
    if (!open || dead || undo_saved(kind, index)) return;
    if (!push(kind, index, cur, false, cur)) { remove_group(cur, false); dead = true; logf("undo: %s is too big to undo", group_name(cur)); }
}
void undo_end(void) { open = false; }

/* undo (or redo): the latest group of its side; what is there now is saved on the other side first */
static bool swap(bool redo, char *msg, int mcap) {
    int i = nrec - 1;
    while (i >= 0 && recs[i].redo != redo) i--;
    if (i < 0) { snfmt(msg, (size_t)mcap, redo ? "nothing to redo" : "nothing to undo"); return false; }
    uint16_t g = recs[i].group;
    int a, b; group_range(g, redo, &a, &b);
    uint16_t other = ++seqno;
    name_group(other, group_name(g));
    for (int j = 0; j < b - a; j++) {                      /* now's state, for the other way */
        group_range(g, redo, &a, &b);                      /* pushing may have moved g down: count from its start */
        int idx = recs[a + j].index, kind = recs[a + j].kind;
        if (!push(kind, idx, other, !redo, g)) { remove_group(other, !redo); break; }
    }
    group_range(g, redo, &a, &b);
    for (int k = b - 1; k >= a; k--) restore(&recs[k]);   /* the earliest save of a thing is put back last */
    snfmt(msg, (size_t)mcap, "%s: %s", redo ? "redone" : "undone", group_name(g));
    remove_group(g, redo);
    open = false; last_kind = -1;
    return true;
}
bool undo_undo(char *msg, int mcap) { return swap(false, msg, mcap); }
bool undo_redo(char *msg, int mcap) { return swap(true, msg, mcap); }
