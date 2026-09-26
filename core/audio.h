#pragma once
/* The audio graph, rendered from the audio interrupt a block at a time:
     sequencer and rhythm events (on exact sample boundaries) → voices, on the PLAY / SEQ / RHYTHM channels, and the
     input channel → mixer (core/mix.h) → stretcher → tape → echo (the channels' sends) → master volume → DC removal
     → look-ahead limiter → output and scope.
   The sample clock is the master clock: the sequencer counts frames, not timer ticks. */
#include <stdint.h>
#include <stdbool.h>

#define AUDIO_SCOPE_LEN 4096            /* power of two */

void     audio_init(uint32_t rate);
void     audio_render(int16_t *stereo, uint32_t frames);     /* interleaved L/R int16 */
uint32_t audio_rate(void);
uint64_t audio_frames(void);                                 /* frames rendered since boot */

void     audio_volume_step(int d);        /* +1 louder, -1 quieter: 2 dB a step, 0 to -40 dB; unmutes */
void     audio_toggle_mute(void);
void     audio_set_mute(bool on);
int      audio_volume_index(void);          /* 0 = 0 dB, 3 = -6 dB … 20 = -40 dB (what the stick remembers) */
void     audio_set_volume_index(int i);
void     audio_set_onebit(bool on);         /* the PC speaker's square wave, rendered as the output (see platform) */
int      audio_volume_db(void);
bool     audio_muted(void);
void     audio_set_echo(bool on);
bool     audio_echo(void);
uint32_t audio_echo_ms(void);                                /* current echo time (a dotted eighth at the tempo) */

/* the input (line in / mic): the platform's driver hands over interleaved frames from the audio interrupt */
void     audio_input_push(const int16_t *stereo, uint32_t frames);
uint32_t audio_input_latency(void);         /* frames the input waits in its FIFO before it is played */

/* scope tap: rings of the output, and the index one past the newest frame */
void     audio_scope_lr(const int16_t **left, const int16_t **right, uint32_t *head);
int      audio_peak(int ch);                                 /* output peak meter 0..32767, decaying (0 L, 1 R) */
int      audio_limiter_q15(void);                            /* the limiter's gain now, 32767 = not limiting */
