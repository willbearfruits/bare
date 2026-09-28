#pragma once
/* Doom's music into the tracker, as breakcore (SEQ: ⇧4). The first level's song is read from the WAD on the stick when
   asked — D_E1M1, or MAP01's D_RUNNIN from a Doom II WAD — so its notes are the WAD's and never part of BARE!. They are
   laid onto the tracker's grid, a 16th a row (a note between rows waits with EDx): the busiest guitar on two channels
   (its chords), the other on a third, the bass on a fourth. Then the arrangement, which is this file's own: 172 BPM,
   chopped breakbeats under it all, snare rolls, stutters and gates, junk metal, an intro break, a breakdown half way
   and a tape stop to end. */
#include <stdint.h>
#include <stdbool.h>

bool doomtrack_load(char *msg, int msg_len);                  /* from the stick's WAD; msg: what happened */
/* a song (MUS or a MIDI file) as the tracker's breakcore song: the part after the WAD (the checks call it) */
bool doomtrack_arrange(const uint8_t *song, uint32_t len, const char *title, char *msg, int msg_len);
