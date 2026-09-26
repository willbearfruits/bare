#include "fat.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

static uint32_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool fat_mount(struct fat *f, int drive, uint64_t part_lba) {
    f->drive = drive; f->base = part_lba;
    if (!plat_blk_read(drive, part_lba, 1, f->buf)) return false;
    uint8_t *b = f->buf;
    if (rd16(b + 510) != 0xAA55 || rd16(b + 11) != 512) return false;
    f->spc = b[13]; uint32_t rsvd = rd16(b + 14), nfats = b[16], fatsz = rd32(b + 36), totsec = rd32(b + 32);
    if (!f->spc || !fatsz || rd16(b + 22) != 0) return false;          /* FAT16 has a 16-bit FAT size: not supported */
    f->fat_lba = rsvd; f->data_lba = rsvd + nfats * fatsz; f->root_clus = rd32(b + 44);
    f->clusters = (totsec - f->data_lba) / f->spc; f->fatsz = fatsz; f->nfats = nfats; f->fsinfo = rd16(b + 48);
    return f->root_clus >= 2;
}

/* the FAT sector last looked at, kept: following a chain reads each FAT sector once, not once per cluster */
static uint8_t fcache[512]; static uint64_t fcache_lba = ~0ull;
static uint32_t next_cluster(struct fat *f, uint32_t c) {
    uint32_t off = c * 4;
    uint64_t lba = f->base + f->fat_lba + off / 512;
    if (lba != fcache_lba) { if (!plat_blk_read(f->drive, lba, 1, fcache)) { fcache_lba = ~0ull; return 0x0FFFFFFF; } fcache_lba = lba; }
    return rd32(fcache + off % 512) & 0x0FFFFFFF;
}
static uint64_t cluster_lba(struct fat *f, uint32_t c) { return f->base + f->data_lba + (uint64_t)(c - 2) * f->spc; }

static void to83(const char *name, int len, char out[11]) {
    memset(out, ' ', 11);
    int i = 0, o = 0;
    for (; i < len && name[i] != '.' && o < 8; i++) out[o++] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i];
    while (i < len && name[i] != '.') i++;
    if (i < len && name[i] == '.') { i++; o = 8; for (; i < len && o < 11; i++) out[o++] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]; }
}

static bool ieq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
    }
    return true;
}

/* search a directory (cluster chain) for a name: matches the 8.3 entry or the VFAT long name */
static bool dir_find(struct fat *f, uint32_t dir, const char name83[11], const char *name, int name_len, struct fat_file *out, bool *is_dir) {
    char lfn[64]; int lfn_len = -1;
    for (uint32_t c = dir; c >= 2 && c < 0x0FFFFFF8; c = next_cluster(f, c)) {
        for (uint32_t s = 0; s < f->spc; s++) {
            uint64_t lba = cluster_lba(f, c) + s;
            if (!plat_blk_read(f->drive, lba, 1, f->buf)) return false;
            for (int e = 0; e < 16; e++) {
                uint8_t *d = f->buf + e * 32;
                if (d[0] == 0) return false;
                if (d[0] == 0xE5) { lfn_len = -1; continue; }
                if ((d[11] & 0x0F) == 0x0F) {                       /* long-name piece: 13 UTF-16 chars */
                    int seq = (d[0] & 0x1F) - 1;
                    if (d[0] & 0x40) { memset(lfn, 0, sizeof lfn); lfn_len = 0; }
                    if (seq < 0 || seq * 13 + 13 > (int)sizeof lfn) { lfn_len = -1; continue; }
                    static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                    for (int i = 0; i < 13; i++) { uint16_t u = d[pos[i]] | (d[pos[i] + 1] << 8); lfn[seq * 13 + i] = (u == 0xFFFF || u == 0) ? 0 : (u < 128 ? (char)u : '?'); }
                    continue;
                }
                if (d[11] & 0x08) { lfn_len = -1; continue; }      /* volume label */
                bool match = memcmp(d, name83, 11) == 0;
                if (!match && lfn_len == 0) { int l = (int)strlen(lfn); match = l == name_len && ieq(lfn, name, l); }
                lfn_len = -1;
                if (!match) continue;
                out->first = (rd16(d + 20) << 16) | rd16(d + 26); out->size = rd32(d + 28);
                out->dirent_lba = lba; out->dirent_idx = e;
                *is_dir = d[11] & 0x10;
                return true;
            }
        }
    }
    return false;
}

bool fat_find(struct fat *f, const char *path, struct fat_file *out) {
    uint32_t dir = f->root_clus; bool is_dir = false;
    while (*path == '/') path++;
    while (*path) {
        int len = 0; while (path[len] && path[len] != '/') len++;
        char n83[11]; to83(path, len, n83);
        if (!dir_find(f, dir, n83, path, len, out, &is_dir)) return false;
        path += len; while (*path == '/') path++;
        if (*path) { if (!is_dir) return false; dir = out->first ? out->first : f->root_clus; }
    }
    return true;
}

/* walk the chain to the cluster containing byte offset `off`, then transfer runs of contiguous clusters */
static bool xfer(struct fat *f, const struct fat_file *fi, uint32_t off, uint8_t *buf, uint32_t len, bool write) {
    if (off + len > fi->size) return false;
    uint32_t cbytes = f->spc * 512, c = fi->first;
    for (uint32_t skip = off / cbytes; skip; skip--) { c = next_cluster(f, c); if (c >= 0x0FFFFFF8 || c < 2) return false; }
    uint32_t inoff = off % cbytes;
    while (len) {
        /* partial first sector(s) */
        uint64_t lba = cluster_lba(f, c) + inoff / 512; uint32_t soff = inoff % 512;
        if (soff || len < 512) {
            if (!plat_blk_read(f->drive, lba, 1, f->buf)) return false;
            uint32_t n = MIN(len, 512 - soff);
            if (write) { memcpy(f->buf + soff, buf, n); if (!plat_blk_write(f->drive, lba, 1, f->buf)) return false; }
            else memcpy(buf, f->buf + soff, n);
            buf += n; len -= n; inoff += n;
        } else {
            /* whole sectors: coalesce a run of contiguous clusters */
            uint32_t run_secs = f->spc - inoff / 512, cc = c;
            while (run_secs * 512 < len && run_secs < 64) {
                uint32_t nc = next_cluster(f, cc);
                if (nc != cc + 1) break;
                cc = nc; run_secs += f->spc;
            }
            uint32_t n = MIN(len / 512, run_secs); if (!n) n = 1;
            if (n > run_secs) n = run_secs;
            if (write ? !plat_blk_write(f->drive, lba, n, buf) : !plat_blk_read(f->drive, lba, n, buf)) return false;
            buf += n * 512; len -= n * 512; inoff += n * 512;
        }
        while (inoff >= cbytes) { inoff -= cbytes; c = next_cluster(f, c); if ((c >= 0x0FFFFFF8 || c < 2) && len) return false; }
    }
    return true;
}
bool fat_read(struct fat *f, const struct fat_file *fi, uint32_t off, void *dst, uint32_t len) { return xfer(f, fi, off, dst, len, false); }
bool fat_write_inplace(struct fat *f, const struct fat_file *fi, uint32_t off, const void *src, uint32_t len) { return xfer(f, fi, off, (void *)src, len, true); }

/* ---- writing new files ---- */
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint8_t fsec[512];                                  /* a FAT sector being changed */

/* one FAT sector, written to every copy of the FAT */
static bool fat_sector_write(struct fat *f, uint32_t s, const uint8_t *data) {
    fcache_lba = ~0ull;
    for (uint32_t k = 0; k < f->nfats; k++) if (!plat_blk_write(f->drive, f->base + f->fat_lba + k * f->fatsz + s, 1, data)) return false;
    return true;
}
static bool fat_set(struct fat *f, uint32_t c, uint32_t v) {           /* a single entry, read-modify-write */
    static uint8_t one[512];
    uint32_t s = c / 128;
    if (!plat_blk_read(f->drive, f->base + f->fat_lba + s, 1, one)) return false;
    wr32(one + (c % 128) * 4, (rd32(one + (c % 128) * 4) & 0xF0000000u) | (v & 0x0FFFFFFF));
    return fat_sector_write(f, s, one);
}
static uint32_t free_chain(struct fat *f, uint32_t c) {
    uint32_t n = 0;
    while (c >= 2 && c < 0x0FFFFFF8) { uint32_t nx = next_cluster(f, c); fat_set(f, c, 0); c = nx; n++; }
    return n;
}
/* FSInfo's free count follows what was taken and given back (left alone where it is already "unknown") */
static void fsinfo_adjust(struct fat *f, int32_t delta, uint32_t next_free) {
    if (!f->fsinfo || f->fsinfo == 0xFFFF || !plat_blk_read(f->drive, f->base + f->fsinfo, 1, f->buf)) return;
    if (rd32(f->buf) != 0x41615252 || rd32(f->buf + 484) != 0x61417272) return;
    uint32_t count = rd32(f->buf + 488);
    if (count != 0xFFFFFFFF) wr32(f->buf + 488, (uint32_t)((int32_t)count + delta));
    if (next_free) wr32(f->buf + 492, next_free);
    plat_blk_write(f->drive, f->base + f->fsinfo, 1, f->buf);
}

/* n clusters, chained, taken in order from the first free ones; 0 when there aren't n free */
static uint32_t alloc_chain(struct fat *f, uint32_t n) {
    uint32_t first = 0, prev = 0, need = n;
    for (uint32_t s = 0; s < f->fatsz && need; s++) {
        if (!plat_blk_read(f->drive, f->base + f->fat_lba + s, 1, fsec)) break;
        bool dirty = false;
        for (uint32_t i = 0; i < 128 && need; i++) {
            uint32_t c = s * 128 + i;
            if (c < 2 || c >= f->clusters + 2 || (rd32(fsec + i * 4) & 0x0FFFFFFF)) continue;
            if (prev) { if (prev / 128 == s) wr32(fsec + (prev % 128) * 4, c); else if (!fat_set(f, prev, c)) return 0; }
            else first = c;
            wr32(fsec + i * 4, 0x0FFFFFFF); dirty = true;
            prev = c; need--;
        }
        if (dirty && !fat_sector_write(f, s, fsec)) return 0;
    }
    if (need) { if (first) free_chain(f, first); return 0; }
    return first;
}

uint32_t fat_free_bytes(struct fat *f) {
    uint32_t free = 0;
    for (uint32_t s = 0; s < f->fatsz; s++) {
        if (!plat_blk_read(f->drive, f->base + f->fat_lba + s, 1, fsec)) break;
        for (uint32_t i = 0; i < 128; i++) { uint32_t c = s * 128 + i; if (c >= 2 && c < f->clusters + 2 && !(rd32(fsec + i * 4) & 0x0FFFFFFF)) free++; }
    }
    uint64_t b = (uint64_t)free * f->spc * 512;
    return b > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)b;
}

/* FAT's date and time for the entry: from the machine's clock, or 2026-01-01 */
static void stamp(uint8_t *d) {
    struct rtc_time t;
    if (!plat_rtc(&t) || t.year < 1980) t = (struct rtc_time){ 2026, 1, 1, 0, 0, 0 };
    uint32_t date = ((uint32_t)(t.year - 1980) << 9) | ((uint32_t)t.month << 5) | t.day, time = ((uint32_t)t.hour << 11) | ((uint32_t)t.min << 5) | (t.sec / 2);
    wr16(d + 14, time); wr16(d + 16, date); wr16(d + 18, date); wr16(d + 22, time); wr16(d + 24, date);
}

bool fat_create(struct fat *f, const char *dir, const char *name, uint32_t size, struct fat_file *out) {
    uint32_t dclus = f->root_clus;
    if (dir && *dir) { struct fat_file d; if (!fat_find(f, dir, &d)) return false; dclus = d.first ? d.first : f->root_clus; }
    char n83[11]; to83(name, (int)strlen(name), n83);
    /* the entry: this name if it is there (its clusters freed), else the first free one */
    uint64_t lba = 0, same_lba = 0; int idx = -1, same = -1; uint32_t old_first = 0; bool end = false;
    for (uint32_t c = dclus; c >= 2 && c < 0x0FFFFFF8 && !end && same < 0; c = next_cluster(f, c)) {
        for (uint32_t s = 0; s < f->spc && !end && same < 0; s++) {
            uint64_t l = cluster_lba(f, c) + s;
            if (!plat_blk_read(f->drive, l, 1, f->buf)) return false;
            for (int e = 0; e < 16; e++) {
                uint8_t *d = f->buf + e * 32;
                if (d[0] == 0 || d[0] == 0xE5) { if (idx < 0) { lba = l; idx = e; } if (d[0] == 0) { end = true; break; } continue; }
                if ((d[11] & 0x0F) != 0x0F && memcmp(d, n83, 11) == 0) { same_lba = l; same = e; old_first = (rd16(d + 20) << 16) | rd16(d + 26); break; }
            }
        }
    }
    if (same >= 0) { lba = same_lba; idx = same; }
    if (idx < 0) { logf("fat: the directory is full"); return false; }
    uint32_t freed = old_first ? free_chain(f, old_first) : 0;
    uint32_t cbytes = f->spc * 512, n = (size + cbytes - 1) / cbytes, first = n ? alloc_chain(f, n) : 0;
    if (n && !first) { fsinfo_adjust(f, (int32_t)freed, 0); logf("fat: no room for %u bytes", size); return false; }
    if (!plat_blk_read(f->drive, lba, 1, f->buf)) return false;
    uint8_t *d = f->buf + idx * 32;
    memset(d, 0, 32); memcpy(d, n83, 11); d[11] = 0x20;                 /* archive */
    stamp(d);
    wr16(d + 20, first >> 16); wr16(d + 26, first & 0xFFFF); wr32(d + 28, size);
    if (!plat_blk_write(f->drive, lba, 1, f->buf)) return false;
    fsinfo_adjust(f, (int32_t)freed - (int32_t)n, first ? first + n : 0);
    *out = (struct fat_file){ first, size, lba, idx };
    return true;
}

int fat_list(struct fat *f, const char *dir, const char *ext, struct fat_entry *out, int max) {
    uint32_t dclus = f->root_clus; int n = 0;
    if (dir && *dir) { struct fat_file d; if (!fat_find(f, dir, &d)) return 0; dclus = d.first ? d.first : f->root_clus; }
    char e83[3] = { ' ', ' ', ' ' };
    for (int i = 0; i < 3 && ext[i]; i++) e83[i] = ext[i] >= 'a' && ext[i] <= 'z' ? ext[i] - 32 : ext[i];
    for (uint32_t c = dclus; c >= 2 && c < 0x0FFFFFF8 && n < max; c = next_cluster(f, c))
        for (uint32_t s = 0; s < f->spc && n < max; s++) {
            uint64_t l = cluster_lba(f, c) + s;
            if (!plat_blk_read(f->drive, l, 1, f->buf)) return n;
            for (int e = 0; e < 16 && n < max; e++) {
                const uint8_t *d = f->buf + e * 32;
                if (d[0] == 0) return n;
                if (d[0] == 0xE5 || (d[11] & 0x0F) == 0x0F || (d[11] & 0x18) || memcmp(d + 8, e83, 3)) continue;
                struct fat_entry *o = &out[n++];
                int k = 0;
                for (int i = 0; i < 8 && d[i] != ' '; i++) o->name[k++] = (char)d[i];
                o->name[k++] = '.';
                for (int i = 8; i < 11 && d[i] != ' '; i++) o->name[k++] = (char)d[i];
                o->name[k] = 0;
                o->size = rd32(d + 28);
                o->file = (struct fat_file){ (rd16(d + 20) << 16) | rd16(d + 26), o->size, l, e };
            }
        }
    return n;
}

/* ---- sequential transfers: a cursor keeps its place in the chain, and runs of adjacent clusters go in one call ---- */
void fat_rewind(const struct fat_file *fi, struct fat_cursor *c) { c->cluster = fi->first; c->off = 0; }
bool fat_seq(struct fat *f, struct fat_cursor *cur, void *buf, uint32_t len, bool write) {
    uint8_t *p = buf;
    uint32_t cb = f->spc * 512;
    len = (len + 511) & ~511u;
    while (len) {
        if (cur->cluster < 2 || cur->cluster >= 0x0FFFFFF8) return false;
        uint32_t in = cur->off % cb, secs = (cb - in) / 512, last = cur->cluster;
        while (secs * 512 < len && secs < 64) { uint32_t n = next_cluster(f, last); if (n != last + 1) break; last = n; secs += f->spc; }
        secs = MIN(secs, MIN(64u, len / 512));
        uint64_t lba = cluster_lba(f, cur->cluster) + in / 512;
        if (write ? !plat_blk_write(f->drive, lba, secs, p) : !plat_blk_read(f->drive, lba, secs, p)) return false;
        p += secs * 512; len -= secs * 512;
        uint32_t moved = in + secs * 512;                          /* whole clusters crossed, then where in the next */
        cur->off += secs * 512;
        for (uint32_t k = moved / cb; k; k--) cur->cluster = next_cluster(f, cur->cluster);
    }
    return true;
}
