#pragma once
/* Songs in and out as WAV files on the stick's FAT partition (the part a PC sees).
   Export renders the song — the tracker from its first order entry, and the tape from its start — faster than real
   time into SONGnnnn.WAV (48 kHz, 16-bit stereo), the speakers quiet meanwhile, a piece per pass of the main loop.
   Import reads a PCM WAV (8, 16 or 24 bit, mono or stereo, any rate) into a sample slot, or onto a tape track at the
   head. */
#include <stdint.h>
#include <stdbool.h>
#include "fat.h"

struct song_io {
    bool     exporting;
    uint32_t done, total;               /* frames */
    char     name[13];
    char     status[64];                /* the last thing that happened, for the page */
    uint32_t changes;                   /* bumped when a file is written: lists of files go stale */
};
extern struct song_io song;

uint32_t song_length(void);             /* frames the export would take (with a tail for echoes); 0 = nothing to play */
bool     song_export_start(void);
void     song_work(void);               /* main loop: the next piece of an export */
void     song_export_cancel(void);
int      song_wavs(struct fat_entry *out, int max);                 /* the WAV files in the stick's FAT root */
bool     song_import(const struct fat_entry *e, bool to_tape, int slot_or_track);
