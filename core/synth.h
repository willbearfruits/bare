#pragma once
/* Polyphonic voices. Fixed-point, rendered in blocks of up to SYNTH_BLOCK frames: envelopes, pitch and filter move
   once per block (a 1.5 kHz control rate), amplitude and pan are ramped per sample, and each waveform has its own
   inner loop. The graph around the voices (sequencer, stretcher, tape, echo, master) is core/audio.c. */
#include <stdint.h>
#include <stdbool.h>

#define SYNTH_MAX_VOICES 24
#define SYNTH_BLOCK      32

enum wave { WAVE_PULSE, WAVE_SAW, WAVE_TRI, WAVE_SINE, WAVE_NOISE, WAVE_TABLE, WAVE_FM, WAVE_SAMPLE, WAVE_GENDY };
enum wave_src { SRC_NONE = 0, SRC_SLOT, SRC_MORPH, SRC_SCAN, SRC_ROM };

struct preset {
    const char *name;
    uint8_t  wave;
    uint8_t  duty;                     /* pulse width % */
    uint16_t attack_ms, decay_ms, release_ms;
    uint8_t  sustain;                  /* % */
    uint8_t  volume;                   /* 0..100 */
    uint8_t  cutoff;                   /* 0..127 filter index, 127 = bypass */
    uint8_t  reso;                     /* 0..100 */
    uint8_t  fenv;                     /* filter envelope amount (cutoff steps) */
    uint8_t  penv;                     /* pitch envelope amount (drums) */
    uint16_t pdecay_ms;
    uint8_t  vib;                      /* vibrato depth 0..100 */
    uint8_t  detune;                   /* second oscillator detune, 0 = single osc */
    bool     oneshot;                  /* ignores note-off; retriggers on same tag */
    uint8_t  src;                      /* WAVE_TABLE source; WAVE_FM, WAVE_GENDY: patch number; WAVE_SAMPLE: slot */
};

enum { P_ORGAN = 0, P_PLUCK, P_SAWBASS, P_FATSAW, P_SQLEAD, P_CHIP, P_BELL, P_GRIND, P_KICK, P_SNARE, P_HAT,
       P_DRAWN, P_MORPH, P_SCAN, P_ROM, P_FM1, P_FM2, P_FM3, P_FM4,
       P_SMP1, P_SMP2, P_SMP3, P_SMP4, P_SMP5, P_SMP6, P_SMP7, P_SMP8, P_GD1, P_GD2, P_GD3, P_GD4,
       P_INST1, P_INST2, P_INST3, P_INST4, P_INST5, P_INST6, P_INST7, P_INST8,
       P_COUNT };   /* new ones go last: projects keep indices */
/* the instruments' sounds (core/inst.c): a preset each, with its own envelope, level and filter whatever its wave;
   p = 0 empties the slot (then it is skipped) */
void synth_user_preset(int i, const struct preset *p);
extern int synth_draw_slot;        /* which wave slot DRAWN / MORPH start from */
const struct preset *synth_preset(int id);
const char *synth_preset_name(int id);       /* FM and GENDY presets show their patch name, samples their slot's */
int synth_preset_next(int id, int d);        /* one along (d = ±1), past the sample slots that hold nothing */

void     synth_init(uint32_t sample_rate);
uint32_t synth_rate(void);
/* Start a note. Voices carry a 16-bit tag so groups can be released together. pan: -100 left .. 100 right. */
void     synth_note_on(uint8_t note, uint8_t vel, uint8_t preset, uint16_t tag);
void     synth_note_on_pan(uint8_t note, uint8_t vel, uint8_t preset, uint16_t tag, int pan);
void     synth_note_off_tag(uint16_t tag);
void     synth_all_off(void);
void     synth_kill_preset(int preset);     /* silence its voices at once (the sampler, before rewriting a slot) */
/* change the sounding voices of a tag (the tracker's effects): pitch in 1/256 semitones from the note, velocity, pan */
void     synth_tag_bend(uint16_t tag, int32_t semis_q8);
void     synth_tag_velocity(uint16_t tag, uint8_t vel);
void     synth_tag_level(uint16_t tag, int32_t level_q15);   /* the same in finer steps: 32767 is velocity 127 */
void     synth_tag_pan(uint16_t tag, int pan);
uint32_t synth_bend_mul(int32_t semis_q8);   /* 2^(s/12) in Q16, s in 1/256 semitones (±96 semitones) */
int      synth_sample_heads(int slot, uint32_t *pos, int max);   /* where the voices playing a sample slot are */
/* Voices sound on a bus by their tag: the sequencer's (0x3xx) on SEQ, the rhythm section's (0x4xx) on RHYTHM, the
   clouds' notes (0x8xx) on CLOUDS, UPIC's arcs (0x9xx) on UPIC, Doom's music (0xBxxx) on DOOM, the rest on PLAY — each a
   mixer channel (audio.c). */
enum { BUS_PLAY, BUS_SEQ, BUS_RHYTHM, BUS_UPIC, BUS_CLOUD, BUS_DOOM, SYNTH_BUSES };
/* Render n <= SYNTH_BLOCK frames of every sounding voice into its bus (cleared first); returns a bit per bus that
   has voices (audio interrupt). */
uint32_t synth_render(int32_t (*left)[SYNTH_BLOCK], int32_t (*right)[SYNTH_BLOCK], uint32_t n);

void     synth_set_mod(int cutoff_delta, int detune_q12);   /* global modulation (thermal etc.) */
int      synth_active_voices(void);
/* Best single frequency for a 1-bit/monophonic output (Hz), 0 = silence. */
uint32_t synth_mono_freq(void);
