/* SATA disks through AHCI, polled. The controller is taken from the firmware (BIOS/OS handoff), each port's command
   list and received-FIS area moved to our memory, and the disks on it identified; reads and writes go through one
   command slot and a 64 KiB bounce buffer (READ/WRITE DMA EXT). Optical drives and port multipliers are left alone. */
#include "blkdev.h"
#include "pci.h"
#include "mem.h"
#include "io.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

enum { CAP = 0x00, GHC = 0x04, PI = 0x0C, CAP2 = 0x24, BOHC = 0x28 };
enum { PxCLB = 0x00, PxCLBU = 0x04, PxFB = 0x08, PxFBU = 0x0C, PxIS = 0x10, PxIE = 0x14, PxCMD = 0x18, PxTFD = 0x20,
       PxSIG = 0x24, PxSSTS = 0x28, PxSCTL = 0x2C, PxSERR = 0x30, PxCI = 0x38 };
#define CMD_ST   (1u << 0)
#define CMD_SUD  (1u << 1)
#define CMD_POD  (1u << 2)
#define CMD_FRE  (1u << 4)
#define CMD_FR   (1u << 14)
#define CMD_CR   (1u << 15)
#define IS_TFES  (1u << 30)
#define CHUNK    128                                   /* sectors: the bounce buffer, 64 KiB */

struct port { volatile uint8_t *r; uint8_t *mem; uint64_t phys; int ctl, num; };   /* mem: list 0, FIS 1024, table 1280 */
static struct port ports[BLKDEVS];
static int nports;
static uint8_t *bounce; static uint64_t bounce_phys;

static uint32_t rd(const struct port *p, uint32_t o) { return mmio_r32(p->r + o); }
static void wr(const struct port *p, uint32_t o, uint32_t v) { mmio_w32(p->r + o, v); }
static bool wait_bits(volatile uint8_t *reg, uint32_t mask, uint32_t want, uint32_t ms) {
    uint64_t end = pit_ticks() + ms;
    do { if ((mmio_r32(reg) & mask) == want) return true; timer_poll(); } while (pit_ticks() < end);
    return (mmio_r32(reg) & mask) == want;
}

/* one command in slot 0: the FIS, the buffer (the bounce, or none), and a wait for it to finish */
static bool command(struct port *p, uint8_t cmd, uint64_t lba, uint32_t count, bool write, uint32_t bytes, uint32_t ms) {
    uint32_t *hdr = (uint32_t *)p->mem;
    uint8_t *tbl = p->mem + 1280;
    memset(tbl, 0, 128 + 16);
    tbl[0] = 0x27; tbl[1] = 0x80; tbl[2] = cmd;                         /* register FIS, host to device: a command */
    tbl[4] = (uint8_t)lba; tbl[5] = (uint8_t)(lba >> 8); tbl[6] = (uint8_t)(lba >> 16); tbl[7] = 0x40;   /* LBA mode */
    tbl[8] = (uint8_t)(lba >> 24); tbl[9] = (uint8_t)(lba >> 32); tbl[10] = (uint8_t)(lba >> 40);
    tbl[12] = (uint8_t)count; tbl[13] = (uint8_t)(count >> 8);
    uint32_t *prd = (uint32_t *)(tbl + 128);
    if (bytes) { prd[0] = (uint32_t)bounce_phys; prd[1] = (uint32_t)(bounce_phys >> 32); prd[3] = bytes - 1; }
    uint64_t tphys = p->phys + 1280;
    hdr[0] = 5 | (write ? 1u << 6 : 0) | (bytes ? 1u << 16 : 0);        /* a 5-dword FIS, one PRD entry */
    hdr[1] = 0; hdr[2] = (uint32_t)tphys; hdr[3] = (uint32_t)(tphys >> 32);
    wr(p, PxIS, 0xFFFFFFFFu);
    barrier();
    wr(p, PxCI, 1);
    uint64_t end = pit_ticks() + ms;
    while (rd(p, PxCI) & 1) {
        if (rd(p, PxIS) & IS_TFES) { logf("ahci: port %d: command %02x failed (task file %02x)", p->num, cmd, rd(p, PxTFD) & 0xFF); return false; }
        if (pit_ticks() > end) { logf("ahci: port %d: command %02x timed out", p->num, cmd); return false; }
        timer_poll();
    }
    return !(rd(p, PxTFD) & 0x01);
}

static bool port_io(struct blkdev *d, uint64_t lba, uint32_t n, void *buf, bool write) {
    struct port *p = d->ctx;
    uint8_t *at = buf;
    while (n) {
        uint32_t c = MIN(n, (uint32_t)CHUNK);
        if (write) memcpy(bounce, at, c * 512);
        if (!command(p, write ? 0x35 : 0x25, lba, c, write, c * 512, 5000)) { logf("ahci: port %d: %s at %lu failed", p->num, write ? "write" : "read", lba); return false; }
        if (!write) memcpy(at, bounce, c * 512);
        at += c * 512; lba += c; n -= c;
    }
    return true;
}

/* a port from the firmware's hands into ours: stopped, our buffers, started; true if a disk answers on it */
static bool port_start(struct port *p) {
    uint32_t cmd = rd(p, PxCMD);
    if (cmd & (CMD_ST | CMD_CR)) { wr(p, PxCMD, rd(p, PxCMD) & ~CMD_ST); if (!wait_bits(p->r + PxCMD, CMD_CR, 0, 500)) return false; }
    if (cmd & (CMD_FRE | CMD_FR)) { wr(p, PxCMD, rd(p, PxCMD) & ~CMD_FRE); if (!wait_bits(p->r + PxCMD, CMD_FR, 0, 500)) return false; }
    wr(p, PxCLB, (uint32_t)p->phys); wr(p, PxCLBU, (uint32_t)(p->phys >> 32));
    wr(p, PxFB, (uint32_t)(p->phys + 1024)); wr(p, PxFBU, (uint32_t)((p->phys + 1024) >> 32));
    wr(p, PxIE, 0); wr(p, PxSERR, 0xFFFFFFFFu); wr(p, PxIS, 0xFFFFFFFFu);
    wr(p, PxCMD, rd(p, PxCMD) | CMD_FRE | CMD_SUD | CMD_POD);
    if ((rd(p, PxSSTS) & 0xF) != 3) {                                   /* no link yet: a COMRESET */
        wr(p, PxSCTL, (rd(p, PxSCTL) & ~0xFu) | 1); sleep_ms(2);
        wr(p, PxSCTL, rd(p, PxSCTL) & ~0xFu);
        if (!wait_bits(p->r + PxSSTS, 0xF, 3, 300)) return false;
        wr(p, PxSERR, 0xFFFFFFFFu);
    }
    if (!wait_bits(p->r + PxTFD, 0x88, 0, 3000)) { logf("ahci: port %d: the disk stays busy", p->num); return false; }   /* BSY, DRQ */
    wr(p, PxCMD, rd(p, PxCMD) | CMD_ST);
    return true;
}

static void ata_string(const uint16_t *w, int n, char *out, int cap) {  /* IDENTIFY strings: byte pairs swapped */
    int k = 0;
    for (int i = 0; i < n && k + 2 < cap; i++) { out[k++] = (char)(w[i] >> 8); out[k++] = (char)w[i]; }
    out[k] = 0;
    while (k && out[k - 1] == ' ') out[--k] = 0;
}

static void controller(const struct pci_dev *pd, int ctl) {
    bool mem; uint64_t abar = pci_bar(pd, 5, &mem);
    if (!mem || !abar || (sizeof(void *) == 4 && (abar >> 32))) { logf("ahci: %02x:%02x.%d: no usable registers (%lx)", pd->bus, pd->dev, pd->fn, abar); return; }
    pci_enable(pd);
    volatile uint8_t *hba = mmio_map(abar, 0x1100);
    if (mmio_r32(hba + CAP2) & 1) {                                     /* BIOS/OS handoff */
        mmio_w32(hba + BOHC, mmio_r32(hba + BOHC) | 2);
        wait_bits(hba + BOHC, 1, 0, 25);
        if (mmio_r32(hba + BOHC) & 0x10) wait_bits(hba + BOHC, 0x10, 0, 2000);
    }
    mmio_w32(hba + GHC, (mmio_r32(hba + GHC) | 1u << 31) & ~2u);        /* AHCI mode, interrupts off */
    uint32_t pi = mmio_r32(hba + PI);
    logf("ahci: %02x:%02x.%d %04x:%04x, %u ports (%08x)", pd->bus, pd->dev, pd->fn, pd->vendor, pd->device, (mmio_r32(hba + CAP) & 31) + 1, pi);
    for (int n = 0; n < 32 && nports < BLKDEVS; n++) {
        if (!(pi >> n & 1)) continue;
        struct port *p = &ports[nports];
        p->r = hba + 0x100 + 0x80 * n; p->ctl = ctl; p->num = n;
        uint32_t det = rd(p, PxSSTS) & 0xF, sig = rd(p, PxSIG);
        if (det != 3 && det != 1) continue;                             /* nothing there */
        if (!p->mem) p->mem = dma_alloc(4096, &p->phys);
        if (!port_start(p)) { logf("ahci: port %d: no disk came up", n); continue; }
        sig = rd(p, PxSIG);
        if (sig != 0x00000101) { logf("ahci: port %d: %s, left alone", n, sig == 0xEB140101 ? "an optical drive" : sig == 0x96690101 ? "a port multiplier" : "not a disk"); continue; }
        if (!command(p, 0xEC, 0, 0, false, 512, 3000)) continue;       /* IDENTIFY DEVICE */
        const uint16_t *id = (const uint16_t *)bounce;
        uint64_t sectors = (id[83] & (1u << 10)) ? (uint64_t)id[100] | (uint64_t)id[101] << 16 | (uint64_t)id[102] << 32 | (uint64_t)id[103] << 48
                                                  : (uint32_t)(id[60] | id[61] << 16);
        bool big = (id[106] & 0xD000) == 0x5000 && (id[106] & (1u << 12)) && (id[117] | (uint32_t)id[118] << 16) != 256;
        char model[41]; ata_string(id + 27, 20, model, sizeof model);
        if (!sectors || big || !(id[83] & (1u << 10))) { logf("ahci: port %d: %s: %s", n, model, big ? "sectors other than 512 bytes" : "no 48-bit addressing"); continue; }
        struct blkdev *d = blkdev_add();
        if (!d) break;
        snfmt(d->name, sizeof d->name, "SATA %d: %s", n, model);
        d->sectors = sectors; d->io = port_io; d->ctx = p;
        nports++;
    }
}

int ahci_init(void) {
    struct pci_dev devs[4];
    int n = pci_find_all_class(0x01, 0x06, devs, 4), before = nports;
    for (int i = 0; i < n; i++) {
        if (devs[i].prog_if != 1) continue;                            /* AHCI (not IDE-compatible mode) */
        if (!bounce) bounce = dma_alloc(CHUNK * 512, &bounce_phys);
        controller(&devs[i], i);
    }
    return nports - before;
}
