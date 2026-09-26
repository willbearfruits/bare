/* Block storage on the 64-bit build: no BIOS from long mode. USB sticks through our own USB stack, then the internal
   disks (SATA, NVMe) — the numbers of the sticks stay put until a rescan, the internal disks follow them. */
#include "platform.h"
#include "io.h"
#include "drivers/usb.h"
#include "drivers/blkdev.h"
#include "libc.h"
int plat_blk_drives(void) { return usb_blk_drives() + blkdev_count(); }
void plat_blk_rescan(void) { usb_blk_rescan(); }
uint64_t plat_blk_sectors(int d) { int u = usb_blk_drives(); return d < u ? usb_blk_sectors(d) : blkdev_get(d - u) ? blkdev_get(d - u)->sectors : 0; }
static bool io(int d, uint64_t lba, uint32_t n, void *buf, bool write) {
    int u = usb_blk_drives();
    return d < u ? usb_blk_io(d, lba, n, buf, write) : blkdev_io(d - u, lba, n, buf, write);
}
bool plat_blk_read(int d, uint64_t lba, uint32_t n, void *dst) { return io(d, lba, n, dst, false); }
bool plat_blk_write(int d, uint64_t lba, uint32_t n, const void *src) { return io(d, lba, n, (void *)src, true); }
const char *plat_blk_name(int d) {
    static char n[64]; int u = usb_blk_drives();
    if (d < u) { snfmt(n, sizeof n, "USB: %s", usb_blk_name(d)); return n; }
    return blkdev_get(d - u) ? blkdev_get(d - u)->name : "?";
}
uint32_t boot_disk_id;                                   /* from Limine (entry.c) */
uint32_t plat_boot_disk_id(void) { return boot_disk_id; }
void plat_reboot(void) { cli(); for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) { } outb(0x64, 0xFE); for (;;) hlt(); }
