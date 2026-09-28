#include "project.h"
#include "meta.h"
#include "carlos.h"
#include "junk.h"
#include "drone.h"
#include "phase.h"
#include "harmony.h"
#include "ans.h"
#include "seq.h"
#include "wave.h"
#include "omni.h"
#include "synth.h"
#include "audio.h"
#include "stretch.h"
#include "fm.h"
#include "gendy.h"
#include "sieve.h"
#include "cloud.h"
#include "upic.h"
#include "inst.h"
#include "tape.h"
#include "rhythm.h"
#include "sampler.h"
#include "mix.h"
#include "fx.h"
#include "touch.h"
#include "libc.h"
#include "platform.h"

/* chunk: tag[4] len[4] payload — little endian, no alignment.
   Versions: 1 = the first builds; 2 = FM algorithm 3 (TWO PAIR) routes op 3 into op 1, which v1 left unconnected. */
#define PROJECT_VERSION '2'
static uint8_t *put(uint8_t *p, const char *tag, const void *data, uint32_t len) {
    memcpy(p, tag, 4); memcpy(p + 4, &len, 4); memcpy(p + 8, data, len);
    return p + 8 + len;
}

struct __attribute__((packed)) omni_blob { uint8_t quality, root, pad, strum; int8_t octave; uint8_t echo, draw_slot, hold; };
struct __attribute__((packed)) stretch_blob { uint16_t factor, win; uint8_t mix, stay; };
struct __attribute__((packed)) rhythm_blob { uint8_t pattern, bass, level, mute, chord_level, strings_level; };
/* 2.6: the omnichord after the OM-108 (OMNI and RHYT stay, in their old meanings, for older builds) */
struct __attribute__((packed)) omn2_blob { uint8_t root, suffix, voice, main, sub, sustain, pad_level, flags; int8_t octave, kb_octave, transpose, tune;
                                           uint8_t pad, strum; };
enum { OF_AUTO = 1, OF_HOLD = 2, OF_SYNC = 4, OF_KEYBOARD = 8 };
struct __attribute__((packed)) rhy2_blob { uint8_t pattern, mute, classic, level; };
struct mts1_blob { struct meta_family fam[META_FAMILIES]; uint8_t bars, seconds, pad[2]; };   /* METASTASEIS */
struct crl1_blob { uint8_t scale; int8_t octave; uint8_t mono, glide, wave, cutoff, reso, contour, attack, decay, sustain, release, level, ribbon_steps, pad[2]; };   /* CARLOS */
struct mrz1_blob { uint8_t drive, bits, chop, feedback, grain, bytes_rate, level, pad; };   /* MERZBOW's knobs */
struct rch1_blob { uint8_t mode, players, len, notes[PHASE_STEPS], per_beat, hold, move, drift, sound, slot, level, pad[2]; };   /* REICH */
struct __attribute__((packed)) rdg1_blob { struct drone_partial p[DRONE_PARTIALS]; int32_t target; uint16_t sweep_s; uint8_t fade_s, depth, level, pad[3]; };   /* RADIGUE */
/* the buttons were Eb-first before 2.6 (Db-first now), with three qualities */
static int old_root(int r) { return (r + 10) % OMNI_ROOTS; }
static int new_root(int r) { return (r % OMNI_ROOTS + 2) % OMNI_ROOTS; }
static int old_quality(int suf) { return suf == SUF_7 ? 2 : suf == SUF_MIN || suf == SUF_MIN7 || suf == SUF_DIM ? 1 : 0; }
static int old_pattern(int p) { static const uint8_t near[RHYTHM_PATTERNS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 4, 1, 3, 3 }; return near[p % RHYTHM_PATTERNS]; }
struct __attribute__((packed)) tape_blob { uint8_t level[4]; int8_t low[4], high[4]; uint8_t mute[4]; uint16_t speed; uint8_t wow, hiss, loop, bounce; };   /* 1.0: 4 tracks */
struct __attribute__((packed)) tape8_blob { uint8_t level[TAPE_TRACKS]; int8_t low[TAPE_TRACKS], high[TAPE_TRACKS], pan[TAPE_TRACKS];
                                            uint8_t mute[TAPE_TRACKS], solo[TAPE_TRACKS], source[TAPE_TRACKS]; uint16_t speed; uint8_t wow, hiss, loop, bounce;
                                            uint32_t loop_in, loop_out; };
struct __attribute__((packed)) seq_track_blob { char name[8]; uint8_t preset, gate, mute; uint8_t note[64]; uint8_t vel[64]; };   /* 1.0 */
struct __attribute__((packed)) song_blob { char title[24]; uint16_t bpm; uint8_t lpb, song_len; uint8_t order[SEQ_ORDER];
                                           struct { char name[8]; uint8_t inst, mute; } ch[SEQ_TRACKS]; };
struct __attribute__((packed)) mix_blob { int8_t db[MIX_CHANNELS_V1], pan[MIX_CHANNELS_V1]; uint8_t mute[MIX_CHANNELS_V1], solo[MIX_CHANNELS_V1], echo[MIX_CHANNELS_V1], in_mono; };   /* MIXR: 2.1 and before */
struct __attribute__((packed)) mix2_head { uint8_t channels, in_mono; };                   /* MIX2: this, then a record per channel */
struct __attribute__((packed)) mix2_ch { int8_t db, pan; uint8_t mute, solo, echo, reverb; };
struct __attribute__((packed)) touch_blob { uint8_t knob[TOUCH_KNOBS], hum60; };
/* 1.0 kept FM patches without velocity, key scaling or an LFO, in the compiler's own layout of that time */
struct fm_op_v1 { uint8_t wave, ratio_x2, fine, level; uint16_t a_ms, d_ms, r_ms; uint8_t s_pct; };
struct fm_patch_v1 { char name[12]; uint8_t algo, feedback; struct fm_op_v1 op[FM_OPS]; };
struct __attribute__((packed)) fx_blob { uint8_t rev_size, rev_damp, rev_level, echo_div, echo_feedback, filter_on, filter_mode, filter_cut,
                                         filter_res, drive_on, drive, crush_on, crush_bits, crush_rate, reverb[MIX_CHANNELS_V1]; };   /* MIX2 has every channel's */
struct __attribute__((packed)) sample_blob { uint8_t slot; char name[12]; uint8_t bits, rate_i, loop, oneshot, root, level;
                                             uint16_t attack, release; uint32_t rate, len, start, end, loop_start, offset, bytes; };
#define NO_FRAMES 0xFFFFFFFFu
struct ans_blob { uint8_t bars, seconds, octave, level, look, thresh, pad[2]; uint32_t offset, bytes; };   /* ANS1 */
struct sieve_blob { char text[SIEVE_TEXT]; uint8_t unit, pad[3]; };                                      /* SIEV: one each */
struct upic_blob { uint8_t bars, seconds, pad[2]; uint16_t narcs, npts; uint32_t offset, bytes; };       /* UPC1: the page is media */
struct inst_blob { char name[12]; uint16_t v[IK_COUNT]; };                                              /* INS1: one each, by name */
static int upic_arcs_in = -1, upic_pts_in;                                /* a loaded page, checked once its media are read */
static struct project_media media[SAMPLE_SLOTS + 2];               /* the samples, the ANS plate, UPIC's page */
static int n_media, dropped;
/* the tape's blocks in a project: (track << 12 | span) in order, after the samples from tb_base, 64 KiB each */
static uint16_t tb_list[TAPE_TRACKS * TAPE_SPANS]; static int tb_n; static uint32_t tb_base, tb_used[TAPE_TRACKS];
static bool tape_left_out, loading;
struct __attribute__((packed)) tape_blocks_hdr { uint32_t used[TAPE_TRACKS]; uint32_t base; uint16_t count; };
int  project_media_count(void) { return n_media + tb_n; }
int  project_media_dropped(void) { return dropped; }
bool project_tape_dropped(void) { return tape_left_out; }
uint32_t project_media_end(void) {
    uint32_t end = tb_n ? tb_base + (uint32_t)tb_n * TAPE_BLOCK * 2 : 0;
    for (int i = 0; i < n_media; i++) end = MAX(end, media[i].offset + ((media[i].bytes + 511) & ~511u));
    return end;
}
bool project_media_get(int i, struct project_media *m) {
    if (i < n_media) { *m = media[i]; return true; }
    i -= n_media;
    if (i >= tb_n) return false;
    int t = tb_list[i] >> 12, span = tb_list[i] & 0xFFF;
    void *d = loading ? (void *)tape_block_for_load(t, (uint32_t)span) : (void *)tape_block(t, (uint32_t)span);
    if (!d) return false;
    *m = (struct project_media){ d, TAPE_BLOCK * 2, tb_base + (uint32_t)i * TAPE_BLOCK * 2 };
    return true;
}
void project_loaded(void) {
    if (upic_arcs_in >= 0) { upic_loaded(upic_arcs_in, upic_pts_in); upic_arcs_in = -1; }   /* UPIC's page, read back: checked */
    if (!loading) return;
    for (int t = 0; t < TAPE_TRACKS; t++) tape_loaded(t, tb_used[t]);
    loading = false;
}
struct __attribute__((packed)) seq_blob { char title[24]; uint16_t bpm; uint8_t len; struct seq_track_blob tr[8]; };                  /* 1.0 */

size_t project_save(uint8_t *out, size_t cap, uint32_t room, bool with_tape) {
    if (cap < 16384) return 0;
    uint8_t *p = out;
    p = put(p, "HBPJ", (const char[]){ PROJECT_VERSION }, 1);
    struct omni_blob ob = { (uint8_t)old_quality(omni.suffix), (uint8_t)old_root(omni.root), omni.pad_preset, omni.strum_preset, omni.octave, audio_echo(), (uint8_t)synth_draw_slot, omni.hold };
    p = put(p, "OMNI", &ob, sizeof ob);
    /* the song, then each pattern that has something in it: its number, its rows, its cells */
    struct song_blob sg; memset(&sg, 0, sizeof sg);
    memcpy(sg.title, seq.title, sizeof sg.title); sg.bpm = seq.bpm; sg.lpb = seq.lpb; sg.song_len = seq.song_len;
    memcpy(sg.order, seq.order, sizeof sg.order);
    for (int c = 0; c < SEQ_TRACKS; c++) { memcpy(sg.ch[c].name, seq.ch[c].name, 8); sg.ch[c].inst = seq.ch[c].inst; sg.ch[c].mute = seq.ch[c].mute; }
    p = put(p, "SONG", &sg, sizeof sg);
    for (int n = 0; n < SEQ_PATTERNS; n++) {
        if (!seq_pattern_used(n) && seq_pat[n].rows == SEQ_ROWS) continue;
        uint8_t hdr[2] = { (uint8_t)n, seq_pat[n].rows };
        memcpy(p, "PATN", 4); uint32_t l = 2 + sizeof seq_pat[n].cell; memcpy(p + 4, &l, 4);
        memcpy(p + 8, hdr, 2); memcpy(p + 10, seq_pat[n].cell, sizeof seq_pat[n].cell);
        p += 8 + l;
        if ((size_t)(p - out) > cap - 8192) return 0;                    /* no room: better no save than half a song */
    }
    p = put(p, "WAVE", wave_bank, sizeof wave_bank);
    struct stretch_blob stb = { stretch.factor, stretch.win, stretch.mix, stretch.stay };
    p = put(p, "STRC", &stb, sizeof stb);
    p = put(p, "FMB2", fm_bank, sizeof fm_bank);
    p = put(p, "GDY1", gendy_bank, sizeof gendy_bank);
    struct sieve_blob sv[SIEVES]; memset(sv, 0, sizeof sv);
    for (int i = 0; i < SIEVES; i++) { memcpy(sv[i].text, sieves[i].text, SIEVE_TEXT); sv[i].unit = sieves[i].unit; }
    p = put(p, "SIEV", sv, sizeof sv);
    struct cloud cb[CLOUDS]; memcpy(cb, clouds, sizeof cb);
    for (int i = 0; i < CLOUDS; i++) cb[i].on = false;                   /* a project doesn't start its clouds */
    p = put(p, "CLD1", cb, sizeof cb);
    struct inst_blob ib[INSTS]; memset(ib, 0, sizeof ib);             /* the instruments' knobs, as turned */
    for (int i = 0; i < inst_count; i++) { memcpy(ib[i].name, insts[i].name, sizeof ib[i].name); for (int k = 0; k < IK_COUNT; k++) ib[i].v[k] = (uint16_t)inst_knob_get(&insts[i], k); }
    if (inst_count) p = put(p, "INS1", ib, (uint32_t)(inst_count * sizeof ib[0]));
    struct tape8_blob tb; memset(&tb, 0, sizeof tb);
    for (int t = 0; t < TAPE_TRACKS; t++) {
        const struct tape_track *tr = &tape.tr[t];
        tb.level[t] = tr->level; tb.low[t] = tr->low; tb.high[t] = tr->high; tb.pan[t] = tr->pan; tb.mute[t] = tr->mute; tb.solo[t] = tr->solo; tb.source[t] = tr->source;
    }
    tb.speed = tape.speed_q12; tb.wow = tape.wow; tb.hiss = tape.hiss; tb.loop = tape.loop; tb.bounce = tape.bounce; tb.loop_in = tape.loop_in; tb.loop_out = tape.loop_out;
    p = put(p, "TAP8", &tb, sizeof tb);
    struct rhythm_blob rb = { (uint8_t)old_pattern(rhythm.pattern), omni.autoplay, rhythm.level, (uint8_t)(rhythm.mute & 15), omni.pad_level, omni.main_level };
    p = put(p, "RHYT", &rb, sizeof rb);
    struct omn2_blob o2 = { omni.root, omni.suffix, omni.voice, omni.main_level, omni.sub_level, omni.sustain, omni.pad_level,
                            (uint8_t)((omni.autoplay ? OF_AUTO : 0) | (omni.hold ? OF_HOLD : 0) | (omni.sync ? OF_SYNC : 0) | (omni.keyboard ? OF_KEYBOARD : 0)),
                            omni.octave, omni.kb_octave, omni.transpose, omni.tune, omni.pad_preset, omni.strum_preset };
    p = put(p, "OMN2", &o2, sizeof o2);
    struct rhy2_blob r2 = { rhythm.next != 0xFF ? rhythm.next : rhythm.pattern, rhythm.mute, rhythm.classic, rhythm.level };
    p = put(p, "RHY2", &r2, sizeof r2);
    p = put(p, "HRM1", (const uint8_t[]){ harmony_on }, 1);          /* keys follow the chord */
    struct mts1_blob mb1 = { { 0 }, meta.bars, meta.seconds }; memcpy(mb1.fam, meta.fam, sizeof meta.fam);
    p = put(p, "MTS1", &mb1, sizeof mb1);                               /* METASTASEIS's families */
    struct crl1_blob crl = { carlos.scale, carlos.octave, carlos.mono, carlos.glide, carlos.wave, carlos.cutoff, carlos.reso, carlos.contour,
                            carlos.attack, carlos.decay, carlos.sustain, carlos.release, carlos.level, carlos.ribbon_steps, { 0 } };
    p = put(p, "CRL1", &crl, sizeof crl);
    struct mrz1_blob zb = { junk.drive, junk.bits, junk.chop, junk.feedback, junk.grain, junk.bytes_rate, junk.level, 0 };
    p = put(p, "MRZ1", &zb, sizeof zb);
    struct rdg1_blob rg; memcpy(rg.p, radigue.p, sizeof rg.p);
    rg.target = radigue.target; rg.sweep_s = radigue.sweep_s; rg.fade_s = radigue.fade_s; rg.depth = radigue.depth; rg.level = radigue.level; memset(rg.pad, 0, sizeof rg.pad);
    p = put(p, "RDG1", &rg, sizeof rg);                                 /* the drone's patch (whether it sounds is not kept) */
    struct rch1_blob rc = { reich.mode, reich.players, reich.len, { 0 }, reich.per_beat, reich.hold, reich.move, reich.drift, reich.sound, reich.slot, reich.level, { 0 } };
    memcpy(rc.notes, reich.notes, sizeof rc.notes);
    p = put(p, "RCH1", &rc, sizeof rc);                                 /* REICH's pattern and process */
    uint8_t mb[sizeof(struct mix2_head) + MIX_CHANNELS * sizeof(struct mix2_ch)];
    struct mix2_head mh = { MIX_CHANNELS, mix.in_mono }; memcpy(mb, &mh, sizeof mh);
    for (int c = 0; c < MIX_CHANNELS; c++) {
        const struct mix_channel *ch = &mix.ch[c];
        struct mix2_ch mc = { ch->db, ch->pan, ch->mute, ch->solo, ch->echo, ch->reverb };
        memcpy(mb + sizeof mh + c * sizeof mc, &mc, sizeof mc);
    }
    p = put(p, "MIX2", mb, sizeof mb);
    struct touch_blob tb2; memcpy(tb2.knob, touch.knob, sizeof tb2.knob); tb2.hum60 = touch.hum60;
    p = put(p, "TUCH", &tb2, sizeof tb2);
    struct fx_blob fb = { fx.rev_size, fx.rev_damp, fx.rev_level, fx.echo_div, fx.echo_feedback, fx.filter_on, fx.filter_mode, fx.filter_cut,
                          fx.filter_res, fx.drive_on, fx.drive, fx.crush_on, fx.crush_bits, fx.crush_rate, { 0 } };
    for (int c = 0; c < MIX_CHANNELS_V1; c++) fb.reverb[c] = mix.ch[c].reverb;
    p = put(p, "MFX1", &fb, sizeof fb);
    /* samples: the settings here, the frames after the blob (512-byte aligned, in the room there is) */
    uint32_t at = 0; n_media = 0; dropped = 0;
    for (int i = 0; i < SAMPLE_SLOTS; i++) {
        const struct sample *s = &samples[i];
        if (!s->len || !s->data) continue;
        uint32_t bytes = s->len * (s->bits / 8u), span = (bytes + 511) & ~511u;
        struct sample_blob sb = { (uint8_t)i, {0}, s->bits, s->rate_i, s->loop, s->oneshot, s->root, s->level, s->attack_ms, s->release_ms,
                                  s->rate, s->len, s->start, s->end, s->loop_start, NO_FRAMES, bytes };
        memcpy(sb.name, s->name, sizeof sb.name);
        if (at + span <= room) { sb.offset = at; media[n_media++] = (struct project_media){ s->data, bytes, at }; at += span; }
        else dropped++;
        p = put(p, "SMPL", &sb, sizeof sb);
    }
    /* the ANS plate: its settings here, its picture after the samples (when something is on it) */
    struct ans_blob ab = { ans.bars, ans.seconds, ans.octave, ans.level, ans.look, ans.thresh, { 0 }, NO_FRAMES, 0 };
    bool drawn = false;
    for (uint32_t i = 0; ans.plate && i < ANS_ROWS * ANS_COLS && !drawn; i++) drawn = ans.plate[i] != 0;
    if (drawn) {
        uint32_t bytes = ANS_ROWS * ANS_COLS, span = (bytes + 511) & ~511u;
        if (at + span <= room) { ab.offset = at; ab.bytes = bytes; media[n_media++] = (struct project_media){ ans.plate, bytes, at }; at += span; }
        else dropped++;
    }
    p = put(p, "ANS1", &ab, sizeof ab);
    /* UPIC's page: its length here, its arcs and points after the plate */
    struct upic_blob ub = { upic.bars, upic.seconds, { 0 }, upic.narcs, upic.npts, NO_FRAMES, 0 };
    uint32_t ubytes = upic_bytes();
    if (ubytes) {
        uint32_t span = (ubytes + 511) & ~511u;
        if (at + span <= room) { ub.offset = at; ub.bytes = ubytes; media[n_media++] = (struct project_media){ &upic.d, ubytes, at }; at += span; }
        else dropped++;
    }
    p = put(p, "UPC1", &ub, sizeof ub);
    /* the tape: which blocks each track has; their frames follow the samples' */
    tb_n = 0; tb_base = at; tape_left_out = false; loading = false;
    if (tape.len) {
        for (int t = 0; t < TAPE_TRACKS; t++) {
            uint16_t spans[TAPE_SPANS]; int n = tape_spans(t, spans, TAPE_SPANS);
            for (int k = 0; k < n; k++) tb_list[tb_n++] = (uint16_t)(t << 12 | spans[k]);
        }
        if (tb_n && (!with_tape || (uint64_t)tb_base + (uint64_t)tb_n * TAPE_BLOCK * 2 > room)) { tb_n = 0; tape_left_out = true; }
        if (tb_n) {
            struct tape_blocks_hdr h; memset(&h, 0, sizeof h);
            for (int t = 0; t < TAPE_TRACKS; t++) h.used[t] = tape.tr[t].used;
            h.base = tb_base; h.count = (uint16_t)tb_n;
            memcpy(p, "TAPB", 4); uint32_t l = (uint32_t)(sizeof h + (size_t)tb_n * 2); memcpy(p + 4, &l, 4);
            memcpy(p + 8, &h, sizeof h); memcpy(p + 8 + sizeof h, tb_list, (size_t)tb_n * 2);
            p += 8 + l;
        }
    }
    p = put(p, "END ", "", 0);
    return (size_t)(p - out);
}

/* Everything read back is clamped into range: a slot on a stick can hold anything. */
bool project_load(const uint8_t *in, size_t len) {
    if (len < 9 || memcmp(in, "HBPJ", 4) != 0) return false;
    int version = in[8] >= '1' && in[8] <= '9' ? in[8] - '0' : 1;
    uint32_t st = plat_irq_save();
    seq_play(false);
    rhythm_play(false);
    synth_all_off();
    sampler_stop();
    for (int i = 0; i < SAMPLE_SLOTS; i++) sampler_clear(i);     /* a project brings its own samples, or none */
    n_media = 0; tb_n = 0; loading = false;
    tape_clear_all();                                             /* and its own tape */
    if (ans.plate) ans_clear();                                   /* and its own plate */
    gendy_init();                                                 /* its GENDY patches, or the defaults */
    sieve_init();                                                 /* its sieves, or the defaults */
    cloud_defaults();                                             /* its clouds, or the defaults (all off) */
    upic.playing = false; upic_clear(); upic_arcs_in = -1;        /* its UPIC page, or an empty one */
    omni_off(0); omni.voice = 0; omni.sub_level = 64; omni.sustain = 60; omni.sync = omni.keyboard = false;   /* the omnichord as 2.5 had it */
    omni.kb_octave = 0; omni_set_transpose(0); omni_set_tune(0); rhythm.classic = false; harmony_on = false;
    meta.playing = false; meta_defaults();                        /* METASTASEIS's families, or the defaults */
    carlos_all_off(); carlos_init(); junk_defaults(); drone_defaults(); phase_defaults();   /* the homages' settings, or theirs */
    const uint8_t *p = in, *end = in + len;
    while (p + 8 <= end) {
        uint32_t clen; memcpy(&clen, p + 4, 4);
        const uint8_t *d = p + 8;
        if (clen > (size_t)(end - d)) break;
        if (memcmp(p, "OMNI", 4) == 0 && clen >= sizeof(struct omni_blob)) {
            const struct omni_blob *ob = (const void *)d;
            static const uint8_t suffix_of[3] = { SUF_MAJ, SUF_MIN, SUF_7 };
            omni.suffix = suffix_of[ob->quality % 3]; omni.root = (uint8_t)new_root(ob->root); omni.pad_preset = ob->pad % P_COUNT; omni.strum_preset = ob->strum % P_COUNT;
            omni.octave = (int8_t)CLAMP(ob->octave, -2, 2); audio_set_echo(ob->echo); synth_draw_slot = ob->draw_slot % WAVE_SLOTS;
            omni.hold = ob->hold != 0;                                  /* the switch; nothing sounds until a button */
        } else if (memcmp(p, "OMN2", 4) == 0 && clen >= sizeof(struct omn2_blob)) {
            const struct omn2_blob *o = (const void *)d;
            omni.root = o->root % OMNI_ROOTS; omni.suffix = o->suffix % OMNI_SUFFIXES; omni_set_voice(o->voice % (OMNI_VOICES + 1));
            omni.main_level = (uint8_t)MIN(o->main, 127); omni.sub_level = (uint8_t)MIN(o->sub, 127); omni.sustain = (uint8_t)MIN(o->sustain, 127);
            omni.pad_level = (uint8_t)MIN(o->pad_level, 127);
            omni.autoplay = o->flags & OF_AUTO; omni.hold = o->flags & OF_HOLD; omni.sync = o->flags & OF_SYNC; omni.keyboard = o->flags & OF_KEYBOARD;
            omni.octave = (int8_t)CLAMP(o->octave, -2, 2); omni.kb_octave = (int8_t)CLAMP(o->kb_octave, -1, 1);
            omni_set_transpose(o->transpose); omni_set_tune(o->tune);
            omni.pad_preset = o->pad % P_COUNT; omni.strum_preset = o->strum % P_COUNT;
        } else if (memcmp(p, "SEQ ", 4) == 0 && clen >= sizeof(struct seq_blob)) {                  /* a 1.0 project */
            const struct seq_blob *sb = (const void *)d;
            char title[24]; memcpy(title, sb->title, sizeof title); title[23] = 0;
            for (int t = 0; t < 8; t++) {
                char name[8]; memcpy(name, sb->tr[t].name, 8); name[7] = 0;
                seq_import_steps(title, sb->bpm ? (uint16_t)CLAMP(sb->bpm, 40, 300) : 120, t, name, sb->tr[t].preset % P_COUNT,
                                 sb->tr[t].gate ? (uint8_t)MIN(sb->tr[t].gate, 100) : 60, sb->tr[t].mute, sb->tr[t].note, sb->tr[t].vel);
            }
            seq.pos = -1;
        } else if (memcmp(p, "SONG", 4) == 0 && clen >= sizeof(struct song_blob)) {
            const struct song_blob *sg = (const void *)d;
            memset(seq_pat, 0, sizeof seq_pat);
            for (int n = 0; n < SEQ_PATTERNS; n++) seq_pat[n].rows = SEQ_ROWS;
            memcpy(seq.title, sg->title, sizeof seq.title); seq.title[sizeof seq.title - 1] = 0;
            seq.bpm = (uint16_t)CLAMP(sg->bpm, 32, 300); seq.lpb = (uint8_t)CLAMP(sg->lpb, 1, 16);
            seq.song_len = (uint8_t)CLAMP(sg->song_len, 1, SEQ_ORDER);
            for (int i = 0; i < SEQ_ORDER; i++) seq.order[i] = sg->order[i] % SEQ_PATTERNS;
            for (int c = 0; c < SEQ_TRACKS; c++) {
                memcpy(seq.ch[c].name, sg->ch[c].name, 8); seq.ch[c].name[7] = 0;
                seq.ch[c].inst = (uint8_t)CLAMP(sg->ch[c].inst, 1, P_COUNT); seq.ch[c].mute = sg->ch[c].mute != 0;
            }
            seq.ord = 0; seq.pos = -1; seq.edit_pat = seq.order[0];
        } else if (memcmp(p, "PATN", 4) == 0 && clen >= 2 + sizeof seq_pat[0].cell) {
            struct seq_pattern *pt = &seq_pat[d[0] % SEQ_PATTERNS];
            pt->rows = (uint8_t)CLAMP(d[1], 1, SEQ_ROWS);
            memcpy(pt->cell, d + 2, sizeof pt->cell);
            for (int r = 0; r < SEQ_ROWS; r++) for (int c = 0; c < SEQ_TRACKS; c++) {    /* anything else in a cell is noise */
                struct seq_cell *x = &pt->cell[r][c];
                if (x->note > 127 && x->note != NOTE_OFF) x->note = NOTE_NONE;
                if (x->inst > P_COUNT) x->inst = INST_NONE;
                if (x->vol > 128) x->vol = VOL_NONE;
                x->fx &= 15;
            }
        } else if (memcmp(p, "WAVE", 4) == 0 && clen == sizeof wave_bank) {
            memcpy(wave_bank, d, sizeof wave_bank);
        } else if (memcmp(p, "STRC", 4) == 0 && clen >= sizeof(struct stretch_blob)) {
            const struct stretch_blob *sb = (const void *)d;
            stretch.factor = sb->factor >= 1 && sb->factor <= 1024 ? sb->factor : 16;
            stretch.win = (sb->win == 1024 || sb->win == 2048 || sb->win == 4096 || sb->win == 8192) ? sb->win : 4096;   /* the FFT size */
            stretch.mix = (uint8_t)MIN(sb->mix, 100); stretch.stay = sb->stay;
        } else if (memcmp(p, "INS1", 4) == 0) {                         /* knobs for the instruments of those names */
            for (uint32_t off = 0; off + sizeof(struct inst_blob) <= clen; off += sizeof(struct inst_blob)) {
                struct inst_blob b; memcpy(&b, d + off, sizeof b); b.name[11] = 0;
                for (int i = 0; i < inst_count; i++) if (!memcmp(insts[i].name, b.name, sizeof b.name)) {
                    for (int k = 0; k < IK_COUNT; k++) inst_knob_set(&insts[i], k, b.v[k]);
                    inst_apply(i);
                }
            }
        } else if (memcmp(p, "UPC1", 4) == 0 && clen >= sizeof(struct upic_blob)) {
            const struct upic_blob *ub = (const void *)d;
            upic.bars = (uint8_t)MIN(ub->bars, 32); upic.seconds = (uint8_t)CLAMP(ub->seconds, 1, 120);
            if (ub->offset != NO_FRAMES && ub->bytes == sizeof upic.d && n_media < SAMPLE_SLOTS + 2) {
                media[n_media++] = (struct project_media){ &upic.d, ub->bytes, ub->offset };
                upic_arcs_in = ub->narcs; upic_pts_in = ub->npts;          /* made the page by project_loaded */
            }
        } else if (memcmp(p, "CLD1", 4) == 0 && clen >= sizeof(struct cloud)) {
            for (int i = 0; i < CLOUDS && (i + 1) * sizeof(struct cloud) <= clen; i++) {
                memcpy(&clouds[i], d + i * sizeof(struct cloud), sizeof(struct cloud));
                cloud_sanitize(&clouds[i]);
            }
        } else if (memcmp(p, "SIEV", 4) == 0 && clen >= sizeof(struct sieve_blob)) {
            const struct sieve_blob *sb = (const void *)d;
            for (int i = 0; i < SIEVES && (i + 1) * sizeof *sb <= clen; i++) {
                struct sieve *s = &sieves[i];
                memcpy(s->text, sb[i].text, SIEVE_TEXT); s->text[SIEVE_TEXT - 1] = 0;
                s->unit = sb[i].unit == 2 || sb[i].unit == 3 || sb[i].unit == 6 ? sb[i].unit : 1;
                if (!sieve_compile(s)) s->ok = false;                   /* a sieve it can't read stays silent */
            }
        } else if (memcmp(p, "GDY1", 4) == 0 && clen == sizeof gendy_bank) {
            memcpy(gendy_bank, d, sizeof gendy_bank);
            for (int i = 0; i < GENDY_PATCHES; i++) gendy_patch_sanitize(&gendy_bank[i]);
        } else if (memcmp(p, "FMB2", 4) == 0 && clen == sizeof fm_bank) {
            memcpy(fm_bank, d, sizeof fm_bank);
            for (int i = 0; i < FM_PATCHES; i++) fm_patch_sanitize(&fm_bank[i]);
        } else if (memcmp(p, "FMBK", 4) == 0 && clen == sizeof(struct fm_patch_v1) * FM_PATCHES) {        /* 1.0 */
            const struct fm_patch_v1 *old = (const void *)d;
            for (int i = 0; i < FM_PATCHES; i++) {
                struct fm_patch *np = &fm_bank[i];
                struct fm_patch_v1 o; memcpy(&o, &old[i], sizeof o);
                memset(np, 0, sizeof *np);
                memcpy(np->name, o.name, sizeof np->name); np->algo = o.algo; np->feedback = o.feedback; np->lfo_rate = 30;
                for (int k = 0; k < FM_OPS; k++)
                    np->op[k] = (struct fm_op){ o.op[k].wave, o.op[k].ratio_x2, o.op[k].fine, o.op[k].level, o.op[k].a_ms, o.op[k].d_ms, o.op[k].r_ms, o.op[k].s_pct, 0, 0 };
                fm_patch_sanitize(np);
                if (version < 2 && np->algo == 2) np->op[2].level = 0;   /* TWO PAIR's op 3 was silent then */
            }
        } else if (memcmp(p, "TAP8", 4) == 0 && clen >= sizeof(struct tape8_blob)) {
            const struct tape8_blob *tb = (const void *)d;
            for (int t = 0; t < TAPE_TRACKS; t++) {
                struct tape_track *tr = &tape.tr[t];
                tr->level = (uint8_t)MIN(tb->level[t], 100); tr->low = (int8_t)CLAMP(tb->low[t], -12, 12); tr->high = (int8_t)CLAMP(tb->high[t], -12, 12);
                tr->pan = (int8_t)CLAMP(tb->pan[t], -100, 100); tr->mute = tb->mute[t] != 0; tr->solo = tb->solo[t] != 0; tr->source = tb->source[t] ? TAPE_SRC_IN : TAPE_SRC_MIX;
            }
            tape.speed_q12 = (uint16_t)CLAMP(tb->speed, 2048, 8192); tape.wow = (uint8_t)MIN(tb->wow, 100); tape.hiss = tb->hiss; tape.loop = tb->loop; tape.bounce = tb->bounce;
            tape.loop_in = MIN(tb->loop_in, tape.len); tape.loop_out = MIN(tb->loop_out, tape.len);
        } else if (memcmp(p, "TAPE", 4) == 0 && clen >= sizeof(struct tape_blob)) {          /* a 1.0 project: the first 4 tracks */
            const struct tape_blob *tb = (const void *)d;
            for (int t = 0; t < 4; t++) {
                tape.tr[t].level = tb->level[t] > 100 ? 100 : tb->level[t];
                tape.tr[t].low = CLAMP(tb->low[t], -12, 12); tape.tr[t].high = CLAMP(tb->high[t], -12, 12); tape.tr[t].mute = tb->mute[t];
            }
            tape.speed_q12 = (uint16_t)CLAMP(tb->speed, 2048, 8192); tape.wow = tb->wow > 100 ? 100 : tb->wow; tape.hiss = tb->hiss; tape.loop = tb->loop; tape.bounce = tb->bounce;
        } else if (memcmp(p, "RHYT", 4) == 0 && clen >= sizeof(struct rhythm_blob)) {
            const struct rhythm_blob *rb = (const void *)d;
            rhythm.pattern = (uint8_t)(rb->pattern % 9); rhythm.next = 0xFF; omni.autoplay = rb->bass != 0; rhythm.level = (uint8_t)MIN(rb->level, 127);
            rhythm.mute = rb->mute & 15; omni.pad_level = (uint8_t)MIN(rb->chord_level, 127); omni.main_level = (uint8_t)MIN(rb->strings_level, 127);
        } else if (memcmp(p, "MTS1", 4) == 0 && clen >= sizeof(struct mts1_blob)) {
            const struct mts1_blob *mb1 = (const void *)d;
            memcpy(meta.fam, mb1->fam, sizeof meta.fam);
            for (int f = 0; f < META_FAMILIES; f++) {                  /* a slot can hold anything */
                struct meta_family *F = &meta.fam[f];
                F->n = (uint8_t)CLAMP(F->n, 2, META_MAX); F->section %= META_SECTIONS; F->level = (uint8_t)MIN(F->level, 100);
                F->a.p0 = (uint16_t)CLAMP(F->a.p0, UPIC_LO, UPIC_HI); F->a.p1 = (uint16_t)CLAMP(F->a.p1, UPIC_LO, UPIC_HI);
                F->b.p0 = (uint16_t)CLAMP(F->b.p0, UPIC_LO, UPIC_HI); F->b.p1 = (uint16_t)CLAMP(F->b.p1, UPIC_LO, UPIC_HI);
            }
            meta.bars = (uint8_t)MIN(mb1->bars, 32); meta.seconds = (uint8_t)CLAMP(mb1->seconds, 1, 240); if (!meta.bars && !meta.seconds) meta.seconds = 20;
            meta_compile();
        } else if (memcmp(p, "CRL1", 4) == 0 && clen >= sizeof(struct crl1_blob)) {
            const struct crl1_blob *c = (const void *)d;
            carlos.scale = c->scale % CARLOS_SCALES; carlos.octave = (int8_t)CLAMP(c->octave, 1, 6); carlos.mono = c->mono != 0;
            carlos.glide = (uint8_t)MIN(c->glide, 100); carlos.wave = c->wave % 3; carlos.cutoff = (uint8_t)MIN(c->cutoff, 127);
            carlos.reso = (uint8_t)MIN(c->reso, 100); carlos.contour = (uint8_t)MIN(c->contour, 60); carlos.attack = (uint8_t)MIN(c->attack, 100);
            carlos.decay = (uint8_t)MIN(c->decay, 100); carlos.sustain = (uint8_t)MIN(c->sustain, 100); carlos.release = (uint8_t)MIN(c->release, 100);
            carlos.level = (uint8_t)MIN(c->level, 100); carlos.ribbon_steps = c->ribbon_steps != 0;
            carlos_apply();
        } else if (memcmp(p, "MRZ1", 4) == 0 && clen >= sizeof(struct mrz1_blob)) {
            const struct mrz1_blob *z = (const void *)d;
            junk.drive = (uint8_t)MIN(z->drive, 100); junk.bits = (uint8_t)CLAMP(z->bits, 1, 16); junk.chop = (uint8_t)MIN(z->chop, 100);
            junk.feedback = (uint8_t)MIN(z->feedback, 100); junk.grain = (uint8_t)MIN(z->grain, 100); junk.bytes_rate = (uint8_t)MIN(z->bytes_rate, 100);
            junk.level = (uint8_t)MIN(z->level, 100);
        } else if (memcmp(p, "RDG1", 4) == 0 && clen >= sizeof(struct rdg1_blob)) {
            const struct rdg1_blob *g = (const void *)d;
            for (int i = 0; i < DRONE_PARTIALS; i++) {                 /* a slot can hold anything */
                struct drone_partial q; memcpy(&q, &g->p[i], sizeof q);
                radigue.p[i].harmonic = (uint8_t)CLAMP(q.harmonic, 1, 16); radigue.p[i].detune = (int16_t)CLAMP(q.detune, -200, 200);
                radigue.p[i].level = (uint8_t)MIN(q.level, 100); radigue.p[i].breath_s = (uint16_t)CLAMP(q.breath_s, 30, 600);
            }
            int32_t t; memcpy(&t, &g->target, 4); uint16_t sw; memcpy(&sw, &g->sweep_s, 2);
            radigue.sweep_s = (uint16_t)MIN(sw, 1800); radigue.fade_s = (uint8_t)CLAMP(g->fade_s, 1, 60); radigue.depth = (uint8_t)MIN(g->depth, 100);
            radigue.level = (uint8_t)MIN(g->level, 100);
            uint16_t keep = radigue.sweep_s; radigue.sweep_s = 0; drone_sweep_to(CLAMP(t, 12000, 96000)); radigue.sweep_s = keep;   /* there at once */
        } else if (memcmp(p, "RCH1", 4) == 0 && clen >= sizeof(struct rch1_blob)) {
            const struct rch1_blob *c = (const void *)d;
            reich.mode = c->mode % PHASE_MODES; reich.players = (uint8_t)CLAMP(c->players, 2, PHASE_PLAYERS); reich.len = (uint8_t)CLAMP(c->len, 2, PHASE_STEPS);
            for (int i = 0; i < PHASE_STEPS; i++) reich.notes[i] = c->notes[i] ? (uint8_t)CLAMP(c->notes[i], 24, 108) : 0;
            reich.per_beat = (uint8_t)CLAMP(c->per_beat, 2, 4); reich.hold = (uint8_t)CLAMP(c->hold, 1, 32); reich.move = (uint8_t)CLAMP(c->move, 1, 16);
            reich.drift = (uint8_t)CLAMP(c->drift, 1, 50); reich.sound = c->sound % PHASE_SOUNDS; reich.slot = c->slot % SAMPLE_SLOTS; reich.level = (uint8_t)MIN(c->level, 100);
        } else if (memcmp(p, "HRM1", 4) == 0 && clen >= 1) {
            harmony_on = d[0] != 0;
        } else if (memcmp(p, "RHY2", 4) == 0 && clen >= sizeof(struct rhy2_blob)) {
            const struct rhy2_blob *r = (const void *)d;
            rhythm.pattern = r->pattern % RHYTHM_PATTERNS; rhythm.next = 0xFF; rhythm.mute = r->mute & 63; rhythm.classic = r->classic != 0;
            rhythm.level = (uint8_t)MIN(r->level, 127);
        } else if (memcmp(p, "TAPB", 4) == 0 && clen >= sizeof(struct tape_blocks_hdr)) {
            const struct tape_blocks_hdr *h = (const void *)d;
            int n = MIN((int)h->count, (int)((clen - sizeof *h) / 2));
            tb_n = 0; tb_base = h->base; loading = true;
            for (int t = 0; t < TAPE_TRACKS; t++) tb_used[t] = MIN(h->used[t], tape.len);
            for (int k = 0; k < n && tb_n < (int)ARRAY_LEN(tb_list); k++) {
                uint16_t v; memcpy(&v, d + sizeof *h + 2 * k, 2);
                if ((v >> 12) < TAPE_TRACKS && (v & 0xFFF) < TAPE_SPANS) tb_list[tb_n++] = v;
            }
        } else if (memcmp(p, "MIXR", 4) == 0 && clen >= sizeof(struct mix_blob)) {
            const struct mix_blob *mb = (const void *)d;
            for (int c = 0; c < MIX_CHANNELS_V1; c++) {
                struct mix_channel *ch = &mix.ch[c];
                ch->db = (int8_t)(mb->db[c] < MIX_DB_MIN ? MIX_DB_OFF : MIN(mb->db[c], MIX_DB_MAX));
                ch->pan = (int8_t)CLAMP(mb->pan[c], -100, 100); ch->mute = mb->mute[c] != 0; ch->solo = mb->solo[c] != 0; ch->echo = (uint8_t)MIN(mb->echo[c], 100);
            }
            mix.in_mono = mb->in_mono != 0;
        } else if (memcmp(p, "MIX2", 4) == 0 && clen >= sizeof(struct mix2_head)) {
            struct mix2_head mh; memcpy(&mh, d, sizeof mh);
            int nc = MIN((int)mh.channels, (int)((clen - sizeof mh) / sizeof(struct mix2_ch)));
            for (int c = 0; c < MIN(nc, MIX_CHANNELS); c++) {
                struct mix2_ch mc; memcpy(&mc, d + sizeof mh + c * sizeof mc, sizeof mc);
                struct mix_channel *ch = &mix.ch[c];
                ch->db = (int8_t)(mc.db < MIX_DB_MIN ? MIX_DB_OFF : MIN(mc.db, MIX_DB_MAX));
                ch->pan = (int8_t)CLAMP(mc.pan, -100, 100); ch->mute = mc.mute != 0; ch->solo = mc.solo != 0;
                ch->echo = (uint8_t)MIN(mc.echo, 100); ch->reverb = (uint8_t)MIN(mc.reverb, 100);
            }
            mix.in_mono = mh.in_mono != 0;
        } else if (memcmp(p, "TUCH", 4) == 0 && clen >= sizeof(struct touch_blob)) {
            const struct touch_blob *tb3 = (const void *)d;
            for (int k = 0; k < TOUCH_KNOBS; k++) touch.knob[k] = (uint8_t)MIN(tb3->knob[k], 100);
            touch.hum60 = tb3->hum60 != 0;
        } else if (memcmp(p, "MFX1", 4) == 0 && clen >= sizeof(struct fx_blob)) {
            const struct fx_blob *fb = (const void *)d;
            fx.rev_size = (uint8_t)MIN(fb->rev_size, 100); fx.rev_damp = (uint8_t)MIN(fb->rev_damp, 100); fx.rev_level = (uint8_t)MIN(fb->rev_level, 100);
            fx.echo_div = (uint8_t)(fb->echo_div % FX_ECHO_DIVS); fx.echo_feedback = (uint8_t)MIN(fb->echo_feedback, 90);
            fx.filter_on = fb->filter_on != 0; fx.filter_mode = (uint8_t)(fb->filter_mode % 3); fx.filter_cut = (uint8_t)MIN(fb->filter_cut, 127);
            fx.filter_res = (uint8_t)MIN(fb->filter_res, 100); fx.drive_on = fb->drive_on != 0; fx.drive = (uint8_t)MIN(fb->drive, 100);
            fx.crush_on = fb->crush_on != 0; fx.crush_bits = (uint8_t)CLAMP(fb->crush_bits, 1, 16); fx.crush_rate = (uint8_t)CLAMP(fb->crush_rate, 1, 32);
            for (int c = 0; c < MIX_CHANNELS_V1; c++) mix.ch[c].reverb = (uint8_t)MIN(fb->reverb[c], 100);
        } else if (memcmp(p, "SMPL", 4) == 0 && clen >= sizeof(struct sample_blob)) {
            const struct sample_blob *sb = (const void *)d;
            struct sample *s = &samples[sb->slot % SAMPLE_SLOTS];
            if (!s->data) { p = d + clen; continue; }
            memcpy(s->name, sb->name, sizeof s->name); s->name[sizeof s->name - 1] = 0;
            s->bits = sb->bits == 8 ? 8 : 16; s->rate_i = (uint8_t)MIN(sb->rate_i, SAMPLE_RATES - 1); s->rate = sampler_rate_hz(s->rate_i);
            s->loop = sb->loop != 0; s->oneshot = sb->oneshot != 0; s->root = (uint8_t)CLAMP(sb->root, 12, 120); s->level = (uint8_t)MIN(sb->level, 100);
            s->attack_ms = (uint16_t)CLAMP(sb->attack, 1, 2000); s->release_ms = (uint16_t)CLAMP(sb->release, 5, 4000);
            /* the frames come after the blob; a machine with less sample memory keeps the beginning */
            uint32_t bytes = MIN(sb->bytes, sampler.slot_bytes), per = s->bits / 8u;
            s->len = sb->offset == NO_FRAMES ? 0 : MIN(sb->len, bytes / per);
            s->end = CLAMP(sb->end, 1, s->len); s->start = MIN(sb->start, s->end - 1); s->loop_start = CLAMP(sb->loop_start, s->start, s->end - 1);
            if (!s->len) s->start = s->end = s->loop_start = 0;
            if (s->len && n_media < SAMPLE_SLOTS) media[n_media++] = (struct project_media){ s->data, s->len * per, sb->offset };
            s->gen++;
        } else if (memcmp(p, "ANS1", 4) == 0 && clen >= sizeof(struct ans_blob)) {
            const struct ans_blob *ab = (const void *)d;
            ans.bars = (uint8_t)MIN(ab->bars, 32); ans.seconds = (uint8_t)CLAMP(ab->seconds, 1, 240); ans.octave = (uint8_t)CLAMP(ab->octave, 1, 4);
            ans.level = (uint8_t)MIN(ab->level, 100); ans.look = ab->look ? ANS_EDGES : ANS_LIGHT; ans.thresh = (uint8_t)MIN(ab->thresh, 250);
            if (ab->offset != NO_FRAMES && ab->bytes == ANS_ROWS * ANS_COLS && ans.plate && n_media < SAMPLE_SLOTS + 2)
                media[n_media++] = (struct project_media){ ans.plate, ab->bytes, ab->offset };
        } else if (memcmp(p, "END ", 4) == 0) break;
        p = d + clen;
    }
    plat_irq_restore(st);
    return true;
}
