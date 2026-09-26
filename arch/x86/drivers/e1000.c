/* Intel PRO/1000 Ethernet, polled, with the legacy descriptors every member of the family keeps: the 8254x (QEMU's
   e1000), the 8257x/82583 (QEMU's e1000e), and the ones built into the chipsets' PCH — 82577/8/9, I217, I218 (the
   ThinkPad X250's), I219. Reset, the address from the receive address registers (the hardware loads them from its NVM;
   the EEPROM otherwise), 32 receive and 16 transmit descriptors of 2 KiB buffers, link up. On a PCH part the PHY may
   have been left in its ultra-low-power state by the last system: its power is cycled first (LANPHYPC, as Linux does). */
#include "netdev.h"
#include "pci.h"
#include "mem.h"
#include "io.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

enum { CTRL = 0x0000, STATUS = 0x0008, EERD = 0x0014, CTRL_EXT = 0x0018, FEXTNVM3 = 0x003C, ICR = 0x00C0, IMC = 0x00D8,
       RCTL = 0x0100, TCTL = 0x0400, TIPG = 0x0410, RDBAL = 0x2800, RDBAH = 0x2804, RDLEN = 0x2808, RDH = 0x2810,
       RDT = 0x2818, TDBAL = 0x3800, TDBAH = 0x3804, TDLEN = 0x3808, TDH = 0x3810, TDT = 0x3818, MTA = 0x5200,
       RAL0 = 0x5400, RAH0 = 0x5404 };
#define RXN 32
#define TXN 16
struct desc { uint64_t addr; uint16_t len, csum; uint8_t status, err; uint16_t special; };   /* receive */
struct txd { uint64_t addr; uint16_t len; uint8_t cso, cmd, status, css; uint16_t special; };   /* transmit */
struct nic {
    volatile uint8_t *r;
    struct desc *rx; struct txd *tx; uint8_t *rxb, *txb; uint64_t rx_phys, tx_phys, rxb_phys, txb_phys;
    int rx_cur, tx_cur;
    bool pch, mc;
};
#define NICS 2
static struct nic nics[NICS]; static int nnic;

static uint32_t rd(const struct nic *n, uint32_t o) { return mmio_r32(n->r + o); }
static void wr(const struct nic *n, uint32_t o, uint32_t v) { mmio_w32(n->r + o, v); }

/* the family, by device ID: PCH parts need their PHY woken; the rest are plain */
static const uint16_t plain[] = { 0x100E, 0x100F, 0x1004, 0x1011, 0x1026, 0x1027, 0x1028, 0x1075, 0x1076, 0x1077, 0x1078,
    0x1079, 0x107A, 0x107B, 0x107C, 0x1107, 0x1112, 0x10D3, 0x10F6, 0x150C, 0x105E, 0x105F, 0x1060, 0x10A4, 0x10A5, 0x10BC,
    0x107D, 0x107E, 0x107F, 0x10B9, 0x108B, 0x108C, 0x109A };
static const uint16_t pch[] = { 0x10F5, 0x294C, 0x10BD, 0x10E5, 0x10BF, 0x10CB, 0x10CC, 0x10CD, 0x10CE, 0x10DE, 0x10DF,
    0x10EA, 0x10EB, 0x10EF, 0x10F0, 0x1502, 0x1503, 0x153A, 0x153B, 0x155A, 0x1559, 0x15A0, 0x15A1, 0x15A2, 0x15A3, 0x156F,
    0x1570, 0x15B7, 0x15B8, 0x15B9, 0x15BB, 0x15BC, 0x15BD, 0x15BE, 0x15D6, 0x15D7, 0x15D8, 0x15E3, 0x15DF, 0x15E0, 0x15E1,
    0x15E2, 0x0D4C, 0x0D4D, 0x0D4E, 0x0D4F, 0x0D53, 0x0D55, 0x15F4, 0x15F5, 0x15F9, 0x15FA, 0x15FB, 0x15FC, 0x1A1C, 0x1A1D,
    0x1A1E, 0x1A1F, 0x550A, 0x550B, 0x550C, 0x550D, 0x550E, 0x550F, 0x5510, 0x5511, 0x57A0, 0x57A1, 0x57B3, 0x57B4, 0x57B5,
    0x57B6, 0x57B7, 0x57B8, 0x57B9, 0x57BA };
static bool in_list(const uint16_t *l, int n, uint16_t id) { for (int i = 0; i < n; i++) if (l[i] == id) return true; return false; }

static bool link_up(struct netdev *d) { return rd(d->ctx, STATUS) & 2; }
static void set_mc(struct netdev *d, bool all) {
    struct nic *n = d->ctx; n->mc = all;
    uint32_t r = rd(n, RCTL);
    wr(n, RCTL, all ? r | (1u << 4) : r & ~(1u << 4));                 /* MPE: every multicast frame */
}
static int recv(struct netdev *d, void *out, int cap) {
    struct nic *n = d->ctx;
    for (;;) {
        struct desc *x = &n->rx[n->rx_cur];
        if (!(x->status & 1)) return 0;
        int len = x->len;
        bool whole = (x->status & 2) && !x->err;                       /* in one buffer, no errors; else skipped */
        if (whole) memcpy(out, n->rxb + n->rx_cur * 2048, (size_t)MIN(len, cap));
        x->status = 0;
        wr(n, RDT, (uint32_t)n->rx_cur);
        n->rx_cur = (n->rx_cur + 1) % RXN;
        if (whole) return MIN(len, cap);
    }
}
static bool send(struct netdev *d, const void *f, int len) {
    struct nic *n = d->ctx;
    if (len > 1514 || len < 1) return false;
    struct txd *x = &n->tx[n->tx_cur];
    if (x->len && !(x->status & 1)) {                                   /* the ring is full: wait a moment */
        uint64_t end = pit_ticks() + 5;
        while (!(x->status & 1) && pit_ticks() < end) timer_poll();
        if (!(x->status & 1)) return false;
    }
    memcpy(n->txb + n->tx_cur * 2048, f, (size_t)len);
    x->addr = n->txb_phys + (uint64_t)n->tx_cur * 2048; x->len = (uint16_t)len; x->cso = 0; x->css = 0;
    x->status = 0; x->special = 0;
    x->cmd = 0x0B;                                                      /* end of packet, insert the FCS, report status */
    barrier();
    n->tx_cur = (n->tx_cur + 1) % TXN;
    wr(n, TDT, (uint32_t)n->tx_cur);
    return true;
}

/* the PHY's power cycled: out of ultra-low-power, reset (LANPHYPC, the PCH parts) */
static void phy_power_cycle(struct nic *n) {
    wr(n, FEXTNVM3, (rd(n, FEXTNVM3) & ~0x0C000000u) | 0x08000000u);  /* its configuration counter: 50 ms */
    uint32_t c = rd(n, CTRL);
    wr(n, CTRL, (c | 1u << 16) & ~(1u << 17)); rd(n, STATUS);          /* LANPHYPC override, value 0 */
    for (int i = 0; i < 20; i++) io_wait();
    wr(n, CTRL, c & ~(1u << 16)); rd(n, STATUS);
    uint64_t end = pit_ticks() + 120;
    while (!(rd(n, CTRL_EXT) & 4) && pit_ticks() < end) timer_poll();   /* LCD power cycle done */
    sleep_ms(30);
}

static void nic_init(const struct pci_dev *pd) {
    bool mem; uint64_t bar = pci_bar(pd, 0, &mem);
    if (!mem || !bar || (sizeof(void *) == 4 && (bar >> 32))) { logf("e1000: %02x:%02x.%d: no usable registers", pd->bus, pd->dev, pd->fn); return; }
    struct nic *n = &nics[nnic];
    memset(n, 0, sizeof *n);
    n->pch = in_list(pch, (int)ARRAY_LEN(pch), pd->device);
    pci_enable(pd);
    n->r = mmio_map(bar, 0x20000);
    wr(n, IMC, 0xFFFFFFFFu);
    if (n->pch) phy_power_cycle(n);
    wr(n, CTRL, rd(n, CTRL) | 1u << 26);                                /* reset */
    sleep_ms(5);
    uint64_t end = pit_ticks() + 100;
    while ((rd(n, CTRL) & 1u << 26) && pit_ticks() < end) timer_poll();
    sleep_ms(n->pch ? 20 : 1);
    wr(n, IMC, 0xFFFFFFFFu); rd(n, ICR);
    /* the address: loaded into receive address 0 at reset; else from the EEPROM */
    uint32_t lo = rd(n, RAL0), hi = rd(n, RAH0);
    uint8_t mac[6];
    if (!lo && !(hi & 0xFFFF)) {
        bool newer = pd->device != 0x100E && pd->device != 0x100F && pd->device != 0x1004;   /* the address field's place */
        for (int w = 0; w < 3; w++) {
            wr(n, EERD, 1 | (uint32_t)w << (newer ? 2 : 8));
            uint32_t v = 0; uint64_t e2 = pit_ticks() + 10;
            while (!((v = rd(n, EERD)) & (newer ? 2u : 16u)) && pit_ticks() < e2) timer_poll();
            mac[w * 2] = (uint8_t)(v >> 16); mac[w * 2 + 1] = (uint8_t)(v >> 24);
        }
        wr(n, RAL0, (uint32_t)mac[0] | mac[1] << 8 | mac[2] << 16 | (uint32_t)mac[3] << 24);
        wr(n, RAH0, (uint32_t)mac[4] | mac[5] << 8 | 1u << 31);
    } else {
        for (int i = 0; i < 4; i++) mac[i] = (uint8_t)(lo >> (8 * i));
        mac[4] = (uint8_t)hi; mac[5] = (uint8_t)(hi >> 8);
        wr(n, RAH0, hi | 1u << 31);
    }
    for (int i = 0; i < 128; i++) wr(n, MTA + 4 * (uint32_t)i, 0);
    /* rings: descriptors in a page each, 2 KiB buffers */
    n->rx = dma_alloc(4096, &n->rx_phys); n->tx = dma_alloc(4096, &n->tx_phys);
    n->rxb = dma_alloc(RXN * 2048, &n->rxb_phys); n->txb = dma_alloc(TXN * 2048, &n->txb_phys);
    for (int i = 0; i < RXN; i++) { n->rx[i].addr = n->rxb_phys + (uint64_t)i * 2048; n->rx[i].status = 0; }
    wr(n, RDBAL, (uint32_t)n->rx_phys); wr(n, RDBAH, (uint32_t)(n->rx_phys >> 32)); wr(n, RDLEN, RXN * 16);
    wr(n, RDH, 0); wr(n, RDT, RXN - 1);
    wr(n, RCTL, 1u << 1 | 1u << 15 | 1u << 26);                         /* on, broadcasts, CRC stripped, 2 KiB buffers */
    wr(n, TDBAL, (uint32_t)n->tx_phys); wr(n, TDBAH, (uint32_t)(n->tx_phys >> 32)); wr(n, TDLEN, TXN * 16);
    wr(n, TDH, 0); wr(n, TDT, 0);
    wr(n, TIPG, 0x0060200A);
    wr(n, TCTL, 1u << 1 | 1u << 3 | 0x0Fu << 4 | 0x40u << 12);          /* on, short frames padded, full duplex */
    wr(n, CTRL, (rd(n, CTRL) | 1u << 6 | 1u << 5) & ~(1u << 3 | 1u << 31));   /* set link up, speed detected; no reset, no PHY reset */
    struct netdev *d = netdev_add();
    if (!d) return;
    snfmt(d->name, sizeof d->name, "Intel %04x at %02x:%02x.%d", pd->device, pd->bus, pd->dev, pd->fn);
    memcpy(d->mac, mac, 6); d->link = link_up; d->send = send; d->recv = recv; d->multicast = set_mc; d->ctx = n;
    nnic++;
    logf("e1000: %s, %02x:%02x:%02x:%02x:%02x:%02x%s", d->name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], n->pch ? ", PHY power-cycled" : "");
}

int e1000_init(void) {
    struct pci_dev devs[4];
    int n = pci_find_all_class(0x02, 0x00, devs, 4), before = nnic;
    for (int i = 0; i < n && nnic < NICS; i++) {
        if (devs[i].vendor != 0x8086) continue;
        if (in_list(plain, (int)ARRAY_LEN(plain), devs[i].device) || in_list(pch, (int)ARRAY_LEN(pch), devs[i].device)) nic_init(&devs[i]);
        else logf("e1000: %02x:%02x.%d 8086:%04x: an Intel network chip this driver doesn't know", devs[i].bus, devs[i].dev, devs[i].fn, devs[i].device);
    }
    return nnic - before;
}
