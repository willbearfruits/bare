#pragma once
/* Minimal FAT32: read files, overwrite them in place, and create files in a directory (a cluster chain in both FATs,
   an 8.3 entry) — for exported songs. No long names are written. */
#include <stdint.h>
#include <stdbool.h>

struct fat {
    int drive; uint64_t base;          /* partition start LBA */
    uint32_t spc, fat_lba, data_lba, root_clus, clusters, fatsz, nfats, fsinfo;
    uint8_t buf[512];
};
struct fat_file { uint32_t first, size; uint64_t dirent_lba; int dirent_idx; };
struct fat_entry { char name[13]; uint32_t size; struct fat_file file; };      /* "SONG0001.WAV" */

bool fat_mount(struct fat *f, int drive, uint64_t part_lba);
bool fat_find(struct fat *f, const char *path, struct fat_file *out);     /* "BOOT/HB_A.ELF" — 8.3 names, any case */
bool fat_read(struct fat *f, const struct fat_file *fi, uint32_t off, void *dst, uint32_t len);
bool fat_write_inplace(struct fat *f, const struct fat_file *fi, uint32_t off, const void *src, uint32_t len);
/* a new file of `size` bytes in directory `dir` ("" for the root), or an existing one of that name replaced; its
   clusters come from the first free ones, so on a fresh stick they run contiguously */
bool fat_create(struct fat *f, const char *dir, const char *name83, uint32_t size, struct fat_file *out);
int  fat_list(struct fat *f, const char *dir, const char *ext, struct fat_entry *out, int max);   /* files with that extension */
uint32_t fat_free_bytes(struct fat *f);
/* a file from its start, in order: len rounds up to whole sectors, which the file's last cluster always covers */
struct fat_cursor { uint32_t cluster, off; };
void fat_rewind(const struct fat_file *fi, struct fat_cursor *c);
bool fat_seq(struct fat *f, struct fat_cursor *c, void *buf, uint32_t len, bool write);
