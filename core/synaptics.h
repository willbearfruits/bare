#pragma once
/* The arithmetic half of the Synaptics PS/2 touchpad driver: its 6-byte absolute packets (W mode) into touch events,
   with the second finger of advanced gesture mode. Nothing here touches hardware, so the host checks run it. */
#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

struct syn_state {
    bool agm;                                        /* advanced gesture mode is on: w = 2 packets carry finger two */
    int x0, x1, y0, y1;                              /* the pad's range, widened as fingers go further */
    int agm_x, agm_y, agm_z;
    struct { bool on; int x, y; } slot[2];           /* the two fingers, each kept on its slot */
};
void syn_start(struct syn_state *s, bool agm);
/* one packet: the events it makes (up to 3), or -1 for a pass-through packet (a TrackPoint: its 3 bytes in rel) */
int  syn_packet(struct syn_state *s, const uint8_t p[6], struct pointer_event out[3], uint8_t rel[3]);
