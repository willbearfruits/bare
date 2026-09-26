/* Block I/O through the BIOS (int 13h extensions). Works on whatever the BIOS booted from — USB sticks included.
   Without BIOS services (a 32-bit UEFI boot), or once our own USB stack has taken over, its sticks stand in. */
#include "bios.h"
#include "io.h"
#include "libc.h"
#include "log.h"
#include "platform.h"
#include "drivers/sound.h"
#include "drivers/usb.h"
#include "drivers/blkdev.h"

#define MAX_DRIVES 4
static int ndrives;
static uint8_t drive_id[MAX_DRIVES];
static uint64_t drive_sectors[MAX_DRIVES];
static int boot_biosdev = -1;

struct __attribute__((packed)) dap { uint8_t size, zero; uint16_t count, off, seg; uint64_t lba; };
struct __attribute__((packed)) dparams { uint16_t size, flags; uint32_t cyl, heads, spt; uint64_t sectors; uint16_t bps; };

static bool probe(uint8_t dev, uint64_t *sectors) {
    struct dparams *dp = (struct dparams *)BIOS_DAP_ADDR;
    memset(dp, 0, sizeof *dp); dp->size = 0x1E;
    struct bios_regs r = { .eax = 0x4800, .edx = dev, .esi = BIOS_DAP_ADDR, .ds = 0, .intno = 0x13 };
    bios_call(&r);
    if ((r.flags & 1) || dp->sectors == 0 || dp->bps != 512) return false;
    *sectors = dp->sectors;
    return true;
}

void blkbios_init(int biosdev) {
    ndrives = 0; boot_biosdev = biosdev;
    if (!bios_available()) return;
    /* boot device first, then the rest */
    for (int pass = 0; pass < 2; pass++)
        for (int dev = 0x80; dev < 0x88 && ndrives < MAX_DRIVES; dev++) {
            bool is_boot = dev == biosdev;
            if ((pass == 0) != is_boot) continue;
            uint64_t sec;
            if (probe((uint8_t)dev, &sec)) { drive_id[ndrives] = (uint8_t)dev; drive_sectors[ndrives++] = sec; logf("blk: drive %02x %lu MiB%s", dev, sec / 2048, is_boot ? " (boot)" : ""); }
        }
}

/* our own USB stack, once it runs, has the sticks: the BIOS's USB is gone then. Without the BIOS the internal disks
   (SATA, NVMe: our drivers, only started on a boot without BIOS) follow the sticks. */
static bool bios(void) { return bios_available() && !usb_running(); }
int plat_blk_drives(void) { return bios() ? ndrives : usb_blk_drives() + blkdev_count(); }
void plat_blk_rescan(void) { if (bios()) blkbios_init(boot_biosdev); else usb_blk_rescan(); }
uint64_t plat_blk_sectors(int d) {
    if (bios()) return d < ndrives ? drive_sectors[d] : 0;
    int u = usb_blk_drives();
    return d < u ? usb_blk_sectors(d) : blkdev_get(d - u) ? blkdev_get(d - u)->sectors : 0;
}
static bool own_io(int d, uint64_t lba, uint32_t n, void *buf, bool write) {
    int u = usb_blk_drives();
    return d < u ? usb_blk_io(d, lba, n, buf, write) : blkdev_io(d - u, lba, n, buf, write);
}

static bool xfer(int d, uint64_t lba, uint32_t n, void *buf, bool write) {
    if (d >= ndrives) return false;
    uint8_t *p = buf;
    while (n) {
        sound_prefill();                             /* the audio ring must survive a few ms without our IRQ */
        uint32_t c = n > 64 ? 64 : n;
        if (write) memcpy((void *)BIOS_BUF_ADDR, p, c * 512);
        for (int attempt = 0;; attempt++) {
            /* a failed call leaves the error code in AH and may shrink the packet's count: set both up every time */
            struct dap *dap = (struct dap *)BIOS_DAP_ADDR;
            *dap = (struct dap){ 0x10, 0, (uint16_t)c, 0, BIOS_BUF_ADDR >> 4, lba };
            struct bios_regs r = { .eax = write ? 0x4300 : 0x4200, .edx = drive_id[d], .esi = BIOS_DAP_ADDR, .ds = 0, .intno = 0x13 };
            bios_call(&r);
            if (!(r.flags & 1)) break;
            if (attempt == 1) { logf("blk: %s lba %lu failed ah=%02x", write ? "write" : "read", lba, (r.eax >> 8) & 0xFF); return false; }
            struct bios_regs rr = { .eax = 0x0000, .edx = drive_id[d], .intno = 0x13 }; bios_call(&rr);   /* reset, try once more */
        }
        if (!write) memcpy(p, (void *)BIOS_BUF_ADDR, c * 512);
        p += c * 512; lba += c; n -= c;
    }
    return true;
}
bool plat_blk_read(int d, uint64_t lba, uint32_t n, void *dst) { return bios() ? xfer(d, lba, n, dst, false) : own_io(d, lba, n, dst, false); }
bool plat_blk_write(int d, uint64_t lba, uint32_t n, const void *src) { return bios() ? xfer(d, lba, n, (void *)src, true) : own_io(d, lba, n, (void *)src, true); }

const char *plat_blk_name(int d) {
    static char n[64];
    if (bios()) { snfmt(n, sizeof n, d < ndrives ? "BIOS disk %02xh%s" : "?", d < ndrives ? drive_id[d] : 0, d < ndrives && drive_id[d] == boot_biosdev ? " (started from)" : ""); return n; }
    int u = usb_blk_drives();
    if (d < u) { snfmt(n, sizeof n, "USB: %s", usb_blk_name(d)); return n; }
    return blkdev_get(d - u) ? blkdev_get(d - u)->name : "?";
}
uint32_t plat_boot_disk_id(void) { return 0; }           /* a BIOS boot lists the boot disk first anyway */

void plat_reboot(void) {
    cli();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) ;
    outb(0x64, 0xFE);
    for (;;) hlt();
}
