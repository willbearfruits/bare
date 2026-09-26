#pragma once
/* The sampler: eight mono sample slots in memory sized from the machine's RAM. A slot holds 16-bit or 8-bit frames
   (8-bit is the Fairlight's grit and twice the length) at the output rate or a fraction of it. Slots are recorded
   from the instrument's output, the stretcher's frozen sound or the line input, grabbed from the stretcher's last
   10 seconds, and played by the SAMPLE presets (synth.c); a slot can also be handed to the stretcher to freeze. */
#include <stdint.h>
#include <stdbool.h>

#define SAMPLE_SLOTS 8
#define SAMPLE_RATES 5                  /* the output rate × 1, 2/3, 1/2, 1/3, 1/6: 48, 32, 24, 16, 8 kHz at 48 kHz */

enum { SMP_OUT, SMP_FREEZE, SMP_IN, SMP_SOURCES };   /* what a recording takes */
enum { SMP_IDLE, SMP_ARMED, SMP_RECORDING };

struct sample {
    char     name[12];
    uint8_t  bits;                      /* 16 or 8 */
    uint8_t  rate_i;                    /* index into the rates: 0 = the output rate */
    uint32_t rate;                      /* Hz */
    uint32_t len;                       /* frames held */
    uint32_t start, end;                /* what plays: start ≤ … < end */
    uint32_t loop_start;                /* with loop on, the end jumps back here */
    bool     loop, oneshot;             /* oneshot: plays to the end whatever the key does */
    uint8_t  root;                      /* the MIDI note that plays it at its own pitch */
    uint8_t  level;                     /* 0..100 */
    uint16_t attack_ms, release_ms;
    uint32_t gen;                       /* bumped by every change to the frames, for displays */
    volatile bool busy;                 /* being rewritten: voices leave it alone */
    void    *data;                      /* int16_t or int8_t frames */
};

struct sampler_state {
    uint32_t slot_bytes;                /* memory per slot; 0 = no sampler on this machine */
    uint8_t  source, state, slot;       /* the recording: where from, idle/armed/recording, into which slot */
    uint8_t  rec_bits, rec_rate_i;      /* the format new recordings take */
    uint16_t threshold;                 /* an armed recording starts when the source gets this loud */
    int32_t  vu;                        /* the source's level, for the meter */
    uint32_t rec_len;                   /* frames recorded so far */
};
extern struct sample samples[SAMPLE_SLOTS];
extern struct sampler_state sampler;

void     sampler_init(uint32_t out_rate, uint32_t pool_bytes);
uint32_t sampler_rate_hz(int rate_i);
uint32_t sampler_capacity(int slot);    /* frames the slot can hold in its format */
/* audio interrupt: a tap point in the graph (SMP_OUT after the echo, SMP_FREEZE the stretcher, SMP_IN the input) */
void     sampler_tap(int source, const int32_t *l, const int32_t *r, uint32_t n);
bool     sampler_listening(int source); /* the render loop only builds a tap's buffers when someone listens */
void     sampler_work(void);            /* main loop: finishes a recording (trim, normalise) */

void     sampler_record(int slot);      /* arm: waits for sound, then records until full or sampler_stop */
void     sampler_stop(void);
bool     sampler_grab(int slot);        /* the last phrase the stretcher heard (its 10 s ring) into the slot */
bool     sampler_to_stretch(int slot);  /* the stretcher freezes on this sample */
void     sampler_set_format(int slot, int bits, int rate_i);   /* converts what the slot holds */
void     sampler_trim(int slot);        /* keep start..end */
void     sampler_normalize(int slot);
void     sampler_reverse(int slot);
void     sampler_clear(int slot);
void     sampler_copy(int from, int to);
int32_t  sampler_frame(const struct sample *s, uint32_t i);    /* one frame as 16-bit */
/* the smallest and largest frame in [from, to), for drawing */
void     sampler_span(const struct sample *s, uint32_t from, uint32_t to, int32_t *lo, int32_t *hi);
/* filling a slot from elsewhere (a WAV file): frames at the slot's own depth and rate, then a name */
bool     sampler_begin_write(int slot);
bool     sampler_write(int slot, const int16_t *frames, uint32_t n);   /* false once the slot is full */
void     sampler_end_write(int slot, const char *name);
void     sampler_restore(int slot, const struct sample *meta, const void *frames);   /* undo: as it was */
