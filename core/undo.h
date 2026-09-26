#pragma once
/* Undo and redo. Before an edit, a page saves what the edit is about to change — a pattern, the song's order list, a
   wave slot, an FM patch, a sample with its frames, tape blocks — into an arena sized from RAM; `Ctrl+Z` puts it back,
   `Ctrl+Y` redoes. Saves are grouped into actions (a whole tape take is one), and an action that goes on (a pen
   stroke, a held key) within a second of the last on the same thing adds nothing new. The oldest actions go when the
   arena is full; an action bigger than the whole arena can't be undone, and says so. */
#include <stdint.h>
#include <stdbool.h>

enum { U_PATTERN, U_SONG, U_WAVE, U_FM, U_SAMPLE, U_SMETA, U_TAPE, U_ANS, U_GENDY, U_SIEVE, U_CLOUD, U_UPIC, U_KINDS };   /* U_SMETA: a sample's settings, not its frames; U_ANS: the ANS plate */

void undo_init(uint32_t bytes);
uint32_t undo_capacity(void);
/* an action: begin, save each thing it will change, end. `what` names it for the message ("pattern 03"). */
void undo_begin(int kind, int index, const char *what, uint64_t now);
void undo_save(int kind, int index);           /* U_TAPE: index = track << 12 | span */
void undo_end(void);
static inline void undo_one(int kind, int index, const char *what, uint64_t now) { undo_begin(kind, index, what, now); undo_save(kind, index); undo_end(); }
bool undo_undo(char *msg, int cap);
bool undo_redo(char *msg, int cap);
bool undo_saved(int kind, int index);          /* already saved in the action going on */
