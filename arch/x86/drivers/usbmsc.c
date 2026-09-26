/* USB sticks: mass storage, bulk-only transport, SCSI commands (INQUIRY, TEST UNIT READY, READ CAPACITY, READ(10),
   WRITE(10)). Data goes through one 64 KiB buffer aligned to 64 KiB, so a transfer is always a single TRB. Drives
   keep their number until a rescan, so a stick pulled out can't hand its number to another. */
#include "usb.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

#define MSCS 4
#define CHUNK 128                           /* sectors per command: the buffer */
struct msc {
    bool used, ready;
    struct usb_dev *d; struct usb_ep *in, *out;
    uint8_t iface; uint32_t tag;
    uint64_t sectors;
};
static struct msc mscs[MSCS];
static struct msc *list[MSCS]; static int nlist;     /* drive numbers */
static uint8_t *buf; static uint64_t buf_phys;

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static void *probe(struct usb_dev *d, const struct usb_iface *i) {
    if (i->cls != 8 || i->proto != 0x50 || (i->sub != 6 && i->sub != 5 && i->sub != 2)) return 0;
    if (!buf && !(buf = usb_mem(CHUNK * 512, CHUNK * 512, &buf_phys))) return 0;   /* 64 KiB on a 64 KiB boundary */
    struct msc *m = 0;
    for (int k = 0; k < MSCS && !m; k++) if (!mscs[k].used) m = &mscs[k];
    if (!m) return 0;
    struct usb_ep *in = 0, *out = 0;
    for (const uint8_t *e = usb_next_desc(i, 0, 5); e; e = usb_next_desc(i, e, 5)) {
        if ((e[3] & 3) != EP_BULK) continue;
        if ((e[2] & 0x80) && !in) in = usb_add_ep(d, i, e);
        else if (!(e[2] & 0x80) && !out) out = usb_add_ep(d, i, e);
    }
    if (!in || !out) return 0;
    memset(m, 0, sizeof *m);
    m->used = true; m->d = d; m->in = in; m->out = out; m->iface = i->num;
    return m;
}

static void reset_recovery(struct msc *m) {
    usb_control(m->d, 0x21, 0xFF, 0, m->iface, 0, 0);               /* Bulk-Only Mass Storage Reset */
    usb_clear_halt(m->in); usb_clear_halt(m->out);
}

/* one command: CBW, data, CSW. ≥ 0: bytes moved; -2: the device says the command failed (ask it why); -1: broken */
static int bot(struct msc *m, const uint8_t *cb, int cblen, bool in, uint32_t len, uint32_t ms) {
    uint8_t *cbw = m->out->buf;
    memset(cbw, 0, 31);
    put32(cbw, 0x43425355); put32(cbw + 4, ++m->tag); put32(cbw + 8, len);
    cbw[12] = in ? 0x80 : 0; cbw[14] = (uint8_t)cblen; memcpy(cbw + 15, cb, (size_t)cblen);
    if (!xhci_queue(m->out, m->out->buf_phys, 31) || !xhci_wait(m->out, 2000) || m->out->cc != 1) { reset_recovery(m); return -1; }
    uint32_t got = 0;
    if (len) {
        struct usb_ep *ep = in ? m->in : m->out;
        if (!xhci_queue(ep, buf_phys, len) || !xhci_wait(ep, ms)) { reset_recovery(m); return -1; }
        if (ep->cc == 6) usb_clear_halt(ep);                         /* stalled: the CSW still comes */
        else if (ep->cc != 1 && ep->cc != 13) { reset_recovery(m); return -1; }
        got = len - MIN(ep->residual, len);
    }
    uint8_t *csw = m->in->buf;
    for (int t = 0; t < 2; t++) {
        if (!xhci_queue(m->in, m->in->buf_phys, 13) || !xhci_wait(m->in, 2000)) { reset_recovery(m); return -1; }
        if (m->in->cc != 6) break;
        usb_clear_halt(m->in);
    }
    if ((m->in->cc != 1 && m->in->cc != 13) || get32(csw) != 0x53425355 || get32(csw + 4) != m->tag) { reset_recovery(m); return -1; }
    if (csw[12] == 0) return (int)got;
    if (csw[12] == 1) return -2;
    reset_recovery(m);                                               /* phase error */
    return -1;
}

/* a command with its retries: a stick answers "unit attention" or "not ready" for a while after it wakes */
static bool scsi(struct msc *m, const uint8_t *cb, int cblen, bool in, uint32_t len, uint32_t ms) {
    for (int attempt = 0; attempt < 4; attempt++) {
        int r = bot(m, cb, cblen, in, len, ms);
        if (r >= 0 && (uint32_t)r == len) return true;
        if (r == -2) {
            uint8_t rs[6] = { 0x03, 0, 0, 0, 18, 0 };
            int sk = bot(m, rs, 6, true, 18, 1000) >= 14 ? buf[2] & 15 : -1;
            if (sk != 2 && sk != 6) return false;                    /* only not-ready and unit attention pass */
            sleep_ms(100);
        } else if (r < 0 && attempt) sleep_ms(50);
    }
    return false;
}

static void start(void *s) {
    struct msc *m = s;
    uint8_t lun = 0;
    usb_control(m->d, 0xA1, 0xFE, 0, m->iface, &lun, 1);             /* GET MAX LUN (may stall): only LUN 0 is used */
    uint8_t inq[6] = { 0x12, 0, 0, 0, 36, 0 };
    if (bot(m, inq, 6, true, 36, 2000) < 5) { logf("usb: %s: no answer to INQUIRY", m->d->name); return; }
    if ((buf[0] & 0x1F) != 0) { logf("usb: %s: not a disk (SCSI type %u)", m->d->name, buf[0] & 0x1F); return; }
    uint8_t tur[6] = { 0 };
    for (int t = 0; t < 30 && !scsi(m, tur, 6, false, 0, 1000); t++) sleep_ms(100);
    uint8_t rc[10] = { 0x25 };
    if (!scsi(m, rc, 10, true, 8, 2000)) { logf("usb: %s: capacity unknown (no medium?)", m->d->name); return; }
    uint32_t last = be32(buf), bs = be32(buf + 4);
    if (bs != 512) { logf("usb: %s: %u-byte sectors, not used", m->d->name, bs); return; }
    m->sectors = (uint64_t)last + 1;
    m->ready = true;
    if (nlist < MSCS) list[nlist++] = m;
    logf("usb: %s: drive %d, %lu MiB", m->d->name, nlist - 1, m->sectors >> 11);
}

static void poll(void) { }
static void gone(void *s) {
    struct msc *m = s;
    for (int i = 0; i < nlist; i++) if (list[i] == m) list[i] = 0;  /* the number stays, dead, until a rescan */
    m->used = m->ready = false;
}

const struct usb_driver usb_msc_driver = { "storage", probe, start, poll, gone };

int usb_blk_drives(void) { return nlist; }
void usb_blk_rescan(void) {
    int k = 0;
    for (int i = 0; i < nlist; i++) if (list[i]) list[k++] = list[i];
    nlist = k;
}
const char *usb_blk_name(int d) { return d >= 0 && d < nlist && list[d] ? list[d]->d->name : "(unplugged)"; }
uint64_t usb_blk_sectors(int d) { return d >= 0 && d < nlist && list[d] ? list[d]->sectors : 0; }
bool usb_blk_io(int d, uint64_t lba, uint32_t n, void *p, bool write) {
    struct msc *m = d >= 0 && d < nlist ? list[d] : 0;
    if (!m || !m->ready || lba + n > m->sectors || lba + n > 0xFFFFFFFFull) return false;
    uint8_t *at = p;
    while (n) {
        uint32_t c = MIN(n, (uint32_t)CHUNK);
        uint8_t cb[10] = { write ? 0x2A : 0x28, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba, 0, (uint8_t)(c >> 8), (uint8_t)c, 0 };
        if (write) memcpy(buf, at, c * 512);
        if (!scsi(m, cb, 10, !write, c * 512, 10000)) { logf("usb: %s: %s at %lu failed", m->d->name, write ? "write" : "read", lba); return false; }
        if (!write) memcpy(at, buf, c * 512);
        at += c * 512; lba += c; n -= c;
    }
    return true;
}
