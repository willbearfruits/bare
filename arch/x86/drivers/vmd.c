/* Intel VMD ("Volume Management Device", the RST/RAID setting of laptops from Tiger Lake on): the NVMe drives sit in a
   PCI domain of their own behind it. Its configuration space is memory at BAR 0, 1 MiB a bus, starting at bus 0, 128
   or 224 (VMCAP/VMCONFIG); the devices' registers are in its two memory windows at the addresses the firmware gave
   them (or offset, where the VMD tells so in its "SHDW" capability). The drives are then plain NVMe (nvme.c).
   Interrupts would be remapped through VMD: we poll, so they don't matter. */
#include "blkdev.h"
#include "nvme.h"
#include "pci.h"
#include "mem.h"
#include "io.h"
#include "libc.h"
#include "log.h"

static const uint16_t vmd_ids[] = { 0x9A0B, 0x467F, 0x4C3D, 0xA77F, 0x7D0B, 0xAD0B, 0xB60B, 0xB06F };   /* client VMDs */

struct domain { uint64_t cfgbar; uint32_t start, buses, next_bus; uint64_t off1, off2, m1, m1_end, m2, m2_end, m1_free; char name[12]; int found; };

static volatile uint8_t *cfg_of(struct domain *d, uint32_t rel, int dev, int fn) {
    static volatile uint8_t *bus_map[256]; static uint64_t mapped_bar;
    if (mapped_bar != d->cfgbar) { memset((void *)bus_map, 0, sizeof bus_map); mapped_bar = d->cfgbar; }
    if (rel >= d->buses) return 0;
    if (!bus_map[rel]) bus_map[rel] = mmio_map(d->cfgbar + ((uint64_t)rel << 20), 1u << 20);
    return bus_map[rel] + ((uint32_t)dev << 15) + ((uint32_t)fn << 12);
}

/* The firmware normally leaves the domain set up (it boots from these drives). Where it didn't: a drive's registers
   get room at the top of the first window, and the port above it a window around them. */
static uint64_t placed_lo, placed_hi;                                /* what was given out, for the ports' windows */
static uint64_t place_bar(struct domain *d, volatile uint8_t *c, bool is64) {
    uint32_t cmd = mmio_r32(c + 4);
    mmio_w32(c + 4, cmd & ~7u);
    mmio_w32(c + 0x10, 0xFFFFFFFFu);
    uint32_t sz = ~(mmio_r32(c + 0x10) & ~0xFu) + 1;
    if (!sz || sz > (8u << 20)) { mmio_w32(c + 0x10, 0); mmio_w32(c + 4, cmd); return 0; }
    if (!d->m1_free) d->m1_free = d->m1_end;
    uint64_t a = (d->m1_free - sz) & ~(uint64_t)(sz - 1);
    if (a < d->m1 || a >> 32) { mmio_w32(c + 0x10, 0); mmio_w32(c + 4, cmd); return 0; }
    d->m1_free = a;
    mmio_w32(c + 0x10, (uint32_t)a | (is64 ? 4 : 0));
    if (is64) mmio_w32(c + 0x14, 0);
    mmio_w32(c + 4, cmd | 6);
    if (!placed_lo || a < placed_lo) placed_lo = a;
    if (a + sz > placed_hi) placed_hi = a + sz;
    logf("vmd: %s: a drive's registers placed at %lx (%u KiB)", d->name, a, sz >> 10);
    return a;
}
static bool place_window(struct domain *d, volatile uint8_t *c, uint32_t rel) {
    (void)d; (void)rel;
    uint32_t w = mmio_r32(c + 0x20);
    uint32_t base = (w & 0xFFF0) << 16, limit = (w & 0xFFF00000) | 0xFFFFF;
    if (!placed_hi || (base <= limit && base <= placed_lo && placed_hi - 1 <= limit)) return true;   /* already covers them */
    uint32_t nb = (uint32_t)placed_lo & 0xFFF00000, nl = (uint32_t)(placed_hi - 1) & 0xFFF00000;
    mmio_w32(c + 0x20, nl | nb >> 16);
    placed_lo = placed_hi = 0;
    return true;
}

static void scan(struct domain *d, uint32_t rel, int depth) {
    if (depth > 4) return;
    for (int dev = 0; dev < 32; dev++)
        for (int fn = 0; fn < 8; fn++) {
            volatile uint8_t *c = cfg_of(d, rel, dev, fn);
            if (!c) return;
            uint32_t id = mmio_r32(c);
            if ((id & 0xFFFF) == 0xFFFF || !id) { if (!fn) break; continue; }
            uint32_t cls = mmio_r32(c + 8), hdr = (mmio_r32(c + 0x0C) >> 16) & 0xFF;
            logf("vmd: %s: %02x:%02x.%d %04x:%04x class %06x", d->name, d->start + rel, dev, fn, id & 0xFFFF, id >> 16, cls >> 8);
            if ((hdr & 0x7F) == 1) {                                     /* a root port: what is behind it */
                mmio_w32(c + 4, mmio_r32(c + 4) | 6);
                uint32_t bn = mmio_r32(c + 0x18), sec = (bn >> 8) & 0xFF;
                if (sec <= d->start + rel && d->next_bus < d->buses) {        /* no bus behind it yet: give it the next */
                    sec = d->start + d->next_bus++;
                    mmio_w32(c + 0x18, (bn & 0xFF000000u) | sec << 16 | sec << 8 | (d->start + rel));
                    logf("vmd: %s: bus %02x given to the port at %02x:%02x.%d", d->name, sec, d->start + rel, dev, fn);
                }
                if (sec > d->start + rel) { d->next_bus = MAX(d->next_bus, sec - d->start + 1); scan(d, sec - d->start, depth + 1); }
                if (!place_window(d, c, rel)) continue;
            } else if ((cls >> 8) == 0x010802) {                          /* an NVMe drive */
                struct nvme_fn nf = { .cfg = c };
                uint32_t lo = mmio_r32(c + 0x10), hi = (lo & 6) == 4 ? mmio_r32(c + 0x14) : 0;
                uint64_t bar = (uint64_t)hi << 32 | (lo & ~0xFu);
                if (!bar && !(lo & 1)) bar = place_bar(d, c, (lo & 6) == 4);   /* no address yet: one in the first window */
                nf.bar_offset = bar >= d->m1 && bar < d->m1_end ? d->off1 : bar >= d->m2 && bar < d->m2_end ? d->off2 : 0;
                snfmt(nf.name, sizeof nf.name, "%s/%02x:%02x.%d", d->name, d->start + rel, dev, fn);
                d->found += nvme_add(&nf);
            }
            if (!fn && !(hdr & 0x80)) break;                              /* one function */
        }
}

int vmd_init(void) {
    struct pci_dev devs[8];
    int found = 0;
    for (int cls = 0; cls < 2; cls++) {                                  /* shown as a RAID controller or as a system device */
        int n = cls ? pci_find_all_class(0x08, 0x80, devs, 8) : pci_find_all_class(0x01, 0x04, devs, 8);
        for (int i = 0; i < n; i++) {
            bool vmd = false;
            for (unsigned k = 0; k < ARRAY_LEN(vmd_ids); k++) vmd |= devs[i].vendor == 0x8086 && devs[i].device == vmd_ids[k];
            if (!vmd) continue;
            struct pci_dev *pd = &devs[i];
            struct domain d; memset(&d, 0, sizeof d);
            snfmt(d.name, sizeof d.name, "%02x:%02x.%d", pd->bus, pd->dev, pd->fn);
            bool mem;
            d.cfgbar = pci_bar(pd, 0, &mem); d.m1 = pci_bar(pd, 2, &mem); d.m2 = pci_bar(pd, 4, &mem);
            if (!d.cfgbar) { logf("vmd: %s: no configuration window", d.name); continue; }
            if (sizeof(void *) == 4 && (d.cfgbar >> 32)) { logf("vmd: %s: its window is above 4 GiB, out of this 32-bit build's reach", d.name); continue; }
            pci_enable(pd);
            uint32_t vmcap = pci_read32(pd->bus, pd->dev, pd->fn, 0x40) & 0xFFFF, vmcfg = pci_read32(pd->bus, pd->dev, pd->fn, 0x44) & 0xFFFF;
            uint32_t restrict_ = vmcap & 1 ? (vmcfg >> 8) & 3 : 0;
            d.start = restrict_ == 1 ? 128 : restrict_ == 2 ? 224 : 0;
            d.buses = restrict_ == 2 ? 32 : restrict_ == 1 ? 128 : 256;
            d.m1_end = d.m1 + (32u << 20); d.m2_end = d.m2 + (1ull << 32);
            /* where the devices' own view of the windows differs from ours (a vendor capability "SHDW") */
            uint8_t cp = pci_read32(pd->bus, pd->dev, pd->fn, 0x34) & 0xFC;
            for (int k = 0; cp && k < 48; k++) {
                uint32_t h = pci_read32(pd->bus, pd->dev, pd->fn, cp);
                if ((h & 0xFF) == 0x09 && pci_read32(pd->bus, pd->dev, pd->fn, (uint8_t)(cp + 4)) == 0x53484457) {
                    uint64_t s1 = (uint64_t)pci_read32(pd->bus, pd->dev, pd->fn, (uint8_t)(cp + 12)) << 32 | (pci_read32(pd->bus, pd->dev, pd->fn, (uint8_t)(cp + 8)) & ~0xFu);
                    uint64_t s2 = (uint64_t)pci_read32(pd->bus, pd->dev, pd->fn, (uint8_t)(cp + 20)) << 32 | (pci_read32(pd->bus, pd->dev, pd->fn, (uint8_t)(cp + 16)) & ~0xFu);
                    d.off1 = d.m1 - s1; d.off2 = d.m2 - s2;
                    d.m1 = s1; d.m1_end = s1 + (32u << 20); d.m2 = s2; d.m2_end = s2 + (1ull << 32);
                    break;
                }
                cp = (h >> 8) & 0xFC;
            }
            logf("vmd: %s %04x: configuration at %lx (buses from %u), windows %lx and %lx%s", d.name, pd->device, d.cfgbar, d.start,
                 d.m1, d.m2, d.off1 || d.off2 ? ", offset" : "");
            d.next_bus = 1;
            scan(&d, 0, 0);
            if (!d.found) logf("vmd: %s: no NVMe drive behind it", d.name);
            found += d.found;
        }
    }
    return found;
}
