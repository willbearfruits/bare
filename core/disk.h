#pragma once
/* Partition discovery: our FAT boot partition (kernel slots, limine.conf), the HBTAPE project partition, update files. */
#include <stdint.h>
#include <stdbool.h>
#include "fat.h"

#define TAPE_MAGIC "HBTAPE01"
#define SLOT_MAGIC "HBPROJ01"
#define TAPE_SLOT_SECTORS 2048          /* 1 MiB per slot */
#define TAPE_MAX_SLOTS 64

/* The header also carries the machine settings (volume, mute, 1-bit, MIDI, the colour scheme) behind "SET1"; older
   sticks have zeros there.
   They are written whenever the header is (saving or loading a project), never on their own: the stick may be out. */
struct tape_hdr { char magic[8]; uint32_t slot_sectors, slots, last_slot; uint32_t part_sectors;
                  char set_magic[4]; uint8_t volume, flags, midi_lo, midi_hi;     /* midi: port+1, channel, switches */
                  uint8_t theme, splash; };             /* 2.3 on: the colour scheme; the last splash shown + 1 */
#define SET_MAGIC  "SET1"
#define SET_MUTED  1
#define SET_ONEBIT 2
#define SET_LINK   4                     /* Ableton Link on */
/* saved: packed date (year-2000)<<26 | month<<22 | day<<17 | hour<<12 | min<<6 | sec, valid when version >= 2 (v1 stored ms since boot). */
/* version 3: frames beyond the slot's own megabyte live in an extent further on in the partition (sectors from its
   start); the partition grows to the end of the stick */
struct slot_hdr { char magic[8]; char name[32]; uint32_t version, len, crc, saved; uint32_t ext_start, ext_sectors; };
uint32_t disk_pack_date(void);                        /* now, in slot_hdr.saved form; 0 without a clock */
uint64_t disk_free_bytes(void);                       /* room left for projects' frames */
void     disk_date_str(uint32_t packed, char *out, int cap);   /* "26-09-01 22:31" or "" */

struct disk_state {
    bool     have_boot_fat, have_tape;
    int      boot_drive; uint64_t fat_lba, tape_lba, tape_sectors;
    struct fat fat;
    struct tape_hdr tape;
    char     status[96];
    bool     tape_left_out;                /* the last save had no room for the tape's frames */
    /* slot directory cache */
    struct slot_hdr slot[TAPE_MAX_SLOTS];
    bool     slot_used[TAPE_MAX_SLOTS];
};
extern struct disk_state disk;

void disk_init(void);                                 /* scan drives, mount, read the slot directory */
void disk_rescan(void);                               /* after (re)plugging the stick */
bool disk_save_slot(int slot, const char *name);      /* serialize the current project into a slot */
bool disk_load_slot(int slot);
bool disk_delete_slot(int slot);
bool disk_autoload(void);                             /* load the slot saved last, if any */
bool disk_settings(uint8_t *volume, uint8_t *flags, uint16_t *midi, uint8_t *theme);  /* the stick's saved settings; false if none */
void disk_note_settings(uint8_t volume, uint8_t flags, uint16_t midi, uint8_t theme, uint8_t splash);   /* what the next header write carries */
/* update files: scan every drive's FAT root for BARE.UPD (or 1.0-2.2's HOMEBREW.UPD) newer than this build; apply into the inactive kernel slot */
bool disk_check_update(uint32_t running_version, char *msg, int msg_len);
