#pragma once
/* Project serialization: everything you made, as a small chunked blob. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define PROJECT_MAX (128 * 1024)
/* Sample frames don't go into the blob: they follow it on the stick, written straight from the sampler's memory and
   read straight back into it. project_save places them, in slot order, in the room it is given; what doesn't fit is
   saved without its frames. After a save or a load, project_media lists what goes where. */
struct project_media { void *data; uint32_t bytes, offset; };     /* offset: bytes from the start of the room */
/* room: bytes there are for frames; with_tape: the tape's blocks go too (after the samples, 64 KiB each) */
size_t project_save(uint8_t *out, size_t cap, uint32_t room, bool with_tape);   /* returns bytes written, 0 on error */
bool   project_load(const uint8_t *in, size_t len);
int    project_media_count(void);                                  /* after a save or a load: the pieces of frames */
bool   project_media_get(int i, struct project_media *m);          /* (a load's tape blocks are taken from the pool here) */
uint32_t project_media_end(void);                                  /* bytes from the start of the room to the end of the last */
int    project_media_dropped(void);                                /* samples the last save had no room for */
bool   project_tape_dropped(void);                                 /* the last save left the tape out */
void   project_loaded(void);                                       /* after the frames are in: the tape's peaks and ends */
