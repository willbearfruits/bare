#include "disk.h"
#include "inst.h"
#include "project.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

struct disk_state disk;
static uint8_t sec[512];
static uint8_t blob[PROJECT_MAX];

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

struct part { uint8_t type; uint64_t lba, sectors; };
static int read_parts(int drive, struct part *out) {
    if (!plat_blk_read(drive, 0, 1, sec) || sec[510] != 0x55 || sec[511] != 0xAA) return 0;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = sec + 446 + i * 16;
        if (!e[4]) continue;
        out[n++] = (struct part){ e[4], rd32(e + 8), rd32(e + 12) };
    }
    return n;
}
static bool is_fat_type(uint8_t t) { return t == 0x0B || t == 0x0C || t == 0x0E || t == 0x06 || t == 0xEF || t == 0x04; }

static void read_directory(void) {
    for (uint32_t i = 0; i < disk.tape.slots && i < TAPE_MAX_SLOTS; i++) {
        uint64_t lba = disk.tape_lba + 1 + (uint64_t)i * disk.tape.slot_sectors;
        disk.slot_used[i] = false;
        if (!plat_blk_read(disk.boot_drive, lba, 1, sec)) continue;
        if (memcmp(sec, SLOT_MAGIC, 8) != 0) continue;
        memcpy(&disk.slot[i], sec, sizeof(struct slot_hdr));
        disk.slot[i].name[31] = 0;
        disk.slot_used[i] = true;
    }
}

/* The stick image's project partition is 128 MiB; sticks are bigger. When it is the last partition and the stick
   goes on past it, its size in the partition table is made to reach the end of the stick — once, and never smaller. */
static void grow_partition(int d, uint64_t lba, uint64_t *sectors) {
    uint64_t total = plat_blk_sectors(d), end = lba + *sectors;
    if (!total || total > 0xFFFFFFFFull || total < end + 8192) return;             /* MBR's limit, or nothing to gain */
    if (!plat_blk_read(d, 0, 1, sec) || sec[510] != 0x55 || sec[511] != 0xAA) return;
    int me = -1;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = sec + 446 + i * 16;
        if (!e[4]) continue;
        uint32_t st = rd32(e + 8);
        if (st == lba && e[4] == 0x7F) me = i; else if (st > lba) return;             /* something after it: leave it */
    }
    if (me < 0) return;
    uint8_t *e = sec + 446 + me * 16;
    uint32_t grown = (uint32_t)(total - lba);
    e[12] = (uint8_t)grown; e[13] = (uint8_t)(grown >> 8); e[14] = (uint8_t)(grown >> 16); e[15] = (uint8_t)(grown >> 24);
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;                                          /* CHS end: "use the LBA" */
    if (!plat_blk_write(d, 0, 1, sec) || !plat_blk_read(d, 0, 1, sec) || rd32(sec + 446 + me * 16 + 12) != grown) { logf("disk: could not grow the project partition"); return; }
    logf("disk: project partition grown from %lu to %lu MiB", *sectors / 2048, (uint64_t)grown / 2048);
    *sectors = grown;
}

/* ---- extents: where projects keep frames that don't fit their slot ---- */
static uint64_t area_start(void) { return 1 + (uint64_t)disk.tape.slots * disk.tape.slot_sectors; }
/* the first gap of `need` sectors after the slots, among the other projects' extents; its start, or 0 */
static uint64_t find_extent(uint32_t need, int except) {
    uint64_t starts[TAPE_MAX_SLOTS], lens[TAPE_MAX_SLOTS]; int n = 0;
    for (uint32_t i = 0; i < disk.tape.slots && i < TAPE_MAX_SLOTS; i++) {
        if (!disk.slot_used[i] || (int)i == except || disk.slot[i].version < 3 || !disk.slot[i].ext_sectors) continue;
        uint64_t st = disk.slot[i].ext_start, ln = disk.slot[i].ext_sectors;
        int k = n++;
        while (k > 0 && starts[k - 1] > st) { starts[k] = starts[k - 1]; lens[k] = lens[k - 1]; k--; }
        starts[k] = st; lens[k] = ln;
    }
    uint64_t at = area_start();
    for (int k = 0; k <= n; k++) {
        uint64_t limit = k < n ? starts[k] : disk.tape_sectors;
        if (limit >= at + need) return at;
        if (k < n) at = MAX(at, starts[k] + lens[k]);
    }
    return 0;
}
uint64_t disk_free_bytes(void) {
    if (!disk.have_tape) return 0;
    uint64_t used = 0;
    for (uint32_t i = 0; i < disk.tape.slots && i < TAPE_MAX_SLOTS; i++) if (disk.slot_used[i] && disk.slot[i].version >= 3) used += disk.slot[i].ext_sectors;
    uint64_t area = disk.tape_sectors > area_start() ? disk.tape_sectors - area_start() : 0;
    return (area > used ? area - used : 0) * 512;
}

void disk_init(void) {
    memset(&disk, 0, sizeof disk);
    disk.boot_drive = -1;
    int nd = plat_blk_drives();
    if (!nd) { snfmt(disk.status, sizeof disk.status, "no drive found: plug the stick in and press R"); return; }
    /* the disk the machine started from goes first (its MBR signature, where the loader told it): with BARE! both on
       a stick and installed on the internal disk, the one that booted keeps the projects */
    int first = 0; uint32_t boot_id = plat_boot_disk_id();
    for (int d = 0; boot_id && d < nd; d++) if (plat_blk_read(d, 0, 1, sec) && rd32(sec + 440) == boot_id) { first = d; break; }
    for (int k = 0; k < nd && disk.boot_drive < 0; k++) {
        int d = (first + k) % nd;
        struct part p[4]; int n = read_parts(d, p);
        for (int i = 0; i < n; i++) {
            if (is_fat_type(p[i].type)) {
                struct fat f; struct fat_file fi;
                if (fat_mount(&f, d, p[i].lba) && fat_find(&f, "BOOT/HB_A.ELF", &fi)) {
                    disk.fat = f; disk.fat_lba = p[i].lba; disk.have_boot_fat = true; disk.boot_drive = d;
                }
            }
        }
        if (disk.boot_drive != d) continue;
        for (int i = 0; i < n; i++) {
            if (p[i].type != 0x7F) continue;
            if (!plat_blk_read(d, p[i].lba, 1, sec) || memcmp(sec, TAPE_MAGIC, 8) != 0) continue;
            memcpy(&disk.tape, sec, sizeof disk.tape);
            disk.tape_lba = p[i].lba; disk.tape_sectors = p[i].sectors; disk.have_tape = true;
            if (disk.tape.slots > TAPE_MAX_SLOTS) disk.tape.slots = TAPE_MAX_SLOTS;
            grow_partition(d, p[i].lba, &p[i].sectors);
            disk.tape_sectors = p[i].sectors;
            /* grow into the whole partition (the stick may be bigger than the image) */
            uint32_t possible = (uint32_t)((p[i].sectors - 1) / disk.tape.slot_sectors);
            if (possible > TAPE_MAX_SLOTS) possible = TAPE_MAX_SLOTS;
            if (possible > disk.tape.slots) { disk.tape.slots = possible; memcpy(sec, &disk.tape, sizeof disk.tape); plat_blk_write(d, p[i].lba, 1, sec); }
            read_directory();
        }
    }
    if (disk.have_tape) snfmt(disk.status, sizeof disk.status, "drive %d · %u project slots of %u KiB", disk.boot_drive, disk.tape.slots, disk.tape.slot_sectors / 2);
    else if (disk.have_boot_fat) snfmt(disk.status, sizeof disk.status, "boot drive found but no project partition (flash the .img, not the .iso)");
    else snfmt(disk.status, sizeof disk.status, "%d drive(s), none with BARE! on it", nd);
    logf("disk: %s", disk.status);
}

void disk_rescan(void) { plat_blk_rescan(); disk_init(); inst_load_all(); }   /* the stick may carry other instruments */

uint32_t disk_pack_date(void) {
    struct rtc_time t;
    if (!plat_rtc(&t) || t.year < 2000) return 0;
    return ((uint32_t)(t.year - 2000) << 26) | ((uint32_t)t.month << 22) | ((uint32_t)t.day << 17) | ((uint32_t)t.hour << 12) | ((uint32_t)t.min << 6) | t.sec;
}
void disk_date_str(uint32_t p, char *out, int cap) {
    if (!p) { if (cap) out[0] = 0; return; }
    snfmt(out, (size_t)cap, "%02u-%02u-%02u %02u:%02u", (p >> 26) & 63, (p >> 22) & 15, (p >> 17) & 31, (p >> 12) & 31, (p >> 6) & 63);
}

static uint64_t slot_lba(int s) { return disk.tape_lba + 1 + (uint64_t)s * disk.tape.slot_sectors; }
/* A slot: the header sector, the project blob (at most PROJECT_MAX), then the sample frames. */
#define MEDIA_SECTOR (1 + PROJECT_MAX / 512)

static uint32_t slot_room(void) { return disk.tape.slot_sectors > MEDIA_SECTOR ? (disk.tape.slot_sectors - MEDIA_SECTOR) * 512 : 0; }
/* where a byte of a project's frames is: in the slot after the blob, then in its extent */
static uint64_t media_lba(int s, const struct slot_hdr *h, uint32_t off) {
    uint32_t room = slot_room();
    return off < room ? slot_lba(s) + MEDIA_SECTOR + off / 512 : disk.tape_lba + h->ext_start + (off - room) / 512;
}
/* frames straight between memory and the stick, in pieces that don't cross from the slot into the extent; a last
   partial sector goes through a buffer of its own (not `sec`: the slot's header waits there while the frames go out) */
static bool media_io(int s, const struct slot_hdr *h, const struct project_media *m, bool write) {
    static uint8_t part[512];
    uint32_t done = 0, room = slot_room();
    while (done < m->bytes) {
        uint32_t off = m->offset + done, n = m->bytes - done;
        if (off < room) n = MIN(n, room - off);
        uint64_t lba = media_lba(s, h, off);
        uint8_t *p = (uint8_t *)m->data + done;
        uint32_t full = n / 512, rest = n % 512;
        if (full && !(write ? plat_blk_write(disk.boot_drive, lba, full, p) : plat_blk_read(disk.boot_drive, lba, full, p))) return false;
        if (rest) {
            uint8_t *tail = p + (size_t)full * 512;
            if (write) { memset(part, 0, 512); memcpy(part, tail, rest); if (!plat_blk_write(disk.boot_drive, lba + full, 1, part)) return false; }
            else { if (!plat_blk_read(disk.boot_drive, lba + full, 1, part)) return false; memcpy(tail, part, rest); }
        }
        done += n;
    }
    return true;
}

bool disk_save_slot(int s, const char *name) {
    if (!disk.have_tape || s < 0 || (uint32_t)s >= disk.tape.slots) return false;
    /* everything, with the frames past the slot's megabyte in an extent: this slot's own if it is big enough, else
       the first gap; with no gap big enough, the tape stays behind */
    uint32_t room = slot_room();
    uint64_t space = room + disk_free_bytes() + (disk.slot_used[s] && disk.slot[s].version >= 3 ? (uint64_t)disk.slot[s].ext_sectors * 512 : 0);
    size_t len = project_save(blob, sizeof blob, (uint32_t)MIN(space, 0xFFFFFFFFull), true);
    if (!len) return false;
    struct slot_hdr h; memset(&h, 0, sizeof h);
    uint32_t end = project_media_end();
    if (end > room) {
        uint32_t need = (end - room + 511) / 512;
        if (disk.slot_used[s] && disk.slot[s].version >= 3 && disk.slot[s].ext_sectors >= need) { h.ext_start = disk.slot[s].ext_start; h.ext_sectors = need; }
        else if ((h.ext_start = (uint32_t)find_extent(need, s)) != 0) h.ext_sectors = need;
        else {
            len = project_save(blob, sizeof blob, room, false);
            if (!len) return false;
            h.ext_start = h.ext_sectors = 0;
        }
    }
    disk.tape_left_out = project_tape_dropped();
    memcpy(h.magic, SLOT_MAGIC, 8); snfmt(h.name, sizeof h.name, "%s", name);
    h.version = 3; h.len = (uint32_t)len; h.crc = crc32(blob, len); h.saved = disk_pack_date();
    memset(sec, 0, 512); memcpy(sec, &h, sizeof h);
    uint32_t nsec = (uint32_t)((len + 511) / 512);
    if (nsec + 1 > disk.tape.slot_sectors) return false;
    memset(blob + len, 0, nsec * 512 - len);
    if (!plat_blk_write(disk.boot_drive, slot_lba(s) + 1, nsec, blob)) return false;
    for (int i = 0; i < project_media_count(); i++) { struct project_media m; if (project_media_get(i, &m) && !media_io(s, &h, &m, true)) return false; }
    if (!plat_blk_write(disk.boot_drive, slot_lba(s), 1, sec)) return false;
    disk.slot[s] = h; disk.slot_used[s] = true;
    disk.tape.last_slot = (uint32_t)s + 1;
    memset(sec, 0, 512); memcpy(sec, &disk.tape, sizeof disk.tape);
    plat_blk_write(disk.boot_drive, disk.tape_lba, 1, sec);
    logf("disk: saved '%s' to slot %d (%u bytes)", name, s, (unsigned)len);
    return true;
}

bool disk_load_slot(int s) {
    if (!disk.have_tape || s < 0 || (uint32_t)s >= disk.tape.slots || !disk.slot_used[s]) return false;
    struct slot_hdr *h = &disk.slot[s];
    if (h->len > sizeof blob) return false;
    uint32_t nsec = (h->len + 511) / 512;
    if (!plat_blk_read(disk.boot_drive, slot_lba(s) + 1, nsec, blob)) return false;
    if (crc32(blob, h->len) != h->crc) { logf("disk: slot %d crc mismatch", s); return false; }
    if (!project_load(blob, h->len)) return false;
    for (int i = 0; i < project_media_count(); i++) {
        struct project_media m;
        if (!project_media_get(i, &m)) { logf("disk: slot %d: no room for its tape here", s); break; }
        if (!media_io(s, h, &m, false)) logf("disk: slot %d: frames unreadable", s);
    }
    project_loaded();
    disk.tape.last_slot = (uint32_t)s + 1;
    memset(sec, 0, 512); memcpy(sec, &disk.tape, sizeof disk.tape);
    plat_blk_write(disk.boot_drive, disk.tape_lba, 1, sec);
    logf("disk: loaded '%s' from slot %d", h->name, s);
    return true;
}

bool disk_delete_slot(int s) {
    if (!disk.have_tape || s < 0 || (uint32_t)s >= disk.tape.slots) return false;
    memset(sec, 0, 512);
    if (!plat_blk_write(disk.boot_drive, slot_lba(s), 1, sec)) return false;
    disk.slot_used[s] = false;
    return true;
}

bool disk_settings(uint8_t *volume, uint8_t *flags, uint16_t *midi, uint8_t *theme) {
    if (!disk.have_tape || memcmp(disk.tape.set_magic, SET_MAGIC, 4) != 0) return false;
    *volume = disk.tape.volume; *flags = disk.tape.flags; *midi = (uint16_t)(disk.tape.midi_lo | disk.tape.midi_hi << 8);
    *theme = disk.tape.theme;
    return true;
}
void disk_note_settings(uint8_t volume, uint8_t flags, uint16_t midi, uint8_t theme, uint8_t splash) {
    if (!disk.have_tape) return;
    memcpy(disk.tape.set_magic, SET_MAGIC, 4); disk.tape.volume = volume; disk.tape.flags = flags; disk.tape.theme = theme;
    if (splash) disk.tape.splash = splash;                          /* a rescan (a takeover, a replug) keeps it too */
    disk.tape.midi_lo = (uint8_t)midi; disk.tape.midi_hi = (uint8_t)(midi >> 8);
}

bool disk_autoload(void) {
    if (!disk.have_tape || !disk.tape.last_slot) return false;
    return disk_load_slot((int)disk.tape.last_slot - 1);
}

/* ---- updates ---- */
/* HBUPD001: the 32-bit kernel. HBUPD002: and the 64-bit one after it, with its own build number (each kernel compares
   its own, so an update can't loop). The kernels go into the slots the stick isn't booting (HB_A/HB_B, H64_A/H64_B),
   are checked, and then limine.conf is switched over to them, in place. */
struct __attribute__((packed)) upd_hdr { char magic[8]; uint32_t version, size, crc, version64, size64, crc64; };
#define KERNEL_SLOT (4u << 20)

/* the slot letter ('A'/'B', any case) after `prefix` ("hb_", "h64_") on a "path:" line of limine.conf, or -1 */
static int slot_index(const char *text, uint32_t n, const char *prefix) {
    uint32_t pl = (uint32_t)strlen(prefix);
    for (uint32_t i = 0; i + 5 < n; i++) {
        if (memcmp(text + i, "path:", 5) != 0) continue;
        for (uint32_t j = i + 5; j + pl + 1 < n && text[j] != '\n'; j++) {
            uint32_t k = 0;
            while (k < pl && (text[j + k] >= 'A' && text[j + k] <= 'Z' ? text[j + k] | 0x20 : text[j + k]) == prefix[k]) k++;
            if (k == pl && ((text[j + pl] | 0x20) == 'a' || (text[j + pl] | 0x20) == 'b')) return (int)(j + pl);
        }
    }
    return -1;
}
static void flip(char *c) { bool lower = *c >= 'a', a = (*c | 0x20) == 'a'; *c = a ? (lower ? 'b' : 'B') : (lower ? 'a' : 'A'); }

/* a kernel from the update file into a slot file, its crc checked on the way */
static bool copy_kernel(struct fat *src, struct fat_file *uf, uint32_t off, uint32_t size, uint32_t crc, struct fat_file *kf, char *msg, int msg_len) {
    static uint8_t piece[32768];
    uint32_t c = 0xFFFFFFFFu, done = 0;
    while (done < size) {
        uint32_t n = MIN(32768u, size - done);
        if (!fat_read(src, uf, off + done, piece, n)) { snfmt(msg, msg_len, "update: read error"); return false; }
        for (uint32_t i = 0; i < n; i++) { c ^= piece[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
        if (!fat_write_inplace(&disk.fat, kf, done, piece, n)) { snfmt(msg, msg_len, "update: write error"); return false; }
        done += n;
    }
    if (~c != crc) { snfmt(msg, msg_len, "update: bad checksum, not switching"); return false; }
    return true;
}

static bool apply_update(struct fat *src, struct fat_file *uf, const struct upd_hdr *h, bool two, char *msg, int msg_len) {
    struct fat_file conf; static char text[4096];
    if (!fat_find(&disk.fat, "BOOT/LIMINE/LIMINE.CONF", &conf) || conf.size > sizeof text) { snfmt(msg, msg_len, "update: no limine.conf"); return false; }
    if (!fat_read(&disk.fat, &conf, 0, text, conf.size)) return false;
    int li = slot_index(text, conf.size, "hb_");
    if (li < 0) { snfmt(msg, msg_len, "update: limine.conf has no HB_A/HB_B path"); return false; }
    bool a_active = (text[li] | 0x20) == 'a';
    struct fat_file kf;
    if (!fat_find(&disk.fat, a_active ? "BOOT/HB_B.ELF" : "BOOT/HB_A.ELF", &kf) || kf.size < h->size) { snfmt(msg, msg_len, "update: slot file missing or too small"); return false; }
    uint32_t hsize = two ? sizeof *h : 20;
    if (!copy_kernel(src, uf, hsize, h->size, h->crc, &kf, msg, msg_len)) return false;
    int l64 = -1;
    if (two) {
        l64 = slot_index(text, conf.size, "h64_");
        struct fat_file k64;
        if (l64 >= 0) {
            bool a64 = (text[l64] | 0x20) == 'a';
            if (!fat_find(&disk.fat, a64 ? "BOOT/H64_B.ELF" : "BOOT/H64_A.ELF", &k64) || k64.size < h->size64) { snfmt(msg, msg_len, "update: 64-bit slot missing or too small"); return false; }
        } else {                                    /* a 1.0 stick: one HB64.ELF. Make its second slot and point UEFI at it */
            char *at = 0;
            for (uint32_t i = 0; i + 8 <= conf.size && !at; i++) if (!memcmp(text + i, "HB64.ELF", 8)) at = text + i;
            if (!at || h->size64 > KERNEL_SLOT || !fat_create(&disk.fat, "BOOT", "H64_B.ELF", KERNEL_SLOT, &k64)) { snfmt(msg, msg_len, "update: no room for the 64-bit kernel"); return false; }
            memmove(at + 9, at + 8, (size_t)(text + conf.size - (at + 9)));   /* the padding at the end loses a newline */
            memcpy(at, "H64_A.ELF", 9);                                      /* A: flipped to B below */
            l64 = (int)(at - text) + 4;
            li = slot_index(text, conf.size, "hb_");                          /* the lines after it moved */
        }
        if (!copy_kernel(src, uf, hsize + h->size, h->size64, h->crc64, &k64, msg, msg_len)) return false;
        flip(&text[l64]);
    }
    flip(&text[li]);
    if (!fat_write_inplace(&disk.fat, &conf, 0, text, conf.size)) { snfmt(msg, msg_len, "update: could not switch slots"); return false; }
    snfmt(msg, msg_len, "updated to build %u in slot %c%s", h->version, a_active ? 'B' : 'A', two ? ", the 64-bit kernel too" : "");
    logf("disk: %s", msg);
    return true;
}

bool disk_check_update(uint32_t running, char *msg, int msg_len) {
    if (!disk.have_boot_fat) return false;
    int nd = plat_blk_drives();
    for (int d = 0; d < nd; d++) {
        struct part p[4]; int n = read_parts(d, p);
        for (int i = 0; i < n * 2; i++) {                        /* each FAT partition: BARE.UPD, then 1.0-2.2's name */
            if (!is_fat_type(p[i / 2].type)) continue;
            struct fat f; struct fat_file uf; struct upd_hdr h = { { 0 } };
            if (!fat_mount(&f, d, p[i / 2].lba) || !fat_find(&f, i % 2 ? "HOMEBREW.UPD" : "BARE.UPD", &uf)) continue;
            if (uf.size < 20 || !fat_read(&f, &uf, 0, &h, MIN(uf.size, (uint32_t)sizeof h))) continue;
            bool two = !memcmp(h.magic, "HBUPD002", 8);
            if (!two && memcmp(h.magic, "HBUPD001", 8)) continue;
            bool is64 = sizeof(void *) == 8;
            uint32_t v = is64 ? h.version64 : h.version;        /* the build of the kernel that would run here next */
            logf("disk: update file on drive %d: build %u (running %u)", d, v, running);
            if (is64 && !two) { snfmt(msg, msg_len, "the update file has no 64-bit kernel (what UEFI boots)"); continue; }
            if (v <= running || (uint64_t)(two ? sizeof h : 20) + h.size + (two ? h.size64 : 0) > uf.size) continue;
            return apply_update(&f, &uf, &h, two, msg, msg_len);
        }
    }
    return false;
}
