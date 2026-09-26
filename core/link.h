#pragma once
/* Ableton Link, spoken directly: tempo, beat phase and start/stop shared with the other Link programs on the network
   (Live, phone apps, another machine running this). Discovery on 224.76.78.75:20808 (each node multicasts its state
   four times a second and answers the others'), a ping-pong on its own port to measure a session's clock, and the
   sessions' rules: the one whose clock has run longest (or, level, the lower id) is joined; within one, the timeline
   with the higher beat origin wins; start/stop goes by the latest timestamp. Times are microseconds: "host" is this
   machine's clock (plat_us), "ghost" the session's shared one (ghost = host + intercept); beats are micro-beats.
   The sequencer and the rhythm section follow: the session's tempo (fractional), a start on the next bar of its beat
   grid, and their phase in the bar held to it by small corrections each audio block. */
#include <stdint.h>
#include <stdbool.h>

#define LINK_QUANTUM 4                           /* beats: the bar the phases line up in */
struct link_state {
    bool on;
    int peers;                                   /* other nodes in our session */
    uint32_t bpm_q16;                            /* the session's tempo, BPM × 65536 */
    bool playing;
    char status[64];
};
extern struct link_state lnk;

void link_init(void);
void link_enable(bool on);
void link_work(uint64_t now_ms);                 /* the main loop: messages, measurements, the sequencer's changes */
bool link_start_request(bool pattern_only);      /* seq_play(true) asks: true = Link starts it on the next bar */
bool link_rhythm_request(void);                  /* the same for the rhythm section */
void link_stop_request(void);                    /* the sequencer stopped here: tell the session */
/* the audio side (interrupt context), before a block whose first frame is heard at host_us */
void link_audio_block(int64_t host_us, uint32_t frames);
