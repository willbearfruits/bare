#pragma once
/* REICH (a LINEAGE view, after Steve Reich): two to four players loop the same pattern, and one process moves them
   apart and back together.
     PHASE  (after Piano Phase): a player holds for some repeats, then plays a little faster until it is a step ahead
            (player k: k steps), locks there exactly on the grid, holds, moves again … back to unison after as many
            moves as the pattern has steps.
     SHIFT  (after Clapping Music): after some repeats a player jumps a step ahead (k steps), on the grid.
     DRIFT  (after It's Gonna Rain): each player runs a little faster than the one before, and never locks.
     LOOP   the same with a sampler slot as the loop: record a voice on WAVE › sampler, and it drifts against itself.
   Players are event sources on the sample clock (like the rhythm section): steps fall on exact frames, and a locked
   player's steps on exactly the first player's. Voices are tagged 0xC80 | player << 4 | step, on the LINEAGE bus. */
#include <stdint.h>
#include <stdbool.h>

#define PHASE_PLAYERS 4
#define PHASE_STEPS 16
enum { PM_PHASE, PM_SHIFT, PM_DRIFT, PM_LOOP, PHASE_MODES };
extern const char *const phase_mode_names[PHASE_MODES];
#define PHASE_SOUNDS 8
extern const uint8_t phase_sounds[PHASE_SOUNDS];   /* the presets a pattern can be played with */

struct phase_state {
    uint8_t mode;
    uint8_t players;                       /* 2 .. 4 */
    uint8_t len;                           /* 2 .. 16 steps */
    uint8_t notes[PHASE_STEPS];            /* MIDI notes, 0 a rest */
    uint8_t per_beat;                      /* steps a beat: 2, 3 or 4 (the tempo is the sequencer's, or Link's) */
    uint8_t hold;                          /* PHASE, SHIFT: repeats before a player moves, 1 .. 32 */
    uint8_t move;                          /* PHASE: repeats a move takes, 1 .. 16 */
    uint8_t drift;                         /* DRIFT, LOOP: how much faster each player runs, tenths of a percent, 1 .. 50 */
    uint8_t sound;                         /* an index into phase_sounds */
    uint8_t slot;                          /* LOOP: the sampler slot */
    uint8_t level;                         /* 0 .. 100 */
    bool    playing;
};
extern struct phase_state reich;

/* what the picture and the checks read; written by the audio side */
struct phase_player {
    volatile uint32_t steps;               /* steps played since the start */
    volatile uint8_t  pos;                 /* the step it plays next */
    volatile bool     moving;              /* PHASE: pulling ahead */
    volatile uint8_t  count;               /* repeats into the present hold or move */
    volatile uint64_t last_q16;            /* when it last played (frames × 65536 since the start) */
};
extern struct phase_player phase_pl[PHASE_PLAYERS];
#define PHASE_LOG 64
struct phase_hit { uint8_t player, step; uint32_t frame; };
extern struct phase_hit phase_log[PHASE_LOG];     /* the last hits, for the checks */
extern volatile uint32_t phase_log_n;

void     phase_init(uint32_t rate);
void     phase_defaults(void);
void     phase_play(bool on);              /* all players from the first step, together */
void     phase_from_chord(void);           /* a pattern from the omnichord's chord (original figures) */
int32_t  phase_offset_q16(int player);     /* how far ahead of the first player, in steps (Q16), now */
uint32_t phase_step_frames(void);          /* the first player's step, in frames */
/* the audio loop */
void     phase_run_events(void);
uint32_t phase_next_event(void);
void     phase_advance(uint32_t frames);
