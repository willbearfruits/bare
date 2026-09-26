#pragma once
/* MIDI in and out, over whatever port the platform has (a serial port in "PC mode" at 38400 baud, an MPU-401; USB
   MIDI once there is a USB stack). In: notes play the page's sound (or go into the tracker at the cursor), with
   velocity, the sustain pedal, pitch bend, the mod wheel on the filter, program change; an external clock sets the
   tempo and starts and stops the song. Out: the tracker's channels as MIDI channels 1-8, and clock, start and stop. */
#include <stdint.h>
#include <stdbool.h>

struct midi_state {
    int8_t   port;                      /* the platform's port, -1 = off */
    bool     clock_in, clock_out, notes_out, thru;
    uint8_t  in_channel;                /* 0 = every channel, else 1..16 */
    uint16_t ext_bpm;                   /* the tempo an incoming clock gives, 0 = none heard lately */
    uint32_t in_msgs, out_bytes;
    char     monitor[8][28];            /* what came in last, newest first */
};
extern struct midi_state midi;

struct midi_msg { uint8_t status, d1, d2; };   /* status: 0x80 note off, 0x90 on, 0xB0 CC, 0xC0 program, 0xE0 bend (| channel) */

void midi_init(uint32_t rate);
bool midi_open(int port);               /* -1 closes */
void midi_work(uint64_t now);           /* main loop: read, parse, send what is waiting */
bool midi_next(struct midi_msg *m);     /* a channel message for the app to play */
/* audio interrupt */
void midi_out_note(int ch, uint8_t note, uint8_t vel);   /* vel 0 = off; ch 0..15 */
void midi_clock_run(uint32_t frames);   /* the clock out, counted on the sample clock */
void midi_transport(bool start);        /* the song started / stopped: FA / FC */
