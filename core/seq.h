#pragma once
/* The tracker. A song is an order list of patterns; a pattern is up to 64 rows × 8 channels, and each cell holds a
   note, an instrument (any of the sounds, samples included), a volume and an effect. A channel plays one note at a
   time and holds it until the next note or a note-off. Effects work on 16 ticks a row. It runs inside the audio render
   (core/audio.c), counting frames: rows and ticks land on exact samples and the tempo never drifts.

   Effects (hex, like the old trackers):   0xy arpeggio  1xx slide up  2xx slide down  3xx glide to the note
   4xy vibrato  8xx pan  Axy volume up x / down y  Bxx jump to order xx  Cxx volume  Dxx next pattern at row xx
   ECx cut after x ticks  EDx delay x ticks  E9x retrigger every x ticks  Fxx tempo (xx >= 20) or rows a beat */
#include <stdint.h>
#include <stdbool.h>

#define SEQ_TRACKS   8                  /* channels */
#define SEQ_ROWS     64                 /* the longest pattern */
#define SEQ_PATTERNS 32
#define SEQ_ORDER    128
#define SEQ_TICKS    16                 /* effect steps a row */
#define SEQ_STEPS    SEQ_ROWS           /* (the 1.0 sequencer's name) */

#define NOTE_NONE 0
#define NOTE_OFF  0xFE                  /* "===": the channel's note is let go */
#define INST_NONE 0                     /* else preset + 1 */
#define VOL_NONE  0                     /* else volume + 1 (0..127): an empty cell is all zeros */

struct seq_cell { uint8_t note, inst, vol, fx, param; };      /* fx 0 with param 0: no effect */
struct seq_pattern { uint8_t rows; struct seq_cell cell[SEQ_ROWS][SEQ_TRACKS]; };
struct seq_channel { char name[8]; uint8_t inst; bool mute; };  /* inst: the sound a note plays when its cell has none */

struct seq_state {
    char     title[24];
    uint16_t bpm;
    uint8_t  lpb;                       /* rows a beat */
    uint8_t  song_len;                  /* entries in the order list */
    uint8_t  order[SEQ_ORDER];
    struct seq_channel ch[SEQ_TRACKS];
    bool     playing, pattern_only;     /* pattern_only: loop the pattern at the edit position */
    uint8_t  ord;                       /* the order entry playing (or being edited) */
    int16_t  pos;                       /* the row playing, -1 before the first */
    uint8_t  edit_pat;                  /* the pattern on the page */
};
extern struct seq_state seq;
extern struct seq_pattern seq_pat[SEQ_PATTERNS];

void seq_init(void);
void seq_load_demo(int n);              /* 0.. — see seq_demo_count() */
void seq_begin(const char *title, uint16_t bpm);   /* stopped, every pattern empty (64 rows), one entry in the order */
int  seq_demo_count(void);
const char *seq_demo_title(int n);
void seq_play(bool on);
void seq_play_pattern(bool on);         /* loop the edit pattern */
void seq_audition(int ch, uint8_t note, uint8_t inst);   /* a note on a channel's sound, for a moment (editing) */
void seq_mute(int ch, bool mute);
int  seq_pattern_used(int p);           /* cells with something in them */
bool seq_channel_sounding(int ch);      /* a note is playing on it (for the page's lights) */
/* a 1.0 project's sequencer: 8 tracks × 64 steps with ties and a gate, into pattern 0 */
void seq_import_steps(const char *title, uint16_t bpm, int t, const char *name, uint8_t preset, uint8_t gate, bool mute,
                      const uint8_t *note, const uint8_t *vel);
/* audio side (interrupt context): */
void     seq_init_rate(uint32_t rate);
void     seq_run_events(void);          /* rows and ticks due at the current frame */
uint32_t seq_next_event(void);          /* frames until the next one (>= 1 after seq_run_events) */
/* Link: its exact tempo; a start the given frames from now; where the rows are (Q16, since the start); a nudge moving
   the next row later (+) or earlier (−) by Q16 frames */
extern uint32_t seq_link_q16;
void     seq_start_in(uint32_t frames, bool pattern_only);
void     seq_stop_quietly(void);         /* stopped by the session: not told back */
int64_t  seq_rows_q16(void);
uint32_t seq_row_q16(void);
void     seq_nudge(int32_t q16);
uint32_t seq_step_left_q16(void);       /* until the next row, Q16 frames (for the rhythm section to fall in) */
void     seq_advance(uint32_t frames);
