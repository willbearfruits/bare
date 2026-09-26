#pragma once
/* Doom (id Software, 1993) inside BARE!: typing iddqd on any page starts it, full screen, with the WAD it finds on the
   stick (DOOM/ or the root: the player's own IWAD, or Freedoom's). The engine is third_party/doom (GPL-2.0-or-later);
   its glue (third_party/doom/bare) asks BARE! for everything through the functions below — core/doomhost.c has the
   memory, the screen, the keys, the clock and the stick's files, core/doomsnd.c the sound effects and the music, which
   the synth's voices play (tags 0xB000 | channel << 7 | note) on the mixer's DOOM channel.
   The F keys leave Doom for a page; it waits where it was (its clock and its music stop) until iddqd again. Its own
   Quit leaves too. If it stops on an error, the next iddqd starts it afresh: its variables are put back as they were
   at boot (the linker gathers them into doom_data and doom_bss). */
#include <stdint.h>
#include <stdbool.h>

/* ---- BARE!'s side ---- */
void doom_reserve(uint32_t avail);         /* plan_memory: memory for Doom where the machine has it to spare */
uint32_t doom_memory(void);                /* what was set aside, bytes (0: Doom can't run on this machine) */
void doom_open(uint64_t now);              /* iddqd: start Doom, or go back to it */
bool doom_showing(void);
bool doom_resident(void);                  /* started and waiting (not stopped) */
bool doom_started(void);                   /* since boot, at all (the mixer shows its channel from then on) */
void doom_key(uint8_t code, bool down);    /* BARE!'s keys while Doom shows (app.c keeps the F keys) */
void doom_step(uint64_t now);              /* the main loop while Doom shows: its tics and frames */
void doom_leave(void);                     /* back to BARE! */
const char *doom_status(void);             /* for people: what happened last ("no WAD on the stick", …) */

/* ---- for Doom's glue (third_party/doom/bare) ---- */
uint32_t doom_clock_ms(void);              /* Doom's clock: runs only while it shows */
void     doom_sleep_ms(int ms);            /* waits; BARE!'s background work goes on meanwhile */
bool     doom_key_next(uint8_t *code, bool *down);   /* the next key for Doom, BARE!'s key codes */
void     doom_frame(const uint8_t *screen);          /* a 320x200 picture to the screen, 4:3 */
void     doom_palette(const uint8_t *rgb);           /* 256 colours, 768 bytes */
void     doom_quit(void);                  /* Doom's own Quit: back to BARE! once this tic is over */
void     doom_fatal(const char *msg) __attribute__((noreturn));   /* I_Error: Doom stops */
void     doom_print(const char *text);     /* what the engine prints: to the log, by lines */
void    *doom_heap(uint32_t *bytes);       /* the C library's heap */
void    *doom_zone(uint32_t *bytes);       /* the engine's zone */
void    *doom_wad_load(const char *name, uint32_t *bytes);    /* the WAD, read into memory (with a progress bar) */
/* the stick's files, in Doom's folder (a name with a '/' is from the root) */
int32_t  doom_file_size(const char *name);                    /* -1: none */
bool     doom_file_read(const char *name, uint32_t off, void *dst, uint32_t len);
bool     doom_file_write(const char *name, const void *src, uint32_t len);   /* made, or replaced */

/* the engine (third_party/doom/bare/i_bare.c) */
void doom_engine_start(const char *iwad);  /* D_DoomMain: it returns after the first tic */
void doom_engine_tick(void);               /* the tics that are due, and a frame */
void doom_engine_resume(void);             /* coming back: its menu opens (a single-player game waits there) */

/* ---- sound (core/doomsnd.c) ---- */
#define DOOM_SFX_CHANNELS 16
#define DOOM_TAG 0xB000                    /* the music's voices: DOOM_TAG | channel << 7 | note */
void doomsnd_sfx_start(int ch, const uint8_t *pcm, uint32_t len, uint32_t rate, int vol, int sep);   /* 8-bit unsigned */
void doomsnd_sfx_update(int ch, int vol, int sep);   /* vol 0..127, sep 0 (left) .. 254 (right) */
void doomsnd_sfx_stop(int ch);
bool doomsnd_sfx_playing(int ch);
bool doomsnd_music_load(const uint8_t *data, uint32_t len);  /* MUS or a MIDI file; false: neither */
void doomsnd_music_play(bool looping);
void doomsnd_music_stop(void);
void doomsnd_music_volume(int vol);        /* 0..127 */
void doomsnd_music_hold(bool held);        /* the game paused: the song waits (the effects go on) */
bool doomsnd_music_playing(void);
void doomsnd_pause(bool paused);           /* leaving Doom: everything stops where it is */
void doomsnd_all_off(void);
void doomsnd_set_memory(void *events, uint32_t bytes);   /* where a song's events go */
bool doomsnd_render(int32_t *l, int32_t *r, uint32_t n, bool add);   /* the effects into the DOOM bus; true if any sounded */
void doomsnd_block(uint32_t n);            /* the audio loop, before each block: the music's events */
