#pragma once
#include <stdint.h>
#include <stdbool.h>
bool     hda_init(void);                        /* find controller, bring up first output path, start stream */
uint32_t hda_rate(void);
uint32_t hda_latency(void);                     /* frames from rendering to the DAC: the lead kept in the ring */
/* Call ~every millisecond: keeps the DMA ring topped up using audio_render (or silence). */
void     hda_pump(bool silent);
void     hda_prefill(void);                      /* fill the whole ring before a long interrupt-less stretch */
void     hda_poll_jacks(void);                  /* headphone plugged → speakers muted; call every ~250 ms from the main loop */
void     hda_status(char *out, int cap);        /* controller, codec, DMA position, each output pin's state (main loop) */
void     hda_test_tone(bool on);
void     hda_all_outputs(bool on);              /* test: every output pin on, jack sense ignored */
void     hda_long_lead(void);                   /* keep ~43 ms ahead instead of ~11: the main loop is pumping */
bool     hda_headphones(void);
/* inputs: line in and microphones the codec can reach; one runs at a time, drained by hda_pump into audio_input_push */
int      hda_inputs(const char **names, int max);
bool     hda_input_select(int index);             /* -1 = off */
int      hda_input_active(void);
bool     hda_input_jack(int index);
void     hda_hold(bool on);                      /* silence, without running the engine (an export is rendering) */
