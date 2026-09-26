/* DesignWare I2C, polled. The input clock differs by chipset (Intel: 120 MHz on Skylake, 133 on Apollo Lake, 216 from
   Cannon Lake on; AMD ~150) and nothing tells the driver which: the timing is worked out for the fastest, so on the
   others the bus just runs slower than 400 kHz, which every device accepts. */
#include "i2c.h"
#include "pci.h"
#include "mem.h"
#include "io.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

enum { IC_CON = 0x00, IC_TAR = 0x04, IC_DATA_CMD = 0x10, IC_SS_HCNT = 0x14, IC_SS_LCNT = 0x18, IC_FS_HCNT = 0x1C,
       IC_FS_LCNT = 0x20, IC_INTR_MASK = 0x30, IC_RAW_INTR = 0x34, IC_RX_TL = 0x38, IC_TX_TL = 0x3C, IC_CLR_INTR = 0x40,
       IC_CLR_TX_ABRT = 0x54, IC_CLR_STOP = 0x60, IC_ENABLE = 0x6C, IC_STATUS = 0x70, IC_TXFLR = 0x74, IC_RXFLR = 0x78,
       IC_SDA_HOLD = 0x7C, IC_ABRT_SRC = 0x80, IC_EN_STATUS = 0x9C, IC_PARAM1 = 0xF4, IC_VERSION = 0xF8, IC_TYPE = 0xFC };
#define R_TX_ABRT (1u << 6)
#define R_STOP    (1u << 9)
#define DW_TYPE   0x44570140u                     /* "DW" and the I2C component number */
#define CLK_MHZ   216

struct bus { volatile uint8_t *r; uint64_t phys; bool pci; uint8_t dev, fn; int txd, rxd; char name[28]; };
static struct bus buses[I2C_BUSES];
static int nb;

static uint32_t rd(const struct bus *b, uint32_t o) { return mmio_r32(b->r + o); }
static void wr(const struct bus *b, uint32_t o, uint32_t v) { mmio_w32(b->r + o, v); }
static void pause_us(int us) { for (int i = 0; i < us; i++) io_wait(); }

static bool set_enable(const struct bus *b, bool on) {
    for (int i = 0; i < 100; i++) {
        wr(b, IC_ENABLE, on);
        if ((rd(b, IC_EN_STATUS) & 1) == (uint32_t)on) return true;
        pause_us(250);
    }
    return false;
}

static bool setup(struct bus *b) {
    uint32_t type = rd(b, IC_TYPE);
    if (type != DW_TYPE) return false;
    if (!set_enable(b, false)) { logf("i2c: %s would not stop", b->name); return false; }
    uint32_t p1 = rd(b, IC_PARAM1), v = rd(b, IC_VERSION);
    b->txd = (int)((p1 >> 16) & 0xFF) + 1; b->rxd = (int)((p1 >> 8) & 0xFF) + 1;
    if (!p1 || b->txd < 2) b->txd = 8;
    if (!p1 || b->rxd < 2) b->rxd = 8;
    /* high ≥ 0.6 µs and low ≥ 1.3 µs at 400 kHz (4.0 and 4.7 at 100), each with 0.3 µs for the edge */
    wr(b, IC_FS_HCNT, CLK_MHZ * 900 / 1000 - 3); wr(b, IC_FS_LCNT, CLK_MHZ * 1600 / 1000 - 1);
    wr(b, IC_SS_HCNT, CLK_MHZ * 4300 / 1000 - 3); wr(b, IC_SS_LCNT, CLK_MHZ * 5000 / 1000 - 1);
    wr(b, IC_SDA_HOLD, CLK_MHZ * 300 / 1000);                         /* 0.3 µs of data hold */
    wr(b, IC_CON, 0x65);                                              /* master, fast mode, restarts, no slave */
    wr(b, IC_TX_TL, 0); wr(b, IC_RX_TL, 0); wr(b, IC_INTR_MASK, 0);
    rd(b, IC_CLR_INTR);
    logf("i2c: %s: DesignWare %c.%c%c%c, FIFOs %d/%d", b->name, v >> 24, v >> 16, v >> 8, v, b->txd, b->rxd);
    return true;
}

/* Intel's I2C controllers (LPSS / Serial IO), by device ID: nothing else of class 0C80 is written to — that class also
   holds the flash chip's SPI controller. Skylake to Meteor Lake, Apollo/Gemini Lake, and the older Serial IO. */
static const uint16_t lpss_i2c[] = {
    0x9D60, 0x9D61, 0x9D62, 0x9D63, 0x9D64, 0x9D65, 0xA160, 0xA161, 0xA162, 0xA163, 0xA2E0, 0xA2E1, 0xA2E2, 0xA2E3,
    0x9DE8, 0x9DE9, 0x9DEA, 0x9DEB, 0x9DC5, 0x9DC6, 0xA368, 0xA369, 0xA36A, 0xA36B, 0x02E8, 0x02E9, 0x02EA, 0x02EB,
    0x02C5, 0x02C6, 0x06E8, 0x06E9, 0x06EA, 0x06EB, 0x34E8, 0x34E9, 0x34EA, 0x34EB, 0x34C5, 0x34C6, 0x38E8, 0x38E9,
    0x38EA, 0x38EB, 0x4DE8, 0x4DE9, 0x4DEA, 0x4DEB, 0x4DC5, 0x4DC6, 0xA0E8, 0xA0E9, 0xA0EA, 0xA0EB, 0xA0C5, 0xA0C6,
    0xA0D8, 0xA0D9, 0x43AD, 0x43AE, 0x43D8, 0x43E8, 0x43E9, 0x43EA, 0x43EB, 0x4B78, 0x4B79, 0x4B7A, 0x4B7B, 0x4B4B,
    0x4B4C, 0x4B44, 0x4B45, 0x51C5, 0x51C6, 0x51D8, 0x51D9, 0x51E8, 0x51E9, 0x51EA, 0x51EB, 0x54C5, 0x54C6, 0x54D8,
    0x54D9, 0x54E8, 0x54E9, 0x54EA, 0x54EB, 0x7ACC, 0x7ACD, 0x7ACE, 0x7ACF, 0x7AFC, 0x7AFD, 0x7A4C, 0x7A4D, 0x7A4E,
    0x7A4F, 0x7A7C, 0x7A7D, 0x7E50, 0x7E51, 0x7E78, 0x7E79, 0x7E7A, 0x7E7B, 0x5AAC, 0x5AAE, 0x5AB0, 0x5AB2, 0x5AB4,
    0x5AB6, 0x5AB8, 0x5ABA, 0x31AC, 0x31AE, 0x31B0, 0x31B2, 0x31B4, 0x31B6, 0x31B8, 0x31BA, 0x9C61, 0x9C62, 0x9CE1, 0x9CE2 };
static bool known_i2c(uint16_t id) { for (unsigned i = 0; i < ARRAY_LEN(lpss_i2c); i++) if (lpss_i2c[i] == id) return true; return false; }

int i2c_init(void) {
    struct pci_dev devs[24];
    int n = pci_find_all_class(0x0C, 0x80, devs, 24);
    for (int i = 0; i < n && nb < I2C_BUSES; i++) {
        if (devs[i].vendor != 0x8086) continue;
        bool known = known_i2c(devs[i].device), mem;
        uint64_t bar = pci_bar(&devs[i], 0, &mem);
        logf("i2c: %02x:%02x.%d %04x at %lx: %s", devs[i].bus, devs[i].dev, devs[i].fn, devs[i].device, bar, known ? "an I2C controller" : "not an I2C one, left alone");
        if (!known || !mem) continue;
        if (!bar) { pci_wake(&devs[i]); bar = pci_place_bar(&devs[i], 0); }   /* the firmware left it without an address */
        if (!bar || (sizeof(void *) == 4 && (bar >> 32))) continue;
        pci_enable(&devs[i]);
        struct bus *b = &buses[nb];
        memset(b, 0, sizeof *b);
        b->r = mmio_map(bar, 0x1000); b->phys = bar; b->pci = true; b->dev = devs[i].dev; b->fn = devs[i].fn;
        snfmt(b->name, sizeof b->name, "%02x:%02x.%d", devs[i].bus, devs[i].dev, devs[i].fn);
        /* out of reset, with its DMA: the LPSS private registers' resets (the older Serial IO's at 0x804) */
        volatile uint8_t *rst = b->r + (devs[i].device >> 8 == 0x9C ? 0x804 : 0x204);
        uint32_t rs = mmio_r32(rst);
        if ((rs & 3) != 3) { mmio_w32(rst, 0x7); pause_us(100); logf("i2c: %s: out of reset (%x, now %x)", b->name, rs, mmio_r32(rst)); }
        if (setup(b)) nb++;
        else logf("i2c: %s: not a DesignWare controller after all (%08x)", b->name, rd(b, IC_TYPE));
    }
    return nb;
}

/* who answers on a bus: a one-byte read at every address, as i2cdetect does; a bit per address in map */
int i2c_scan(int bi, uint8_t map[16]) {
    char line[128]; int k = snfmt(line, sizeof line, "i2c: %s answers at", i2c_name(bi)), found = 0;
    memset(map, 0, 16);
    for (uint16_t a = 0x08; a < 0x78; a++) {
        uint8_t x;
        if (i2c_xfer(bi, a, 0, 0, &x, 1) != 1) continue;
        map[a >> 3] |= (uint8_t)(1u << (a & 7)); found++;
        if (k < 120) k += snfmt(line + k, sizeof line - (size_t)k, " %02x", a);
    }
    logf("%s%s", line, found ? "" : " nothing");
    return found;
}

void i2c_add_mmio(uint64_t base) {
    if (!base || nb == I2C_BUSES || (sizeof(void *) == 4 && (base >> 32))) return;
    for (int i = 0; i < nb; i++) if (buses[i].phys == base) return;
    struct bus *b = &buses[nb];
    memset(b, 0, sizeof *b);
    b->r = mmio_map(base, 0x1000); b->phys = base;
    snfmt(b->name, sizeof b->name, "at %lx", base);
    if (setup(b)) nb++;
    else logf("i2c: %s: no DesignWare I2C there (%08x)", b->name, rd(b, IC_TYPE));
}

int i2c_buses(void) { return nb; }
const char *i2c_name(int bus) { return bus >= 0 && bus < nb ? buses[bus].name : "?"; }
int i2c_find(bool pci, uint8_t dev, uint8_t fn, uint64_t mmio) {
    for (int i = 0; i < nb; i++) {
        if (pci && buses[i].pci && buses[i].dev == dev && buses[i].fn == fn) return i;
        if (!pci && mmio && buses[i].phys == mmio) return i;
    }
    return -1;
}

int i2c_xfer(int bi, uint16_t addr, const uint8_t *w, int wn, uint8_t *r, int rn) {
    if (bi < 0 || bi >= nb || wn + rn == 0) return -3;
    const struct bus *b = &buses[bi];
    uint64_t end = pit_ticks() + 20;
    while ((rd(b, IC_STATUS) & 0x20) && pit_ticks() < end) timer_poll();   /* the last transfer still on the bus */
    if (!set_enable(b, false)) return -2;
    wr(b, IC_TAR, addr & 0x3FF);
    if (!set_enable(b, true)) return -2;
    rd(b, IC_CLR_INTR);
    int total = wn + rn, sent = 0, got = 0, pending = 0;
    end = pit_ticks() + 30 + (uint64_t)total / 8;
    while (got < rn || sent < total) {
        if (rd(b, IC_RAW_INTR) & R_TX_ABRT) {
            uint32_t src = rd(b, IC_ABRT_SRC); rd(b, IC_CLR_TX_ABRT);
            return (src & 7) ? -1 : -3;                               /* the address not acknowledged: nobody there */
        }
        while (sent < total && (int)rd(b, IC_TXFLR) < b->txd) {
            uint32_t c;
            if (sent < wn) c = w[sent];
            else { if (pending >= b->rxd) break; c = 0x100 | (sent == wn && wn ? 0x400u : 0); pending++; }   /* read, restart */
            if (sent == total - 1) c |= 0x200;                        /* stop after the last */
            wr(b, IC_DATA_CMD, c); sent++;
        }
        while (got < rn && rd(b, IC_RXFLR)) { r[got++] = (uint8_t)rd(b, IC_DATA_CMD); pending--; }
        if (pit_ticks() > end) { set_enable(b, false); return -2; }
        timer_poll();
    }
    end = pit_ticks() + 10;
    while (!(rd(b, IC_RAW_INTR) & (R_STOP | R_TX_ABRT)) && pit_ticks() < end) timer_poll();
    uint32_t raw = rd(b, IC_RAW_INTR);
    rd(b, IC_CLR_STOP);
    if (raw & R_TX_ABRT) { uint32_t src = rd(b, IC_ABRT_SRC); rd(b, IC_CLR_TX_ABRT); return (src & 7) ? -1 : -3; }
    return rn;
}
