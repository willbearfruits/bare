#include "install.h"
#include "disk.h"
#include "fat.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

struct install_state inst;
#define CHUNK 128                                     /* sectors a piece: 64 KiB */
static uint8_t buf[CHUNK * 512], sec[512], mbr[512];
enum { ST_BOOT, ST_TAPE, ST_FINISH };
static int stage;
static uint64_t at, end, fat_end, tape_used, target_sectors, disk_end;   /* target: as the MBR can say it; disk_end: really */

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

void install_describe(int d, char *out, int cap) {
    if (!plat_blk_read(d, 0, 1, sec)) { snfmt(out, cap, "can't be read"); return; }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        bool zero = true; for (int i = 0; i < 512; i++) zero &= !sec[i];
        snfmt(out, cap, zero ? "empty" : "no partition table"); return;
    }
    int n = 0; bool gpt = false, ours = false;
    for (int i = 0; i < 4; i++) { uint8_t t = sec[446 + i * 16 + 4]; if (t) n++; gpt |= t == 0xEE; ours |= t == 0x7F; }
    if (gpt && plat_blk_read(d, 1, 1, sec) && !memcmp(sec, "EFI PART", 8)) {            /* count the GPT's partitions */
        uint64_t lba = rd32(sec + 72); uint32_t count = MIN(rd32(sec + 80), 128u), size = rd32(sec + 84), parts = 0;
        if (size == 128) for (uint32_t s = 0; s < (count + 3) / 4 && plat_blk_read(d, lba + s, 1, sec); s++)
            for (int e = 0; e < 4 && s * 4 + (uint32_t)e < count; e++) { bool used = false; for (int b = 0; b < 16; b++) used |= sec[e * 128 + b] != 0; parts += used; }
        snfmt(out, cap, parts == 1 ? "GPT, 1 partition" : "GPT, %u partitions", parts);
        return;
    }
    if (ours && d != disk.boot_drive) {
        struct fat f; struct fat_file fi; struct { uint8_t type; uint64_t lba; } p[4]; int np = 0;
        if (plat_blk_read(d, 0, 1, sec)) for (int i = 0; i < 4; i++) if (sec[446 + i * 16 + 4]) { p[np].type = sec[446 + i * 16 + 4]; p[np++].lba = rd32(sec + 446 + i * 16 + 8); }
        for (int i = 0; i < np; i++) if (p[i].type == 0x0C && fat_mount(&f, d, p[i].lba) && fat_find(&f, "BOOT/HB_A.ELF", &fi)) { snfmt(out, cap, "BARE! is installed"); return; }
    }
    snfmt(out, cap, n == 1 ? "MBR, 1 partition" : "MBR, %d partitions", n);
}

static bool fail(const char *what, uint64_t lba) {
    snfmt(inst.status, sizeof inst.status, "install failed: %s at sector %lu", what, lba);
    logf("install: %s", inst.status);
    inst.running = false; inst.failed = true;
    return false;
}

bool install_start(int d) {
    memset(&inst, 0, sizeof inst);
    inst.target = d;
    if (!disk.have_boot_fat || !disk.have_tape) { snfmt(inst.status, sizeof inst.status, "not running from a BARE! stick: nothing to copy"); return false; }
    if (d == disk.boot_drive || d < 0 || d >= plat_blk_drives()) { snfmt(inst.status, sizeof inst.status, "that is the stick itself"); return false; }
    if (!plat_blk_read(disk.boot_drive, 0, 1, mbr)) { snfmt(inst.status, sizeof inst.status, "the stick can't be read"); return false; }
    fat_end = 0;
    for (int i = 0; i < 4; i++) if (rd32(mbr + 446 + i * 16 + 8) == disk.fat_lba) fat_end = disk.fat_lba + rd32(mbr + 446 + i * 16 + 12);
    if (!fat_end || fat_end > disk.tape_lba) { snfmt(inst.status, sizeof inst.status, "the stick's partitions are not the usual ones"); return false; }
    tape_used = 1 + (uint64_t)disk.tape.slots * disk.tape.slot_sectors;              /* the header, the slots, their frames */
    for (uint32_t i = 0; i < disk.tape.slots && i < TAPE_MAX_SLOTS; i++)
        if (disk.slot_used[i] && disk.slot[i].version >= 3 && disk.slot[i].ext_sectors)
            tape_used = MAX(tape_used, (uint64_t)disk.slot[i].ext_start + disk.slot[i].ext_sectors);
    disk_end = plat_blk_sectors(d); target_sectors = MIN(disk_end, 0xFFFFFFFFull);
    if (target_sectors < disk.tape_lba + tape_used + 4096) {
        snfmt(inst.status, sizeof inst.status, "too small: it needs %lu MiB", (disk.tape_lba + tape_used + 4096) >> 11); return false;
    }
    inst.total = (fat_end - 1) + tape_used + 34;
    stage = ST_BOOT; at = 1; end = fat_end;                            /* sector 0, the partition table, goes last */
    inst.running = true;
    snfmt(inst.status, sizeof inst.status, "copying the boot partition");
    logf("install: onto drive %d: sectors 1-%lu, then %lu of the project partition at %lu; %lu sectors in all", d, fat_end - 1, tape_used, disk.tape_lba, target_sectors);
    return true;
}

void install_cancel(void) {
    if (!inst.running) return;
    inst.running = false; inst.failed = true;
    snfmt(inst.status, sizeof inst.status, "stopped: the disk is only partly written, and won't start");
    logf("install: %s", inst.status);
}

static void finish(void) {
    /* the project partition's header, told how big it is now */
    if (!plat_blk_read(inst.target, disk.tape_lba, 1, sec)) { fail("reading the project header", disk.tape_lba); return; }
    struct tape_hdr h; memcpy(&h, sec, sizeof h);
    h.part_sectors = (uint32_t)(target_sectors - disk.tape_lba);
    memcpy(sec, &h, sizeof h);
    if (!plat_blk_write(inst.target, disk.tape_lba, 1, sec)) { fail("writing the project header", disk.tape_lba); return; }
    /* a GPT left at the end of the disk (its backup copy) goes */
    memset(buf, 0, 34 * 512);
    if (!plat_blk_write(inst.target, disk_end - 34, 34, buf)) { fail("clearing the end of the disk", disk_end - 34); return; }
    /* the partition table: the stick's, the project partition to the end, a disk signature of its own */
    for (int i = 0; i < 4; i++) {
        uint8_t *e = mbr + 446 + i * 16;
        if (e[4] == 0x7F && rd32(e + 8) == disk.tape_lba) { wr32(e + 12, (uint32_t)(target_sectors - disk.tape_lba)); e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF; }
    }
    uint32_t sig = (uint32_t)plat_ms() * 2654435761u ^ 0x42480000u;
    wr32(mbr + 440, sig ? sig : 0x42480001u); mbr[444] = mbr[445] = 0;
    if (!plat_blk_write(inst.target, 0, 1, mbr) || !plat_blk_read(inst.target, 0, 1, sec) || memcmp(sec, mbr, 512)) { fail("writing the partition table", 0); return; }
    struct fat f; struct fat_file fi;                                   /* and it reads as a BARE! disk */
    if (!fat_mount(&f, inst.target, disk.fat_lba) || !fat_find(&f, "BOOT/HB_A.ELF", &fi)) { fail("checking the copy", disk.fat_lba); return; }
    inst.running = false; inst.done = true;
    snfmt(inst.status, sizeof inst.status, "installed: take the stick out and restart (Enter)");
    logf("install: done, disk signature %08x", sig);
}

void install_work(void) {
    if (!inst.running) return;
    uint64_t t0 = plat_ms();
    for (int pieces = 0; inst.running && plat_ms() - t0 < 25 && pieces < 32; pieces++) {   /* ~25 ms, 2 MiB at most, a pass */
        if (stage == ST_FINISH) { finish(); return; }
        if (at >= end) {
            if (stage == ST_BOOT) { stage = ST_TAPE; at = disk.tape_lba; end = disk.tape_lba + tape_used; snfmt(inst.status, sizeof inst.status, "copying the projects"); }
            else stage = ST_FINISH;
            continue;
        }
        uint32_t n = (uint32_t)MIN(end - at, (uint64_t)CHUNK);
        if (!plat_blk_read(disk.boot_drive, at, n, buf)) { fail("reading the stick", at); return; }
        if (!plat_blk_write(inst.target, at, n, buf)) { fail("writing the disk", at); return; }
        at += n; inst.copied += n;
    }
}
