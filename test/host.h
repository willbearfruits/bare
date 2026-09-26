#pragma once
/* Host test harness: runs the portable core (core/) as an ordinary Linux program. test/host.c implements
   core/platform.h with a simulated clock, scripted input, a memory framebuffer and an optional disk image. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "platform.h"

extern uint64_t host_now_ms;
extern bool host_log;                         /* echo the core's log to stderr */

void host_key(uint8_t code, bool down);       /* queue a key event (core/keys.h codes) */
void host_tap(uint8_t code);                  /* down + up */
void host_shift_tap(uint8_t code);            /* Shift+key: the function layer */
void host_touch(int tx, int ty, int z);       /* a touchpad finger at tx, ty (0..32767), pressure z (0 = lifted) */
void host_finger(int finger, int tx, int ty, int z, int size);   /* the same for finger 0..4, with a contact size */
void host_pointer(int ax, int ay, int buttons);   /* absolute pointer, 0..32767 */
bool host_disk_open(const char *path);        /* back drive 0 with a disk image file */
int  host_disk_add(const char *path);         /* another drive after it: its number, -1 on failure */
void host_disk_remove(int drive);             /* the last one added */
extern uint32_t host_boot_id;                 /* what plat_boot_disk_id says (0: not known) */
void host_net(bool present, bool link);       /* a network port, and whether its cable is in */
void host_net_inject(const void *frame, int n);   /* a frame arriving */
int  host_net_sent(void *frame, int cap);     /* the next frame the core sent, 0 when none */
void host_input_tone(double hz, int amp);     /* what the line input hears (input 0, "LINE IN"), 0 amp = silence */
extern uint32_t host_latency;                 /* what plat_audio_latency reports (frames) */
void host_midi_in(const uint8_t *bytes, int n);   /* MIDI bytes arriving at the port ("HOST MIDI") */
extern uint8_t host_midi_out[65536];          /* what the instrument sent */
extern int host_midi_out_len;

/* framebuffer: XRGB8888 in memory */
struct fb_info host_fb(int w, int h);

/* Advance simulated time by ms: 1 ms audio ticks (sequencer + synth render), main-loop steps in between.
   Rendered audio goes to host_wav if it is open. */
void host_run(int ms);
void host_audio_ms(void);                     /* one 1 ms audio tick, 48 frames rendered in lockstep */
void host_audio_pump_ms(void);                /* one 1 ms tick the way the HDA driver renders (chunks, lead) */
void app_step(uint64_t now);                  /* one main-loop iteration (core/app.c) */

/* audio capture */
bool host_wav_open(const char *path);
void host_wav_close(void);
extern int16_t *host_last_audio;              /* the frames rendered by the last audio tick (stereo) */
extern int host_last_frames;

bool host_write_ppm(const char *path, const struct fb_info *fb);

/* the core entry points that changed shape since the baseline this harness also measures (-DHB_BASELINE) */
#if __has_include("audio.h") && !defined(HB_BASELINE)
#include "audio.h"
#define HOST_RENDER(buf, n) audio_render(buf, n)
#define HOST_SET_ECHO(on)   audio_set_echo(on)
#define HOST_DEMO(n)        host_shift_tap((uint8_t)('1' + (n)))   /* on SEQ */
#define HOST_FREEZE()       host_shift_tap('f')
#define HOST_KEY_FM         KEY_F5
#define HOST_KEY_TAPE       KEY_F6
#define HOST_KEY_FILE       KEY_F7
#else
#include "synth.h"
#define HOST_RENDER(buf, n) synth_render(buf, n)
#define HOST_SET_ECHO(on)   synth_set_echo(on)
#define HOST_DEMO(n)        host_tap((uint8_t)(KEY_F5 + (n)))      /* the baseline's keys */
#define HOST_FREEZE()       host_tap(KEY_F11)
#define HOST_KEY_FM         KEY_DELETE
#define HOST_KEY_TAPE       KEY_PRTSC
#define HOST_KEY_FILE       KEY_INSERT
#endif

static inline uint64_t host_rdtsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
extern bool host_camera_present;                  /* plat_camera: a synthetic 160x120 camera (on by default) */
