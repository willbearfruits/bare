#pragma once
/* The tape: 8 mono tracks on a timeline of up to 30 minutes. Audio lives in blocks of 32768 frames taken from one pool
   as a track is recorded, so memory holds as much sound as it can whichever tracks it is on (a 32 MB PC gets a few
   minutes of it all told). Each block keeps a peak per 512 frames for drawing waveforms. Recording takes the mix
   (what you play, the input, the stretcher) or the input alone, per track, and lands where you heard the tape when
   you played: the output's delay is taken off. Portastudio rules otherwise: recording replaces what was there,
   BOUNCE records the other tracks' playback too, and the tape has wow, flutter, hiss, varispeed and saturation. */
#include <stdint.h>
#include <stdbool.h>

#define TAPE_TRACKS      8
#define TAPE_BLOCK_SHIFT 15
#define TAPE_BLOCK       (1u << TAPE_BLOCK_SHIFT)       /* frames in a block */
#define TAPE_SPANS       2640                           /* blocks along the timeline: 30 minutes at 48 kHz */
#define TAPE_PEAK_SHIFT  9                              /* a peak per 512 frames */
#define TAPE_NO_BLOCK    0xFFFF

enum { TAPE_SRC_MIX, TAPE_SRC_IN };

struct tape_track {
    uint16_t *map;                 /* the block under each span of the timeline, TAPE_NO_BLOCK = silence */
    uint32_t used;                 /* frames up to the end of what is recorded */
    uint32_t gen;                  /* bumped as it is recorded or edited, for drawing */
    uint8_t  level;                /* 0..100 */
    int8_t   pan;                  /* -100 .. 100 */
    int8_t   low, high;            /* shelving EQ in dB, -12..+12 (~250 Hz and ~3 kHz shelves) */
    bool     arm, mute, solo;
    uint8_t  source;               /* TAPE_SRC_MIX or TAPE_SRC_IN */
    int32_t  lp_low, lp_high;      /* filter states */
    int32_t  vu;                   /* peak meter, decays */
};

struct tape_state {
    struct tape_track tr[TAPE_TRACKS];
    uint32_t len;                  /* the timeline: TAPE_SPANS blocks; 0 = no tape (no memory for one) */
    uint32_t blocks, blocks_free;  /* the pool */
    uint32_t pos; uint16_t frac;   /* head position: frame + 1/65536 */
    uint32_t used;                 /* extent over all tracks */
    uint32_t loop_in, loop_out;    /* the loop region; out <= in means from the start to the end of what is recorded */
    bool     playing, recording, loop, bounce, hiss, full;   /* full: a recording ran out of blocks */
    uint16_t speed_q12;            /* 4096 = normal; 2048..8192 (half to double speed) */
    uint8_t  wow;                  /* wow & flutter depth 0..100 */
    int32_t  vu_in, vu_out;
    int      cur;                  /* track selected on the TAPE page */
    int      spin;                 /* reel animation: frames of fast spinning after a seek (negative = backwards) */
};
extern struct tape_state tape;

void     tape_init(uint32_t rate, uint32_t pool_bytes);
/* audio interrupt: record the armed tracks from mix (or in), play back into out; false when the tape is still */
bool     tape_process(const int32_t *mix_l, const int32_t *mix_r, const int32_t *in_l, const int32_t *in_r,
                      int32_t *out_l, int32_t *out_r, uint32_t n);
void     tape_work(void);                           /* main loop: clears freed blocks for reuse */
void     tape_seek(int64_t delta_frames);
void     tape_seek_to(uint32_t frame);
bool     tape_erase(int track);                     /* the whole track; false while it is recording */
bool     tape_erase_range(int track, uint32_t from, uint32_t to);
bool     tape_copy(int from_track, uint32_t from, uint32_t to, int to_track, uint32_t at);   /* false: out of tape */
uint32_t tape_rate(void);
uint32_t tape_free_seconds(void);                   /* mono seconds the pool still holds */
int16_t  tape_sample(int track, uint32_t frame);
uint8_t  tape_peak(int track, uint32_t from, uint32_t to);   /* the loudest in [from, to), 0..255 */
uint8_t  tape_peak_coarse(int track, uint32_t from, uint32_t to);   /* the same from the 512-frame peaks only: cheap */
bool     tape_write(int track, uint32_t at, const int16_t *frames, uint32_t n);   /* main loop: frames onto a track */
/* projects: the blocks a track has, and filling them back in */
int      tape_spans(int track, uint16_t *spans, int max);          /* the spans that hold a block */
const int16_t *tape_block(int track, uint32_t span);
int16_t *tape_block_for_load(int track, uint32_t span);           /* a block to read into (its frames all overwritten) */
void     tape_clear_all(void);                                      /* every track empty, the blocks back at once */
void     tape_loaded(int track, uint32_t used);                     /* peaks from the frames, and where it ends */
void     tape_restore_block(int track, uint32_t span, const int16_t *frames, uint32_t used);   /* undo: NULL = no block */
void     tape_take_begin(uint64_t now);                             /* a take starts: its undo keeps the blocks ahead */
