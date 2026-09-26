#pragma once
/* ANS: a plate of light played by a moving slit, after Evgeny Murzin's photoelectronic synthesizer (1957). The plate
   is ANS_ROWS pure tones, 72 to the octave (a sixth of a semitone apart), from the octave chosen upward, over ANS_COLS
   columns of time; each cell's brightness is that tone's loudness while the slit is over it. The slit crosses the
   plate in a number of bars of the tempo (or of seconds) and starts again. The camera can put a picture on the plate
   (as its light, or its edges), or be the plate while it plays: the room as the score. Keys and MIDI notes held while
   it plays write their row under the slit, so a pass can be recorded into it. Its own mixer channel. */
#include <stdint.h>
#include <stdbool.h>

#define ANS_ROWS 360                             /* five octaves of 72 */
#define ANS_COLS 512
enum { ANS_LIGHT, ANS_EDGES };                    /* how the camera's picture becomes the plate */
enum { ANS_CAM_OFF, ANS_CAM_LIVE };
struct ans_state {
    uint8_t *plate;                              /* column-major: plate[col * ANS_ROWS + row], row 0 the lowest tone */
    volatile bool playing;
    uint8_t bars;                                /* the pass: bars of the tempo, 1..32; 0: seconds */
    uint8_t seconds;                             /* 1..60 */
    uint8_t octave;                              /* the lowest row: C of this octave (1..4) */
    uint8_t level;                               /* 0..100 */
    uint8_t look, thresh;                        /* the camera: light or edges, and the black level 0..250 */
    uint8_t camera;                              /* ANS_CAM_OFF, ANS_CAM_LIVE */
    volatile uint32_t pos_q16;                   /* the slit: columns, Q16 (to look at; ans_seek moves it) */
    volatile int32_t seek;                       /* a column to jump to + 1 (0: none), taken by the next block */
    uint32_t dirty[ANS_COLS / 32];               /* columns changed since the picture last looked (a bit each) */
};
extern struct ans_state ans;

void ans_init(uint32_t rate);                    /* the plate from plat_alloc (184 KB) */
bool ans_render(int32_t *l, int32_t *r, uint32_t n);   /* the audio interrupt: false when silent */
void ans_work(uint64_t now);                     /* the main loop: the tempo, the live camera */
void ans_note(int row, uint8_t vel);             /* a key or note held (vel 0: let go): heard, and written under the slit */
bool ans_snap(void);                             /* the camera's picture laid over the plate (false: none) */
void ans_clear(void);
void ans_mark(int col);                          /* a column changed (drawing) */
uint32_t ans_frames_per_pass(void);
