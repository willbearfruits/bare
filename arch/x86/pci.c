#include "pci.h"
#include "io.h"
#include "log.h"
#include "mem.h"
#include "acpi.h"

static uint32_t addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC);
}
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) { outl(0xCF8, addr(bus, dev, fn, off)); return inl(0xCFC); }
void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) { outl(0xCF8, addr(bus, dev, fn, off)); outl(0xCFC, v); }

int pci_find_all_class(uint8_t class, uint8_t subclass, struct pci_dev *out, int max) {
    int n = 0;
    for (int bus = 0; bus < 32 && n < max; bus++)
        for (int dev = 0; dev < 32 && n < max; dev++) {
            /* function 0 decides whether there are others; but a chipset may switch function 0 off and keep 1-7 (Intel's
               Serial IO I2C controllers), so without it every function is looked at */
            uint32_t id0 = pci_read32(bus, dev, 0, 0);
            int nfn = (id0 & 0xFFFF) == 0xFFFF || (pci_read32(bus, dev, 0, 0x0C) & 0x800000) ? 8 : 1;
            if ((id0 & 0xFFFF) == 0xFFFF && bus) continue;                 /* off bus 0 that does not happen */
            for (int fn = 0; fn < nfn && n < max; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                uint32_t cl = pci_read32(bus, dev, fn, 8);
                if ((cl >> 24) == class && ((cl >> 16) & 0xFF) == subclass)
                    out[n++] = (struct pci_dev){ bus, dev, fn, id & 0xFFFF, id >> 16, cl >> 24, (cl >> 16) & 0xFF, (cl >> 8) & 0xFF };
            }
        }
    return n;
}
bool pci_find_class(uint8_t class, uint8_t subclass, struct pci_dev *out) { return pci_find_all_class(class, subclass, out, 1) == 1; }

uint64_t pci_bar(const struct pci_dev *d, int bar, bool *is_mem) {
    uint32_t lo = pci_read32(d->bus, d->dev, d->fn, 0x10 + bar * 4);
    if (lo & 1) { if (is_mem) *is_mem = false; return lo & ~3u; }
    if (is_mem) *is_mem = true;
    uint64_t a = lo & ~0xFu;
    if (((lo >> 1) & 3) == 2) a |= (uint64_t)pci_read32(d->bus, d->dev, d->fn, 0x14 + bar * 4) << 32;
    return a;
}

/* offset of the power-management capability, 0 if the device has none */
static uint8_t pm_cap(const struct pci_dev *d) {
    if (!(pci_read32(d->bus, d->dev, d->fn, 4) & (0x10u << 16))) return 0;       /* status: no capability list */
    uint8_t p = pci_read32(d->bus, d->dev, d->fn, 0x34) & 0xFC;
    for (int n = 0; p && n < 48; n++) {
        uint32_t h = pci_read32(d->bus, d->dev, d->fn, p);
        if ((h & 0xFF) == 0x01) return p;
        p = (h >> 8) & 0xFC;
    }
    return 0;
}

void pci_wake(const struct pci_dev *d) {
    /* UEFI firmware may leave a device it didn't use in D3 (asleep): its registers then read all ones. Wake it, and put
       back the address registers, which a device may lose on the way to D0. */
    uint8_t pm = pm_cap(d);
    uint32_t csr = pm ? pci_read32(d->bus, d->dev, d->fn, (uint8_t)(pm + 4)) : 0;
    if (!(csr & 3)) return;
    uint32_t bars[6];
    for (int i = 0; i < 6; i++) bars[i] = pci_read32(d->bus, d->dev, d->fn, (uint8_t)(0x10 + i * 4));
    pci_write32(d->bus, d->dev, d->fn, (uint8_t)(pm + 4), csr & ~3u);
    for (int i = 0; i < 10000; i++) io_wait();                              /* ~10 ms: D3 to D0 takes up to 10 */
    for (int i = 0; i < 6; i++) pci_write32(d->bus, d->dev, d->fn, (uint8_t)(0x10 + i * 4), bars[i]);
    logf("pci: %02x:%02x.%d was in D%u, woken", d->bus, d->dev, d->fn, csr & 3);
}

void pci_enable(const struct pci_dev *d) {
    pci_wake(d);
    uint32_t cmd = pci_read32(d->bus, d->dev, d->fn, 4);
    pci_write32(d->bus, d->dev, d->fn, 4, (cmd | 0x6) & ~0x400u);   /* MEM + BUS MASTER, INTx enabled */
}

/* ---- BARs the firmware left empty ----
   UEFI firmware may skip devices its own drivers don't use (Intel's Serial IO I2C controllers on some laptops), leaving
   the address to the OS, which takes it from the PCI window ACPI describes — by running AML. Without that: the window is
   where the host bridge sends addresses to PCI, from the top of DRAM below 4 GiB (Intel: TOLUD, the host bridge's 0xBC;
   else the end of the memory map's RAM) up to PCI configuration space (MCFG). The BAR goes as high in it as it fits,
   clear of every BAR and bridge window on bus 0, the memory map, and the fixed ranges the ACPI tables declare. */

static uint32_t bar_len(const struct pci_dev *d, uint8_t off) {             /* decoding off while the BAR shows its size */
    uint32_t cmd = pci_read32(d->bus, d->dev, d->fn, 4), lo = pci_read32(d->bus, d->dev, d->fn, off);
    pci_write32(d->bus, d->dev, d->fn, 4, cmd & ~3u);
    pci_write32(d->bus, d->dev, d->fn, off, 0xFFFFFFFFu);
    uint32_t m = pci_read32(d->bus, d->dev, d->fn, off) & ~0xFu;
    pci_write32(d->bus, d->dev, d->fn, off, lo);
    pci_write32(d->bus, d->dev, d->fn, 4, cmd);
    return m ? ~m + 1 : 0;                                                  /* 0: 4 GiB or more */
}

/* what on bus 0 overlaps [a, a + len): *start is where the lowest such thing begins */
static bool bus0_uses(const struct pci_dev *self, uint32_t a, uint32_t len, uint64_t *start) {
    bool hit = false; uint64_t lo = ~0ull, end = (uint64_t)a + len;
    for (uint8_t dev = 0; dev < 32; dev++)
        for (uint8_t fn = 0; fn < 8; fn++) {
            if ((pci_read32(0, dev, fn, 0) & 0xFFFF) == 0xFFFF || (!self->bus && self->dev == dev && self->fn == fn)) continue;
            struct pci_dev d = { 0, dev, fn, 0, 0, 0, 0, 0 };
            uint8_t type = (pci_read32(0, dev, fn, 0x0C) >> 16) & 0x7F;
            if (type > 1 || pci_read32(0, dev, fn, 8) >> 16 == 0x0600) continue;   /* a host bridge: nothing there */
            if (type == 1) {                                                /* a bridge's windows hold all behind it */
                uint32_t m = pci_read32(0, dev, fn, 0x20), pm = pci_read32(0, dev, fn, 0x24);
                uint64_t wb[2] = { (m & 0xFFF0) << 16, (uint64_t)(pm & 0xFFF0) << 16 };
                uint64_t wl[2] = { (m & 0xFFF00000) | 0xFFFFF, (uint64_t)(pm & 0xFFF00000) | 0xFFFFF };
                if ((pm & 0xF) == 1) { wb[1] |= (uint64_t)pci_read32(0, dev, fn, 0x28) << 32; wl[1] |= (uint64_t)pci_read32(0, dev, fn, 0x2C) << 32; }
                for (int w = 0; w < 2; w++)
                    if (wb[w] <= wl[w] && wb[w] < end && a <= wl[w]) { hit = true; if (wb[w] < lo) lo = wb[w]; }
            }
            for (int b = 0; b < (type ? 2 : 6); b++) {
                uint8_t off = (uint8_t)(0x10 + b * 4);
                uint32_t v = pci_read32(0, dev, fn, off), hi = 0;
                if (v & 1) continue;                                        /* I/O */
                if (((v >> 1) & 3) == 2) { hi = pci_read32(0, dev, fn, (uint8_t)(off + 4)); b++; }
                uint32_t base = v & ~0xFu;
                if (hi || !base || base >= end) continue;
                /* a BAR is aligned to its size, so it ends by base + the lowest bit set in base; sized only if that isn't enough */
                uint64_t top = (uint64_t)base + (base & (~base + 1));
                if (top > a) { uint32_t n = bar_len(&d, off); top = n ? (uint64_t)base + n : ~0ull; }
                if (top > a) { hit = true; if (base < lo) lo = base; }
            }
        }
    if (hit) *start = lo;
    return hit;
}

static uint32_t ram_top(void) {                                            /* the top of DRAM below 4 GiB */
    uint32_t host = pci_read32(0, 0, 0, 0);
    uint32_t tolud = (host & 0xFFFF) == 0x8086 ? pci_read32(0, 0, 0, 0xBC) & 0xFFF00000u : 0;
    if (tolud >= (256u << 20)) return tolud;
    uint64_t top = (pmm_ram_top32() + 0xFFFFF) & ~0xFFFFFull;               /* else where the memory map's RAM ends */
    return top >> 32 ? 0xFFF00000u : (uint32_t)top;
}

uint64_t pci_place_bar(const struct pci_dev *d, int bar) {
    uint8_t off = (uint8_t)(0x10 + bar * 4);
    uint32_t v = pci_read32(d->bus, d->dev, d->fn, off);
    if (v & 1) return 0;                                                    /* I/O */
    uint64_t ecam = acpi_ecam();
    uint32_t bottom = ram_top(), len = bar_len(d, off);
    if (!len || len > (16u << 20)) return 0;
    if (!ecam || ecam > 0xFEC00000u || ecam <= bottom) { logf("pci: %02x:%02x.%d: BAR %d is empty, and where PCI addresses go isn't known (RAM to %x, configuration space at %lx)", d->bus, d->dev, d->fn, bar, bottom, ecam); return 0; }
    if (len < 4096) len = 4096;                                            /* a page of its own */
    uint32_t top = (uint32_t)ecam;
    for (uint32_t a = (top - len) & ~(len - 1); a >= bottom && a < top;) {
        uint64_t s = 0;
        if (bus0_uses(d, a, len, &s) || acpi_fixed_mmio(a, len, &s) || pmm_claimed(a, len, &s)) {
            if (s < (uint64_t)bottom + len) break;
            a = ((uint32_t)s - len) & ~(len - 1);
            continue;
        }
        pci_write32(d->bus, d->dev, d->fn, off, a | (v & 0xF));
        if (((v >> 1) & 3) == 2) pci_write32(d->bus, d->dev, d->fn, (uint8_t)(off + 4), 0);
        logf("pci: %02x:%02x.%d: BAR %d was empty, now at %x (%u KiB; PCI's window %x-%x)", d->bus, d->dev, d->fn, bar, a, len >> 10, bottom, top - 1);
        return a;
    }
    logf("pci: %02x:%02x.%d: BAR %d is empty and no room was found for it between %x and %x", d->bus, d->dev, d->fn, bar, bottom, top);
    return 0;
}
