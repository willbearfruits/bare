#pragma once
/* TOUCH: a circuit played with the fingers, like a crackle box. Eight bare contact pads lead into a small circuit: an
   op-amp wired as a trigger with its feedback left open, a capacitor behind each of its inputs, its compensation point,
   the supply and ground. A finger on a pad joins it to the body through the skin; pads touched at once are joined
   through the body, so the fingers become the circuit's missing resistors — how hard they press sets the pitch, a light
   touch crackles, and a finger alone brings in the hum the body picks up.
   Simulated at twice the sample rate in fixed point: four nodes with capacitors, the op-amp as a gain with one pole and
   soft rails, and the body as a node without capacitance, solved each step from the currents that meet there. */
#include <stdint.h>
#include <stdbool.h>

#define TOUCH_PADS 8
#define TOUCH_FINGERS 16                      /* the touchpad's five, the mouse, a key per pad, two spare */
enum { TP_OUT, TP_INV, TP_C1, TP_COMP, TP_VCC, TP_NI, TP_C2, TP_GND };   /* the pads: top row, then bottom row */
enum { TK_RANGE, TK_GAIN, TK_CRACKLE, TK_HUM, TK_SKIN, TK_TONE, TK_LEVEL, TOUCH_KNOBS };

struct touch_finger { bool on; uint16_t x, y; uint8_t press, size; };   /* x, y 0..32767 across the board; press 0..255 */
struct touch_state {
    uint8_t knob[TOUCH_KNOBS];                /* 0..100 */
    bool hum60;                               /* mains at 60 Hz, else 50 */
    struct touch_finger f[TOUCH_FINGERS];     /* set by the page (main loop), read by the audio */
    uint16_t glow[TOUCH_PADS];                /* how much current each pad carried lately, 0..32767 (the picture) */
    bool sounding;                            /* the circuit was rendered in the last block */
};
extern struct touch_state touch;
extern const char *const touch_pad_names[TOUCH_PADS];
extern const char *const touch_knob_names[TOUCH_KNOBS];

void touch_init(void);                        /* knobs to their defaults, circuit at rest */
void touch_pad_rect(int pad, int *x0, int *y0, int *x1, int *y1);   /* where a pad is on the board, 0..32767 */
void touch_lift_all(void);
/* audio: n frames (n <= 32) into l and r; false when the circuit is silent and untouched (nothing written) */
bool touch_render(int32_t *l, int32_t *r, uint32_t n);
