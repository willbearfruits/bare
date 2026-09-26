/* NVMe, polled. The controller is reset and enabled with an admin queue pair, its first namespace identified, and one
   I/O queue pair made; reads and writes go through a 64 KiB bounce buffer described by a PRP list. A namespace with
   4 KiB blocks is read and written around the 512-byte sectors the rest of the system uses (reading the blocks a
   partial write touches first). A controller behind Intel's VMD is reached the same way once vmd.c has found it: only
   its configuration space is elsewhere. */
#include "blkdev.h"
#include "nvme.h"
#include "pci.h"
#include "mem.h"
#include "io.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

#define QSIZE 16
#define BOUNCE (64u << 10)
enum { R_CAP = 0x00, R_VS = 0x08, R_CC = 0x14, R_CSTS = 0x1C, R_AQA = 0x24, R_ASQ = 0x28, R_ACQ = 0x30 };
enum { PG_ASQ, PG_ACQ, PG_IOSQ, PG_IOCQ, PG_ID, PG_PRP, PAGES };

struct ctl {
    volatile uint8_t *r; uint32_t stride, timeout_ms;
    uint8_t *mem; uint64_t phys;
    uint16_t sq_tail[2], cq_head[2], cid; uint8_t phase[2];
    uint32_t nsid, shift, max_bytes;                    /* the namespace, its block size (log2), the largest transfer */
    char name[16];
};
#define CTLS 4
static struct ctl ctls[CTLS];
static int nctl;
static uint8_t *bounce; static uint64_t bounce_phys;

static uint32_t rd(const struct ctl *c, uint32_t o) { return mmio_r32(c->r + o); }
static void wr(const struct ctl *c, uint32_t o, uint32_t v) { mmio_w32(c->r + o, v); }
static uint8_t *page(const struct ctl *c, int i) { return c->mem + i * 4096; }
static uint64_t page_phys(const struct ctl *c, int i) { return c->phys + (uint64_t)i * 4096; }

/* one command on queue q (0 admin, 1 I/O), waited for: its status, 0 = success, -1 = no answer */
static int submit(struct ctl *c, int q, uint32_t *sqe, uint32_t ms) {
    uint32_t *sq = (uint32_t *)page(c, q ? PG_IOSQ : PG_ASQ), *cq = (uint32_t *)page(c, q ? PG_IOCQ : PG_ACQ);
    sqe[0] = (sqe[0] & 0xFFFF) | (uint32_t)(++c->cid) << 16;
    memcpy(sq + c->sq_tail[q] * 16, sqe, 64);
    c->sq_tail[q] = (uint16_t)((c->sq_tail[q] + 1) % QSIZE);
    barrier();
    wr(c, 0x1000 + (2 * q) * c->stride, c->sq_tail[q]);
    volatile uint32_t *e = cq + c->cq_head[q] * 4;
    uint64_t end = pit_ticks() + ms;
    while (((e[3] >> 16) & 1) != c->phase[q]) {
        if (pit_ticks() > end) { logf("nvme: %s: command %02x timed out", c->name, sqe[0] & 0xFF); return -1; }
        timer_poll();
    }
    int status = (int)((e[3] >> 17) & 0x7FFF);
    if (++c->cq_head[q] == QSIZE) { c->cq_head[q] = 0; c->phase[q] ^= 1; }
    wr(c, 0x1000 + (2 * q + 1) * c->stride, c->cq_head[q]);
    if (status) logf("nvme: %s: command %02x: status %x", c->name, sqe[0] & 0xFF, status);
    return status;
}
static int admin(struct ctl *c, uint8_t op, uint32_t nsid, uint64_t prp1, uint32_t cdw10, uint32_t cdw11) {
    uint32_t sqe[16] = { op, nsid, 0, 0, 0, 0, (uint32_t)prp1, (uint32_t)(prp1 >> 32), 0, 0, cdw10, cdw11 };
    return submit(c, 0, sqe, 5000);
}

/* blocks [lba, lba + count) between the disk and the bounce buffer (count << shift <= max_bytes) */
static bool rw_blocks(struct ctl *c, uint64_t lba, uint32_t count, bool write) {
    uint32_t bytes = count << c->shift, pages = (bytes + 4095) / 4096;
    uint64_t prp2 = 0;
    if (pages == 2) prp2 = bounce_phys + 4096;
    else if (pages > 2) {
        uint64_t *list = (uint64_t *)page(c, PG_PRP);
        for (uint32_t i = 1; i < pages; i++) list[i - 1] = bounce_phys + (uint64_t)i * 4096;
        prp2 = page_phys(c, PG_PRP);
    }
    uint32_t sqe[16] = { write ? 0x01u : 0x02u, c->nsid, 0, 0, 0, 0, (uint32_t)bounce_phys, (uint32_t)(bounce_phys >> 32),
                         (uint32_t)prp2, (uint32_t)(prp2 >> 32), (uint32_t)lba, (uint32_t)(lba >> 32), count - 1 };
    return submit(c, 1, sqe, 10000) == 0;
}

static bool ns_io(struct blkdev *d, uint64_t lba, uint32_t n, void *buf, bool write) {
    struct ctl *c = d->ctx;
    uint32_t per = 1u << (c->shift - 9);                                /* sectors in a block */
    uint32_t chunk = c->max_bytes >> c->shift;                          /* blocks a transfer can take */
    uint8_t *at = buf;
    uint64_t end = lba + n;
    while (lba < end) {
        uint64_t b0 = lba / per; uint32_t skip = (uint32_t)(lba - b0 * per);          /* sectors before ours in the first block */
        uint32_t blocks = (uint32_t)MIN((end - b0 * per + per - 1) / per, (uint64_t)chunk);
        uint32_t take = (uint32_t)MIN(end - lba, (uint64_t)blocks * per - skip);      /* our sectors in these blocks */
        if (write) {
            if ((skip || (take + skip) % per) && !rw_blocks(c, b0, blocks, false)) return false;   /* a partial block: read it first */
            memcpy(bounce + skip * 512, at, take * 512);
            if (!rw_blocks(c, b0, blocks, true)) return false;
        } else {
            if (!rw_blocks(c, b0, blocks, false)) return false;
            memcpy(at, bounce + skip * 512, take * 512);
        }
        at += take * 512; lba += take;
    }
    return true;
}

static void trimmed(char *out, const uint8_t *s, int n) {
    int k = 0;
    for (int i = 0; i < n; i++) out[k++] = (char)(s[i] >= 32 && s[i] < 127 ? s[i] : ' ');
    out[k] = 0;
    while (k && out[k - 1] == ' ') out[--k] = 0;
}

int nvme_add(const struct nvme_fn *fn) {
    if (nctl == CTLS) return 0;
    struct ctl *c = &ctls[nctl];
    memset(c, 0, sizeof *c);
    snfmt(c->name, sizeof c->name, "%s", fn->name);
    uint32_t lo = nvme_cfg_rd(fn, 0x10), hi = (lo & 6) == 4 ? nvme_cfg_rd(fn, 0x14) : 0;
    uint64_t bar = ((uint64_t)hi << 32 | (lo & ~0xFu)) + fn->bar_offset;
    if (!(lo & ~0xFu) && !hi) { logf("nvme: %s: its registers have no address", c->name); return 0; }
    if (sizeof(void *) == 4 && (bar >> 32)) { logf("nvme: %s: registers at %lx, out of this 32-bit build's reach", c->name, bar); return 0; }
    nvme_cfg_wr(fn, 4, nvme_cfg_rd(fn, 4) | 6);                         /* memory space, bus master */
    c->r = mmio_map(bar, 0x2000);
    uint32_t cap_lo = rd(c, R_CAP), cap_hi = rd(c, R_CAP + 4), vs = rd(c, R_VS);
    if (cap_lo == 0xFFFFFFFFu) { logf("nvme: %s: nothing answers at %lx", c->name, bar); return 0; }
    c->stride = 4u << (cap_hi & 15); c->timeout_ms = MAX(((cap_lo >> 24) & 0xFF) * 500u, 1000u);
    if ((cap_hi >> 16) & 15) { logf("nvme: %s: pages of at least %u KiB, not supported", c->name, 4u << ((cap_hi >> 16) & 15)); return 0; }
    if (!bounce) bounce = dma_alloc(BOUNCE, &bounce_phys);
    c->mem = dma_alloc(PAGES * 4096, &c->phys);
    /* reset, the admin queues, enable */
    wr(c, R_CC, rd(c, R_CC) & ~1u);
    uint64_t end = pit_ticks() + c->timeout_ms;
    while ((rd(c, R_CSTS) & 1) && pit_ticks() < end) timer_poll();
    if (rd(c, R_CSTS) & 1) { logf("nvme: %s: would not stop", c->name); return 0; }
    wr(c, R_AQA, (QSIZE - 1) << 16 | (QSIZE - 1));
    wr(c, R_ASQ, (uint32_t)page_phys(c, PG_ASQ)); wr(c, R_ASQ + 4, (uint32_t)(page_phys(c, PG_ASQ) >> 32));
    wr(c, R_ACQ, (uint32_t)page_phys(c, PG_ACQ)); wr(c, R_ACQ + 4, (uint32_t)(page_phys(c, PG_ACQ) >> 32));
    c->phase[0] = c->phase[1] = 1;
    wr(c, R_CC, 1u | 6u << 16 | 4u << 20);                              /* enabled, 4 KiB pages, 64/16-byte entries */
    end = pit_ticks() + c->timeout_ms;
    while (!(rd(c, R_CSTS) & 3) && pit_ticks() < end) timer_poll();
    if ((rd(c, R_CSTS) & 3) != 1) { logf("nvme: %s: would not start (status %x)", c->name, rd(c, R_CSTS)); return 0; }
    /* who it is, how big a transfer it takes */
    if (admin(c, 0x06, 0, page_phys(c, PG_ID), 1, 0)) return 0;         /* IDENTIFY controller */
    const uint8_t *id = page(c, PG_ID);
    char model[41]; trimmed(model, id + 24, 40);
    uint8_t mdts = id[77];
    c->max_bytes = mdts && mdts < 4 ? 4096u << mdts : BOUNCE;
    /* the first namespace: 1, or the first on the active list */
    c->nsid = 1;
    if (admin(c, 0x06, 1, page_phys(c, PG_ID), 0, 0) || !(id[0] | id[1] | id[2] | id[3])) {
        if (admin(c, 0x06, 0, page_phys(c, PG_ID), 2, 0)) return 0;
        memcpy(&c->nsid, id, 4);
        if (!c->nsid || admin(c, 0x06, c->nsid, page_phys(c, PG_ID), 0, 0)) { logf("nvme: %s (%s): no namespace", c->name, model); return 0; }
    }
    uint64_t nsze; memcpy(&nsze, id, 8);
    uint8_t flbas = id[26] & 15; uint32_t lbaf; memcpy(&lbaf, id + 128 + 4 * flbas, 4);
    c->shift = (lbaf >> 16) & 0xFF;
    if ((lbaf & 0xFFFF) && (id[26] & 0x10)) { logf("nvme: %s (%s): metadata inside its blocks, not supported", c->name, model); return 0; }
    if (c->shift < 9 || c->shift > 12) { logf("nvme: %s (%s): %u-byte blocks, not supported", c->name, model, 1u << c->shift); return 0; }
    /* the I/O queue pair: completion queue 1, then submission queue 1 onto it; no interrupts */
    if (admin(c, 0x05, 0, page_phys(c, PG_IOCQ), (QSIZE - 1) << 16 | 1, 1)) return 0;
    if (admin(c, 0x01, 0, page_phys(c, PG_IOSQ), (QSIZE - 1) << 16 | 1, 1u << 16 | 1)) return 0;
    struct blkdev *d = blkdev_add();
    if (!d) return 0;
    snfmt(d->name, sizeof d->name, "NVMe %s: %s", c->name, model);
    d->sectors = nsze << (c->shift - 9); d->io = ns_io; d->ctx = c;
    logf("nvme: %s: %s, version %u.%u, %lu blocks of %u bytes%s", c->name, model, vs >> 16, (vs >> 8) & 0xFF, nsze, 1u << c->shift,
         c->max_bytes < BOUNCE ? " (small transfers)" : "");
    nctl++;
    return 1;
}

uint32_t nvme_cfg_rd(const struct nvme_fn *fn, uint32_t off) {
    return fn->cfg ? mmio_r32(fn->cfg + off) : pci_read32(fn->pd.bus, fn->pd.dev, fn->pd.fn, (uint8_t)off);
}
void nvme_cfg_wr(const struct nvme_fn *fn, uint32_t off, uint32_t v) {
    if (fn->cfg) mmio_w32(fn->cfg + off, v); else pci_write32(fn->pd.bus, fn->pd.dev, fn->pd.fn, (uint8_t)off, v);
}

int nvme_init(void) {
    struct pci_dev devs[4];
    int n = pci_find_all_class(0x01, 0x08, devs, 4), added = 0;
    for (int i = 0; i < n; i++) {
        if (devs[i].prog_if != 2) continue;
        struct nvme_fn fn = { .pd = devs[i] };
        snfmt(fn.name, sizeof fn.name, "%02x:%02x.%d", devs[i].bus, devs[i].dev, devs[i].fn);
        pci_wake(&devs[i]);
        added += nvme_add(&fn);
    }
    return added;
}
