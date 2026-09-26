#include "link.h"
#include "net.h"
#include "seq.h"
#include "rhythm.h"
#include "midi.h"
#include "audio.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

struct link_state lnk;

#define DISC_PORT 20808
#define MEAS_PORT 20809
#define GROUP     NET_IP(224, 76, 78, 75)
#define TTL_S     5
#define PEERS     16
#define MBEAT     1000000LL                                  /* micro-beats a beat */

struct timeline { int64_t upb, bo, to; };                    /* µs a beat; the beat origin (µbeats) at ghost time `to` */
struct sst { bool playing; int64_t beats, ts; };            /* start/stop: the beat it names, the ghost time it was made */
struct peer { bool used; uint8_t id[8], sess[8]; struct timeline tl; struct sst st; uint32_t mip; uint16_t mport; uint64_t seen; };
static struct peer peers[PEERS];
static uint8_t node[8], session[8];
static struct timeline tl;
static struct sst st;
static int64_t intercept;                                    /* ghost = host + intercept */
static bool changed;
static uint64_t sent_ms, remeasure_ms;
static uint8_t buf[512];
static volatile int32_t phase_err_us;                          /* the sequencer's (or rhythm's) distance from the grid, last block */

/* ---- big-endian fields ---- */
static void p16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void p64(uint8_t *p, int64_t v) { p32(p, (uint32_t)((uint64_t)v >> 32)); p32(p + 4, (uint32_t)v); }
static uint32_t g32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static int64_t g64(const uint8_t *p) { return (int64_t)((uint64_t)g32(p) << 32 | g32(p + 4)); }

/* ---- time and beats ---- */
static int64_t host_now(void) { return (int64_t)plat_us(); }
static int64_t ghost_now(void) { return host_now() + intercept; }
static int64_t beat_at(const struct timeline *t, int64_t ghost) { return t->bo + (ghost - t->to) * MBEAT / t->upb; }
static int64_t upb_of(uint32_t bpm_q16) { return (int64_t)(3932160000000ULL / MAX(bpm_q16, 1u)); }   /* 60e6 × 65536 / BPM_q16 */
static uint32_t bpm_of(int64_t upb) { return (uint32_t)((3932160000000ULL + (uint64_t)MAX(upb, 1) / 2) / (uint64_t)MAX(upb, 1)); }
static void bpm_str(char *out, int cap, uint32_t b) { uint32_t c = (b * 100u + 32768u) >> 16; snfmt(out, cap, "%u.%02u", c / 100, c % 100); }
static int64_t floor_div(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }

/* ---- what the audio side reads: a copy, with a version odd while it is written ---- */
struct snap { bool on; int64_t intercept, upb, bo, to; uint32_t bpm_q16; int64_t arm_seq, arm_rhythm; bool arm_pattern; uint32_t arm_id; };
static struct snap snap; static volatile uint32_t snap_ver;
static volatile uint32_t fired_seq, fired_rhythm;            /* the arm_id the audio side acted on */
static uint32_t arm_id; static int64_t arm_seq, arm_rhythm; static bool arm_pattern;
static void publish(void) {
    snap_ver++;
    snap = (struct snap){ lnk.on, intercept, tl.upb, tl.bo, tl.to, bpm_of(tl.upb), arm_seq, arm_rhythm, arm_pattern, arm_id };
    snap_ver++;
    lnk.bpm_q16 = bpm_of(tl.upb); lnk.playing = st.playing;
}

/* ---- messages: discovery (_asdp_v1) ---- */
static int entry(uint8_t *p, const char *key, int size) { memcpy(p, key, 4); p32(p + 4, (uint32_t)size); return 8; }
static int state_payload(uint8_t *p) {
    int n = 0;
    n += entry(p + n, "tmln", 24); p64(p + n, tl.upb); p64(p + n + 8, tl.bo); p64(p + n + 16, tl.to); n += 24;
    n += entry(p + n, "sess", 8); memcpy(p + n, session, 8); n += 8;
    n += entry(p + n, "stst", 17); p[n] = st.playing; p64(p + n + 1, st.beats); p64(p + n + 9, st.ts); n += 17;
    n += entry(p + n, "mep4", 6); p32(p + n, net.ip); p16(p + n + 4, MEAS_PORT); n += 6;
    return n;
}
static int disc_header(uint8_t *b, int type) {
    memcpy(b, "_asdp_v\x01", 8); b[8] = (uint8_t)type; b[9] = type == 3 ? 0 : TTL_S; b[10] = b[11] = 0; memcpy(b + 12, node, 8);
    return 20;
}
static void send_state(int type, uint32_t ip, uint16_t port) {
    int n = disc_header(buf, type);
    if (type != 3) n += state_payload(buf + n);
    net_send(ip, DISC_PORT, port, buf, n);
}

/* ---- peers ---- */
static struct peer *peer_of(const uint8_t *id, bool make) {
    struct peer *free_p = 0;
    for (int i = 0; i < PEERS; i++) {
        if (peers[i].used && !memcmp(peers[i].id, id, 8)) return &peers[i];
        if (!peers[i].used && !free_p) free_p = &peers[i];
    }
    if (!make || !free_p) return 0;
    memset(free_p, 0, sizeof *free_p); free_p->used = true; memcpy(free_p->id, id, 8);
    return free_p;
}
static int count_session(const uint8_t *sid) { int n = 0; for (int i = 0; i < PEERS; i++) n += peers[i].used && !memcmp(peers[i].sess, sid, 8); return n; }

/* ---- measuring a session's clock through one of its peers: ping, pong, a hundred samples, their median ---- */
#define SAMPLES 101
static struct { bool on; uint8_t sid[8]; uint32_t ip; uint16_t port; int64_t d[SAMPLES + 2]; int n, tries; uint64_t at; } ms;
static struct { uint8_t sid[8]; uint64_t at; } others[4];              /* other sessions measured lately: not again for 30 s */
static bool measured_lately(const uint8_t *sid, uint64_t now) {
    int old = 0;
    for (int i = 0; i < 4; i++) { if (!memcmp(others[i].sid, sid, 8) && others[i].at && now - others[i].at < 30000) return true; if (others[i].at < others[old].at) old = i; }
    memcpy(others[old].sid, sid, 8); others[old].at = now ? now : 1;
    return false;
}
static void ping(int64_t prev_ghost) {
    uint8_t *b = buf; int n = 9;
    memcpy(b, "_link_v\x01", 8); b[8] = 1;
    n += entry(b + n, "__ht", 8); p64(b + n, host_now()); n += 8;
    if (prev_ghost) { n += entry(b + n, "_pgt", 8); p64(b + n, prev_ghost); n += 8; }
    net_send(ms.ip, MEAS_PORT, ms.port, b, n);
    ms.at = plat_ms();
}
static void measure(const uint8_t *sid) {
    struct peer *pick = 0;
    for (int i = 0; i < PEERS; i++) {
        if (!peers[i].used || memcmp(peers[i].sess, sid, 8) || !peers[i].mip) continue;
        if (!pick || !memcmp(peers[i].id, sid, 8)) pick = &peers[i];    /* the founder, where it is there */
    }
    if (!pick || ms.on) return;
    memset(&ms, 0, sizeof ms);
    ms.on = true; memcpy(ms.sid, sid, 8); ms.ip = pick->mip; ms.port = pick->mport;
    ping(0);
}
static void sort64(int64_t *a, int n) { for (int i = 1; i < n; i++) { int64_t v = a[i]; int j = i; while (j && a[j - 1] > v) { a[j] = a[j - 1]; j--; } a[j] = v; } }
static void measured(bool ok, int64_t offset);
static void pong(const uint8_t *p, int n) {
    uint8_t sid[8] = { 0 }; int64_t gt = 0, pgt = 0, ht = 0;
    for (const uint8_t *e = p; e + 8 <= p + n;) {
        uint32_t sz = g32(e + 4); const uint8_t *v = e + 8;
        if (v + sz > p + n) break;
        if (!memcmp(e, "sess", 4) && sz == 8) memcpy(sid, v, 8);
        else if (!memcmp(e, "__gt", 4) && sz == 8) gt = g64(v);
        else if (!memcmp(e, "_pgt", 4) && sz == 8) pgt = g64(v);
        else if (!memcmp(e, "__ht", 4) && sz == 8) ht = g64(v);
        e = v + sz;
    }
    if (memcmp(sid, ms.sid, 8)) { measured(false, 0); return; }
    int64_t now = host_now();
    ping(gt);                                                    /* the next, carrying its ghost time back */
    if (gt && ht) {
        ms.d[ms.n++] = gt - (now + ht) / 2;
        if (pgt && ms.n < SAMPLES + 2) ms.d[ms.n++] = (gt + pgt) / 2 - ht;
    }
    ms.tries = 0;
    if (ms.n > 100) { sort64(ms.d, ms.n); measured(true, ms.d[ms.n / 2]); }
}

/* ---- sessions ---- */
static void adopt_timeline(const struct timeline *t) {
    uint32_t was = bpm_of(tl.upb);
    tl = *t; if (tl.upb <= 0) tl.upb = 500000; changed = true;
    uint32_t b = bpm_of(tl.upb);
    char bs[16]; bpm_str(bs, sizeof bs, b);
    if ((b >> 8) != (was >> 8)) logf("link: tempo %s from the session", bs);
}
static void measured(bool ok, int64_t offset) {
    uint8_t sid[8]; memcpy(sid, ms.sid, 8);
    ms.on = false;
    if (!ok) { if (memcmp(sid, session, 8)) for (int i = 0; i < PEERS; i++) if (!memcmp(peers[i].sess, sid, 8)) peers[i].used = false; return; }
    if (!memcmp(sid, session, 8)) { intercept = offset; publish(); return; }   /* our own session again: the clock trimmed */
    int64_t diff = offset - intercept;                            /* how far ahead its clock is of ours */
    if (diff > 500000 || (diff > -500000 && diff < 500000 && memcmp(sid, session, 8) < 0)) {
        char h[20]; snfmt(h, sizeof h, "%02x%02x%02x%02x", sid[0], sid[1], sid[2], sid[3]);
        memcpy(session, sid, 8); intercept = offset;
        bool got_tl = false; st = (struct sst){ false, 0, 0 };
        for (int i = 0; i < PEERS; i++)                           /* its newest timeline and start/stop, from its peers */
            if (peers[i].used && !memcmp(peers[i].sess, sid, 8)) {
                if (peers[i].tl.upb > 0 && (!got_tl || peers[i].tl.bo > tl.bo)) { tl = peers[i].tl; got_tl = true; }
                if (peers[i].st.ts > st.ts) st = peers[i].st;
            }
        char bs[16]; bpm_str(bs, sizeof bs, bpm_of(tl.upb));
        logf("link: joined session %s…, %s BPM", h, bs);
        changed = true; remeasure_ms = plat_ms();
        publish();
    }
}
static void peer_state(const uint8_t *id, const uint8_t *p, int n, uint32_t src, uint64_t now) {
    struct peer *pr = peer_of(id, true);
    if (!pr) return;
    bool has_tl = false, has_st = false;
    for (const uint8_t *e = p; e + 8 <= p + n;) {
        uint32_t sz = g32(e + 4); const uint8_t *v = e + 8;
        if (v + sz > p + n) break;
        if (!memcmp(e, "tmln", 4) && sz == 24) { pr->tl.upb = g64(v); pr->tl.bo = g64(v + 8); pr->tl.to = g64(v + 16); has_tl = pr->tl.upb > 0; }
        else if (!memcmp(e, "sess", 4) && sz == 8) memcpy(pr->sess, v, 8);
        else if (!memcmp(e, "stst", 4) && sz == 17) { pr->st.playing = v[0] != 0; pr->st.beats = g64(v + 1); pr->st.ts = g64(v + 9); has_st = true; }
        else if (!memcmp(e, "mep4", 4) && sz == 6) { pr->mip = g32(v); pr->mport = g16(v + 4); if (!pr->mip) pr->mip = src; }
        e = v + sz;
    }
    pr->seen = now;
    if (!memcmp(pr->sess, session, 8)) {
        if (has_tl && pr->tl.bo > tl.bo) adopt_timeline(&pr->tl);           /* a newer change in our session */
        if (has_st && pr->st.ts > st.ts) { if (pr->st.playing != st.playing) logf("link: the session %s", pr->st.playing ? "starts" : "stops"); st = pr->st; changed = true; }
    } else if (has_tl && !ms.on && !measured_lately(pr->sess, now)) measure(pr->sess);   /* another session: its clock? */
    publish();
}
static void on_disc(uint32_t src, uint16_t sport, uint32_t dst, const uint8_t *d, int len, uint64_t now) {
    (void)dst;
    if (!lnk.on || len < 20 || memcmp(d, "_asdp_v\x01", 8) || d[10] || d[11] || !memcmp(d + 12, node, 8)) return;
    int type = d[8];
    if (type == 1) send_state(2, src, sport);                     /* alive: answer it, to where it came from */
    if (type == 1 || type == 2) peer_state(d + 12, d + 20, len - 20, src, now);
    else if (type == 3) { struct peer *pr = peer_of(d + 12, false); if (pr) pr->used = false; }
}
static void on_meas(uint32_t src, uint16_t sport, uint32_t dst, const uint8_t *d, int len, uint64_t now) {
    (void)dst; (void)now;
    if (!lnk.on || len < 9 || memcmp(d, "_link_v\x01", 8)) return;
    if (d[8] == 1 && len - 9 <= 32) {                             /* ping: our session, our ghost time, its payload back */
        uint8_t *b = buf; int n = 9;
        memcpy(b, "_link_v\x01", 8); b[8] = 2;
        n += entry(b + n, "sess", 8); memcpy(b + n, session, 8); n += 8;
        n += entry(b + n, "__gt", 8); p64(b + n, ghost_now()); n += 8;
        memcpy(b + n, d + 9, (size_t)(len - 9)); n += len - 9;
        net_send(src, MEAS_PORT, sport, b, n);
    } else if (d[8] == 2 && ms.on && src == ms.ip) pong(d + 9, len - 9);
}

/* ---- the app's side ---- */
static void new_tempo(uint32_t bpm_q16) {                        /* a new timeline, continuous at now, ranked above the old */
    int64_t g = ghost_now(), b = beat_at(&tl, g), upb = upb_of(bpm_q16);
    int64_t bo = MAX(b, tl.bo + 1);
    tl = (struct timeline){ upb, bo, g + (bo - b) * upb / MBEAT };
    changed = true;
}
static void new_start_stop(bool playing, int64_t beat) { st = (struct sst){ playing, beat, ghost_now() }; changed = true; }
/* the next multiple of q beats at or after beat b (µbeats) */
static int64_t next_bar(int64_t b, int64_t q) { return floor_div(b + q - 1, q) * q; }

bool link_start_request(bool pattern_only) {
    if (!lnk.on) return false;
    int64_t q = LINK_QUANTUM * MBEAT, b = beat_at(&tl, ghost_now());
    if (!lnk.peers) {                                             /* alone: the grid moves to us, and it starts now */
        int64_t g = ghost_now(), bo = MAX(next_bar(tl.bo + 1, q), next_bar(b, q));
        tl = (struct timeline){ tl.upb, bo, g };
        new_start_stop(true, bo);
        arm_seq = bo; arm_pattern = pattern_only; arm_id++;
    } else {                                                      /* with others: on the next bar */
        arm_seq = next_bar(b, q); arm_pattern = pattern_only; arm_id++;
        new_start_stop(true, arm_seq);
    }
    publish();
    return true;
}
bool link_rhythm_request(void) {
    if (!lnk.on || seq.playing) return false;                     /* with the sequencer going, it falls in on its step */
    int64_t pb = (int64_t)rhythm_steps() * MBEAT / MAX(rhythm_beat_steps(), 1), b = beat_at(&tl, ghost_now());
    if (!lnk.peers) { int64_t bo = MAX(next_bar(tl.bo + 1, pb), next_bar(b, pb)); tl = (struct timeline){ tl.upb, bo, ghost_now() }; arm_rhythm = bo; changed = true; }
    else arm_rhythm = next_bar(b, pb);
    arm_id++;
    publish();
    return true;
}
void link_stop_request(void) { if (lnk.on) { new_start_stop(false, beat_at(&tl, ghost_now())); publish(); } }

void link_init(void) {
    memset(&lnk, 0, sizeof lnk);
    snfmt(lnk.status, sizeof lnk.status, "off");
    net_listen(DISC_PORT, on_disc);
    net_listen(MEAS_PORT, on_meas);
}
void link_enable(bool on) {
    if (on == lnk.on) return;
    if (!on) { send_state(3, GROUP, DISC_PORT); lnk.on = false; publish(); snfmt(lnk.status, sizeof lnk.status, "off"); seq_link_q16 = 0; return; }
    uint64_t r = plat_us() * 6364136223846793005ULL + 1442695040888963407ULL;
    for (int i = 0; i < 8; i++) { r = r * 6364136223846793005ULL + net.mac[i % 6]; node[i] = (uint8_t)('a' + (r >> 59) % 26); }
    memcpy(session, node, 8);
    memset(peers, 0, sizeof peers); memset(&ms, 0, sizeof ms); memset(others, 0, sizeof others);
    intercept = -host_now();                                      /* our own session's clock starts at 0 */
    tl = (struct timeline){ upb_of((uint32_t)(seq.bpm ? seq.bpm : 120) << 16), 0, 0 };
    st = (struct sst){ seq.playing, 0, 0 };
    lnk.on = true; changed = true;
    net_join(GROUP);
    publish();
    char id[9]; memcpy(id, node, 8); id[8] = 0;
    logf("link: on, node %s", id);
}

void link_work(uint64_t now) {
    if (!lnk.on) return;
    if (net.phase != NET_READY) { snfmt(lnk.status, sizeof lnk.status, "waiting for the network: %s", net.status); return; }
    for (int i = 0; i < PEERS; i++) if (peers[i].used && now - peers[i].seen > TTL_S * 1000) { peers[i].used = false; changed = true; }
    lnk.peers = count_session(session);
    /* the sequencer's own tempo changes (PgUp/PgDn, the tracker's Fxx) go to the session */
    uint32_t mine = (uint32_t)seq.bpm << 16, sess_bpm = bpm_of(tl.upb);
    static uint16_t shown_bpm;
    if (seq.bpm != shown_bpm && (sess_bpm + 32768) >> 16 != seq.bpm) new_tempo(mine);
    seq.bpm = (uint16_t)((bpm_of(tl.upb) + 32768) >> 16); shown_bpm = seq.bpm;
    seq_link_q16 = bpm_of(tl.upb);
    /* start and stop from the session */
    if (fired_seq == arm_id && arm_seq) { arm_seq = 0; midi_transport(true); publish(); }
    if (fired_rhythm == arm_id && arm_rhythm) { arm_rhythm = 0; publish(); }
    if (st.playing && !seq.playing && !arm_seq) {                 /* the others started: in on the next bar */
        int64_t q = LINK_QUANTUM * MBEAT; arm_seq = next_bar(MAX(beat_at(&tl, ghost_now()), st.beats), q); arm_pattern = false; arm_id++; publish();
    }
    if (!st.playing && arm_seq) { arm_seq = 0; publish(); }
    if (!st.playing && seq.playing && !arm_seq) { seq_stop_quietly(); midi_transport(false); }
    /* measurements: a ping lost; our own session again every half minute */
    if (ms.on && now - ms.at > 50) { if (++ms.tries > 5) measured(false, 0); else ping(0); }
    if (!ms.on && lnk.peers && now - remeasure_ms > 30000) { remeasure_ms = now; measure(session); }
    /* our state, four times a second, or at once after a change */
    if ((changed && now - sent_ms >= 50) || now - sent_ms >= 250) { send_state(1, GROUP, DISC_PORT); sent_ms = now; changed = false; publish(); }
    static uint64_t err_at;
    if ((seq.playing || rhythm.playing) && now - err_at >= 2000) { err_at = now; logf("link: %s %d µs off the beat", seq.playing ? "sequencer" : "rhythm", phase_err_us); }
    char bs[16]; bpm_str(bs, sizeof bs, bpm_of(tl.upb));
    snfmt(lnk.status, sizeof lnk.status, "%d other%s · %s BPM%s", lnk.peers, lnk.peers == 1 ? "" : "s", bs, st.playing ? " · playing" : "");
}

/* ---- the audio side: starts on the beat, phases held to the session's grid ---- */
static void lock(int64_t pos_q16, uint32_t unit_q16, int units_per_beat, int64_t beat, int64_t quantum, uint32_t frames, void (*nudge)(int32_t)) {
    if (!unit_q16) return;
    int64_t ours = pos_q16 * MBEAT / 65536 / units_per_beat;      /* our position, µbeats since our start */
    int64_t err = ours - beat;                                     /* against the grid, within the bar */
    err = ((err % quantum) + quantum + quantum / 2) % quantum - quantum / 2;
    int64_t err_q16 = err * (int64_t)unit_q16 * units_per_beat / MBEAT;   /* frames, Q16 */
    phase_err_us = (int32_t)(err_q16 * 1000 / 65536 * 1000 / audio_rate());
    int64_t lim = (int64_t)frames << 16 >> (err > MBEAT / 8 || err < -MBEAT / 8 ? 2 : 4);   /* 25 % of the block when far off, 6 % near */
    nudge((int32_t)CLAMP(err_q16 / 16, -lim, lim));
}
void link_audio_block(int64_t host_us, uint32_t frames) {
    struct snap s; uint32_t v = snap_ver;
    if (v & 1) return;
    s = snap;
    if (snap_ver != v || !s.on || s.upb <= 0) return;
    int64_t beat = s.bo + (host_us + s.intercept - s.to) * MBEAT / s.upb;
    int64_t block = (int64_t)frames * 1000000 / audio_rate() * MBEAT / s.upb;   /* µbeats the block covers */
    if (s.arm_seq && fired_seq != s.arm_id && beat + block > s.arm_seq) {
        int64_t fr = s.arm_seq > beat ? (s.arm_seq - beat) * s.upb / MBEAT * audio_rate() / 1000000 : 0;
        seq_start_in((uint32_t)MIN(fr, (int64_t)frames), s.arm_pattern); fired_seq = s.arm_id;
    }
    if (s.arm_rhythm && fired_rhythm != s.arm_id && beat + block > s.arm_rhythm) {
        int64_t fr = s.arm_rhythm > beat ? (s.arm_rhythm - beat) * s.upb / MBEAT * audio_rate() / 1000000 : 0;
        rhythm_start_in((uint32_t)MIN(fr, (int64_t)frames)); fired_rhythm = s.arm_id;
    }
    if (seq.playing) lock(seq_rows_q16(), seq_row_q16(), seq.lpb ? seq.lpb : 4, beat, LINK_QUANTUM * MBEAT, frames, seq_nudge);
    else if (rhythm.playing) {
        int pb = MAX(rhythm_beat_steps(), 1);
        lock(rhythm_steps_q16(), rhythm_step_q16(), pb, beat, (int64_t)rhythm_steps() * MBEAT / pb, frames, rhythm_nudge);
    }
}
