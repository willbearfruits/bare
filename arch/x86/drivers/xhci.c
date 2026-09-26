/* xHCI (USB 3) host controllers, polled. A command ring, one event ring (interrupter 0, its interrupt left off) and a
   transfer ring per endpoint. Every transfer is one TRB — the callers keep buffers inside a 64 KiB piece — except a
   control transfer's setup, data and status stages. Everything the controller reads or writes is in memory from
   usb_dma (below 4 GiB, so 32-bit-only controllers reach it too). */
#include "usb.h"
#include "pci.h"
#include "mem.h"
#include "io.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

struct trb { uint32_t p0, p1, status, control; };

enum { TRB_NORMAL = 1, TRB_SETUP = 2, TRB_DATA = 3, TRB_STATUS = 4, TRB_ISOCH = 5, TRB_LINK = 6,
       TRB_ENABLE_SLOT = 9, TRB_DISABLE_SLOT = 10, TRB_ADDRESS = 11, TRB_CONFIGURE = 12, TRB_EVALUATE = 13,
       TRB_RESET_EP = 14, TRB_STOP_EP = 15, TRB_SET_DEQ = 16, TRB_TRANSFER_EV = 32, TRB_COMMAND_EV = 33 };
#define T_TYPE(t) ((uint32_t)(t) << 10)
#define T_ISP    (1u << 2)
#define T_IOC    (1u << 5)
#define T_IDT    (1u << 6)
#define T_TC     (1u << 1)
#define T_DIR_IN (1u << 16)
#define T_SIA    (1u << 31)             /* isochronous: start as soon as possible */
enum { CC_SUCCESS = 1, CC_STALL = 6, CC_SHORT = 13, CC_OVERRUN = 15, CC_CONTEXT_STATE = 19, CC_MISSED = 23, CC_STOPPED = 26 };

/* operational registers */
#define USBCMD 0x00
#define USBSTS 0x04
#define PAGESIZE 0x08
#define CRCR   0x18
#define DCBAAP 0x30
#define CONFIG 0x38
/* interrupter 0, in the runtime registers */
#define IMAN   0x20
#define IMOD   0x24
#define ERSTSZ 0x28
#define ERSTBA 0x30
#define ERDP   0x38
/* PORTSC */
#define P_CCS  (1u << 0)
#define P_PED  (1u << 1)
#define P_PR   (1u << 4)
#define P_PP   (1u << 9)
#define P_CSC  (1u << 17)
#define P_WRC  (1u << 19)
#define P_PRC  (1u << 21)
#define P_WPR  (1u << 31)
#define P_CHANGES 0x00FE0000u           /* CSC PEC WRC OCC PRC PLC CEC: write 1 to clear */
#define P_KEEP    0x0E00C3E0u           /* PLS, PP, indicators, wake bits: written back as read (PED and changes as 0) */

#define HCS    4
#define SLOTS  64                       /* at most this many enabled */
#define RING_N 64                       /* TRBs per command/transfer ring: 1 KiB (isochronous rings: ep->ring_n) */
#define EVT_N  1024                     /* a camera finishes up to 8000 transfers a second */

struct xhc {
    volatile uint8_t *cap, *op, *rt; volatile uint32_t *db;
    uint64_t phys;                      /* the registers' physical address: pages are mapped as they are needed */
    uint8_t  ports, slots, csz;         /* csz: context size, 32 or 64 bytes */
    uint32_t usb3[8];                   /* a bit per port: a USB 3 one */
    uint64_t *dcbaa;
    struct usb_ep cmd;                  /* the command ring, kept like a transfer ring */
    volatile struct trb *evt; uint64_t evt_phys; uint16_t evt_deq; uint8_t evt_cycle;
    uint8_t *in; uint64_t in_phys;      /* the input context: one command at a time */
    uint8_t *cbuf; uint64_t cbuf_phys;  /* control transfers' data, 4 KiB */
    volatile bool cmd_done; volatile uint8_t cmd_cc, cmd_slot; uint64_t cmd_wait;
    bool     dead;
    struct usb_dev *devs[SLOTS + 1];
};
static struct xhc hcs[HCS];
static int nhc;

static uint32_t rd(volatile uint8_t *b, uint32_t off) { return mmio_r32(b + off); }
static void wr(volatile uint8_t *b, uint32_t off, uint32_t v) { mmio_w32(b + off, v); }
static void wr64(volatile uint8_t *b, uint32_t off, uint64_t v) { wr(b, off, (uint32_t)v); wr(b, off + 4, (uint32_t)(v >> 32)); }
static bool wait_reg(volatile uint8_t *b, uint32_t off, uint32_t mask, uint32_t want, uint32_t ms) {
    uint64_t end = pit_ticks() + ms;
    for (;;) {
        if ((rd(b, off) & mask) == want) return true;
        if (pit_ticks() > end) return false;
        timer_poll();                                                /* without timer interrupts the audio lives on this */
    }
}

/* ---- rings ---- */
static inline unsigned rn(const struct usb_ep *r) { return r->ring_n ? r->ring_n : RING_N; }
static void ring_init(struct usb_ep *r, void *mem, uint64_t phys) {
    r->ring = mem; r->ring_phys = phys; r->enq = 0; r->cycle = 1;
    memset(mem, 0, rn(r) * sizeof(struct trb));
    volatile struct trb *l = &r->ring[rn(r) - 1];                  /* the last TRB links back to the first */
    l->p0 = (uint32_t)phys; l->p1 = (uint32_t)(phys >> 32); l->status = 0; l->control = T_TYPE(TRB_LINK) | T_TC;
}
static uint64_t ring_push(struct usb_ep *r, uint32_t p0, uint32_t p1, uint32_t status, uint32_t control) {
    volatile struct trb *t = &r->ring[r->enq];
    uint64_t at = r->ring_phys + (uint64_t)r->enq * sizeof(struct trb);
    t->p0 = p0; t->p1 = p1; t->status = status;
    barrier();
    t->control = (control & ~1u) | r->cycle;                       /* the cycle bit last: then it is the controller's */
    if (++r->enq == rn(r) - 1) {
        r->ring[rn(r) - 1].control = T_TYPE(TRB_LINK) | T_TC | r->cycle;
        r->enq = 0; r->cycle ^= 1;
    }
    return at;
}
static uint64_t ring_deq(const struct usb_ep *r) { return (r->ring_phys + (uint64_t)r->enq * sizeof(struct trb)) | r->cycle; }

/* ---- events ---- */
static struct usb_ep *ep_of(struct xhc *h, unsigned slot, unsigned dci) {
    struct usb_dev *d = slot && slot <= SLOTS ? h->devs[slot] : 0;
    if (!d) return 0;
    if (dci == 1) return &d->ep0;
    for (int i = 0; i < d->neps; i++) if (d->ep[i].dci == dci) return &d->ep[i];
    return 0;
}
static void hc_events(struct xhc *h) {
    bool any = false;
    for (;;) {
        volatile struct trb *e = &h->evt[h->evt_deq];
        uint32_t c = e->control;
        if ((c & 1) != h->evt_cycle) break;
        barrier();
        uint64_t p = e->p0 | (uint64_t)e->p1 << 32;
        uint32_t st = e->status, type = (c >> 10) & 63;
        if (type == TRB_COMMAND_EV && p == h->cmd_wait) {
            h->cmd_cc = (uint8_t)(st >> 24); h->cmd_slot = (uint8_t)(c >> 24); h->cmd_done = true;
        } else if (type == TRB_TRANSFER_EV) {
            struct usb_ep *ep = ep_of(h, c >> 24, (c >> 16) & 31);
            uint8_t cc = (uint8_t)(st >> 24);
            struct usb_iso *io = ep ? ep->iso : 0;
            if (io) {                                                /* isochronous: the slot's length, in order */
                if (cc == CC_OVERRUN) io->overruns++;
                else if (p >= ep->ring_phys && p < ep->ring_phys + (uint64_t)io->n * sizeof(struct trb)) {
                    uint32_t k = (uint32_t)((p - ep->ring_phys) / sizeof(struct trb));
                    for (uint32_t s = io->expect; s != k; s = (s + 1) % io->n)   /* skipped over: nothing came */
                        if (io->state[s] == 1) { io->len[s] = 0; io->state[s] = 2; io->missed++; }
                    bool ok = cc == CC_SUCCESS || cc == CC_SHORT;
                    io->len[k] = ok ? (uint16_t)(io->size - MIN(st & 0xFFFFFF, (uint32_t)io->size)) : 0;
                    if (cc == CC_MISSED) io->missed++;
                    io->state[k] = 2; io->expect = (uint16_t)((k + 1) % io->n);
                }
            } else if (ep && ep->busy) {
                if (p == ep->wait_trb || (cc != CC_SUCCESS && cc != CC_SHORT)) {
                    ep->cc = cc; ep->residual = st & 0xFFFFFF; ep->busy = false; ep->done = true;
                } else if (cc == CC_SHORT) ep->data_residual = st & 0xFFFFFF;   /* a control transfer's data stage */
            }
        }
        if (++h->evt_deq == EVT_N) { h->evt_deq = 0; h->evt_cycle ^= 1; }
        any = true;
    }
    if (any) wr64(h->rt, ERDP, (h->evt_phys + (uint64_t)h->evt_deq * sizeof(struct trb)) | 8);   /* 8: clear busy */
}
void xhci_events(void) { for (int i = 0; i < nhc; i++) if (!hcs[i].dead) hc_events(&hcs[i]); }

static int command(struct xhc *h, uint64_t param, uint32_t control) {
    if (h->dead) return -1;
    h->cmd_done = false;
    h->cmd_wait = ring_push(&h->cmd, (uint32_t)param, (uint32_t)(param >> 32), 0, control);
    barrier();
    h->db[0] = 0;
    uint64_t end = pit_ticks() + 3000;
    while (!h->cmd_done) {
        hc_events(h);
        timer_poll();
        if (!h->cmd_done && pit_ticks() > end) {
            logf("xhci: command %u got no answer: controller given up", (control >> 10) & 63);
            h->dead = true;
            return -1;
        }
    }
    return h->cmd_cc;
}

/* ---- contexts ---- */
static uint32_t *ictx(struct xhc *h, int i) { return (uint32_t *)(h->in + (size_t)i * h->csz); }       /* 0 control, 1 slot, 1 + dci */
static uint32_t *octx(struct xhc *h, struct usb_dev *d, int i) { return (uint32_t *)((uint8_t *)d->ctx + (size_t)i * h->csz); }
static void in_clear(struct xhc *h) { memset(h->in, 0, (size_t)h->csz * 33); }
static uint32_t ep_id(const struct usb_ep *ep) { return (uint32_t)ep->dev->slot << 24 | (uint32_t)ep->dci << 16; }

bool xhci_address(struct usb_dev *d) {
    struct xhc *h = &hcs[d->hc];
    if (command(h, 0, T_TYPE(TRB_ENABLE_SLOT)) != CC_SUCCESS || !h->cmd_slot || h->cmd_slot > h->slots) {
        logf("xhci: no device slot free"); return false;
    }
    uint8_t slot = h->cmd_slot;
    uint64_t rphys;
    d->ctx = usb_dma(&d->ctx_phys);
    void *ring = usb_dma(&rphys);
    if (!d->ctx || !ring) {
        if (d->ctx) usb_dma_free(d->ctx);
        if (ring) usb_dma_free(ring);
        d->ctx = 0;
        command(h, 0, T_TYPE(TRB_DISABLE_SLOT) | (uint32_t)slot << 24);
        logf("usb: out of controller memory"); return false;
    }
    d->slot = slot;
    h->dcbaa[slot] = d->ctx_phys;
    d->ep0 = (struct usb_ep){ .dev = d, .dci = 1, .type = EP_CONTROL, .mps = d->mps0, .mem = ring };
    ring_init(&d->ep0, ring, rphys);
    h->devs[slot] = d;
    in_clear(h);
    ictx(h, 0)[1] = 3;                                               /* add: slot, EP0 */
    uint32_t *s = ictx(h, 1);
    s[0] = (d->route & 0xFFFFF) | (uint32_t)d->speed << 20 | 1u << 27;
    s[1] = (uint32_t)d->root_port << 16;
    if (d->tt_slot && (d->speed == USB_FULL || d->speed == USB_LOW)) s[2] = d->tt_slot | (uint32_t)d->tt_port << 8;
    uint32_t *e = ictx(h, 2);
    e[1] = 3u << 1 | 4u << 3 | (uint32_t)d->mps0 << 16;             /* 3 retries, control, max packet */
    e[2] = (uint32_t)rphys | 1; e[3] = (uint32_t)(rphys >> 32);
    e[4] = 8;
    int cc = command(h, h->in_phys, T_TYPE(TRB_ADDRESS) | (uint32_t)slot << 24);
    if (cc != CC_SUCCESS) { logf("xhci: port %d: no address (completion %d)", d->root_port, cc); xhci_free(d); return false; }
    return true;
}

bool xhci_set_mps0(struct usb_dev *d, uint16_t mps) {
    struct xhc *h = &hcs[d->hc];
    in_clear(h);
    ictx(h, 0)[1] = 2;
    uint32_t *e = ictx(h, 2);
    memcpy(e, octx(h, d, 1), h->csz);
    e[0] &= ~7u;
    e[1] = (e[1] & 0xFFFF) | (uint32_t)mps << 16;
    if (command(h, h->in_phys, T_TYPE(TRB_EVALUATE) | (uint32_t)d->slot << 24) != CC_SUCCESS) return false;
    d->mps0 = d->ep0.mps = mps;
    return true;
}

/* the controller's interval: 2^n × 125 µs */
static uint32_t interval(uint8_t speed, uint8_t b) {
    if (speed == USB_HIGH || speed >= USB_SUPER) return (uint32_t)CLAMP(b, 1, 16) - 1;
    uint32_t n = 3;                                                  /* full/low speed: b is in ms */
    while (n < 10 && (1u << (n + 1)) <= (uint32_t)b * 8) n++;
    return n;
}

static void fill_ep(struct xhc *h, struct usb_dev *d, struct usb_ep *ep) {
    bool in = ep->addr & 0x80, iso = ep->type == EP_ISO, periodic = iso || ep->type == EP_INTR;
    uint32_t type = ep->type == EP_BULK ? (in ? 6 : 2) : ep->type == EP_INTR ? (in ? 7 : 3) : (in ? 5 : 1);
    uint32_t esit = periodic ? (uint32_t)ep->mps * (ep->burst + 1u) * (ep->mult + 1u) : 0;   /* bytes a service interval */
    uint32_t ival = iso && (d->speed == USB_FULL || d->speed == USB_LOW) ? (uint32_t)CLAMP(ep->interval, 1, 16) + 2 : interval(d->speed, ep->interval);
    uint64_t deq = ring_deq(ep);
    uint32_t *e = ictx(h, 1 + ep->dci);
    e[0] = periodic ? ival << 16 | (esit >> 16) << 24 | (uint32_t)ep->mult << 8 : 0;
    e[1] = (iso ? 0u : 3u << 1) | type << 3 | (uint32_t)ep->burst << 8 | (uint32_t)ep->mps << 16;   /* isochronous: no retries */
    e[2] = (uint32_t)deq; e[3] = (uint32_t)(deq >> 32);
    e[4] = (periodic ? MAX(1u, esit & 0xFFFF) : 1024u) | (esit & 0xFFFF) << 16;
}
static void slot_in(struct xhc *h, struct usb_dev *d) {
    uint32_t maxdci = 1;
    for (int i = 0; i < d->neps; i++) maxdci = MAX(maxdci, d->ep[i].dci);
    uint32_t *s = ictx(h, 1);
    memcpy(s, octx(h, d, 0), h->csz);
    s[0] = (s[0] & ~(0x1Fu << 27)) | maxdci << 27;
    s[3] = 0;
    if (d->hub) {
        s[0] |= 1u << 26;
        s[1] = (s[1] & 0x00FFFFFF) | (uint32_t)d->hub_ports << 24;
        if (d->speed == USB_HIGH) s[2] = (s[2] & ~(3u << 16)) | (uint32_t)d->hub_ttt << 16;
    }
}

/* endpoints without a ring yet are added; the slot context is updated either way (a hub's ports and think time) */
bool xhci_configure(struct usb_dev *d) {
    struct xhc *h = &hcs[d->hc];
    in_clear(h);
    slot_in(h, d);
    uint32_t add = 1;
    for (int i = 0; i < d->neps; i++) {
        struct usb_ep *ep = &d->ep[i];
        if (ep->ring) continue;
        uint64_t phys; uint8_t *m = usb_dma(&phys);
        if (!m) { logf("usb: out of controller memory"); return false; }
        ep->mem = m; ring_init(ep, m, phys);
        if (ep->type == EP_ISO) { ep->buf = 0; ep->buf_phys = 0; }      /* its slots' buffers come with xhci_iso_start */
        else { ep->buf = m + RING_N * sizeof(struct trb); ep->buf_phys = phys + RING_N * sizeof(struct trb); }
        fill_ep(h, d, ep);
        add |= 1u << ep->dci;
    }
    ictx(h, 0)[1] = add;
    int cc = command(h, h->in_phys, T_TYPE(TRB_CONFIGURE) | (uint32_t)d->slot << 24);
    if (cc != CC_SUCCESS) { logf("xhci: configuring %s failed (completion %d)", d->name, cc); return false; }
    return true;
}

void xhci_free(struct usb_dev *d) {
    struct xhc *h = &hcs[d->hc];
    if (d->slot) {
        h->devs[d->slot] = 0;
        command(h, 0, T_TYPE(TRB_DISABLE_SLOT) | (uint32_t)d->slot << 24);
        h->dcbaa[d->slot] = 0;
        d->slot = 0;
    }
    if (d->ep0.mem) usb_dma_free(d->ep0.mem);
    for (int i = 0; i < d->neps; i++) if (d->ep[i].mem) usb_dma_free(d->ep[i].mem);
    if (d->ctx) usb_dma_free(d->ctx);
    d->ep0.mem = 0; d->ctx = 0; d->neps = 0;
}

/* ---- transfers ---- */
/* A halted endpoint is reset, which also resets its data toggle. One that isn't (a cancelled transfer, a device that
   was told to clear a halt it never had) is stopped, then dropped and added again: the only way to a fresh toggle. */
void xhci_recover(struct usb_ep *ep) {
    struct xhc *h = &hcs[ep->dev->hc]; struct usb_dev *d = ep->dev;
    int cc = command(h, 0, T_TYPE(TRB_RESET_EP) | ep_id(ep));
    if (cc == CC_CONTEXT_STATE) command(h, 0, T_TYPE(TRB_STOP_EP) | ep_id(ep));
    if (cc == CC_CONTEXT_STATE && ep->dci > 1) {
        in_clear(h);
        slot_in(h, d);
        ictx(h, 0)[0] = 1u << ep->dci; ictx(h, 0)[1] = 1u | 1u << ep->dci;
        fill_ep(h, d, ep);
        command(h, h->in_phys, T_TYPE(TRB_CONFIGURE) | (uint32_t)d->slot << 24);
    } else command(h, ring_deq(ep), T_TYPE(TRB_SET_DEQ) | ep_id(ep));
    ep->busy = false; ep->done = false;
}
static void cancel(struct usb_ep *ep) {
    struct xhc *h = &hcs[ep->dev->hc];
    if (command(h, 0, T_TYPE(TRB_STOP_EP) | ep_id(ep)) == CC_CONTEXT_STATE) command(h, 0, T_TYPE(TRB_RESET_EP) | ep_id(ep));
    command(h, ring_deq(ep), T_TYPE(TRB_SET_DEQ) | ep_id(ep));
    ep->busy = false; ep->done = false;
}

bool xhci_wait(struct usb_ep *ep, uint32_t ms) {
    struct xhc *h = &hcs[ep->dev->hc];
    uint64_t end = pit_ticks() + ms;
    while (!ep->done) {
        if (h->dead) return false;
        hc_events(h);
        timer_poll();
        if (!ep->done && pit_ticks() > end) { cancel(ep); return false; }
    }
    return true;
}

/* ---- isochronous IN ---- */
/* Slot k is TRB k of the endpoint's ring (the ring has one TRB more, the link), so completions map straight back. Each
   TRB is a whole TD: a service interval's bytes, the controller told how many packets (TLBPC) and to start at once. */
static void iso_push(struct usb_ep *ep, unsigned k) {
    struct usb_iso *io = ep->iso;
    uint32_t packets = (io->size + ep->mps - 1u) / MAX(1u, (uint32_t)ep->mps);
    uint32_t tlbpc = ep->dev->speed >= USB_SUPER ? (packets % (ep->burst + 1u) ? packets % (ep->burst + 1u) - 1 : ep->burst) : (packets ? packets - 1 : 0);
    uint32_t tbc = ep->dev->speed >= USB_SUPER ? (packets + ep->burst) / (ep->burst + 1u) - 1 : 0;
    uint64_t at = io->phys + (uint64_t)k * io->size;
    io->state[k] = 1;
    ring_push(ep, (uint32_t)at, (uint32_t)(at >> 32), io->size,
              T_TYPE(TRB_ISOCH) | T_IOC | T_ISP | T_SIA | (tbc & 3) << 7 | (tlbpc & 15) << 16);
}
bool xhci_iso_start(struct usb_ep *ep, struct usb_iso *io, uint16_t size, uint8_t *buf, uint64_t phys) {
    struct usb_dev *d = ep->dev;
    if (!ep->ring || !d->slot || ep->type != EP_ISO || hcs[d->hc].dead || rn(ep) < 2 || rn(ep) - 1 > USB_ISO_SLOTS) return false;
    if (ep->enq != 0) return false;                                  /* slot k must be TRB k: a fresh ring */
    memset(io, 0, sizeof *io);
    io->buf = buf; io->phys = phys; io->size = size;
    io->n = (uint16_t)(rn(ep) - 1);                                  /* every TRB but the link */
    ep->iso = io;
    for (unsigned k = 0; k < io->n; k++) iso_push(ep, k);
    barrier();
    hcs[d->hc].db[d->slot] = ep->dci;
    return true;
}
int xhci_iso_take(struct usb_ep *ep, const uint8_t **data) {
    struct usb_iso *io = ep->iso;
    if (!io || io->state[io->tail] != 2) return -1;
    unsigned k = io->tail;
    *data = io->buf + (size_t)k * io->size;
    io->state[k] = 0;                                                /* taken: queued again by the next refill */
    io->tail = (uint16_t)((k + 1) % io->n);
    return io->len[k];
}
void xhci_iso_refill(struct usb_ep *ep) {
    struct usb_dev *d = ep->dev;
    struct usb_iso *io = ep->iso;
    if (!io || hcs[d->hc].dead) return;
    bool any = false;
    while (io->state[io->head] == 0 && io->head != io->tail) {       /* up to the oldest not yet taken */
        iso_push(ep, io->head); io->head = (uint16_t)((io->head + 1) % io->n); any = true;
    }
    if (io->state[io->head] == 0 && io->head == io->tail) {          /* everything taken: all of them again */
        do { iso_push(ep, io->head); io->head = (uint16_t)((io->head + 1) % io->n); any = true; }
        while (io->head != io->tail && io->state[io->head] == 0);
    }
    if (any) { barrier(); hcs[d->hc].db[d->slot] = ep->dci; }
}
void xhci_iso_stop(struct usb_ep *ep) {
    struct xhc *h = &hcs[ep->dev->hc];
    if (!ep->iso) return;
    ep->iso = 0;
    if (h->dead) return;
    if (command(h, 0, T_TYPE(TRB_STOP_EP) | ep_id(ep)) == CC_CONTEXT_STATE) command(h, 0, T_TYPE(TRB_RESET_EP) | ep_id(ep));
    ring_init(ep, (void *)ep->ring, ep->ring_phys);                   /* a fresh ring for the next start */
    command(h, ring_deq(ep), T_TYPE(TRB_SET_DEQ) | ep_id(ep));
}

bool xhci_queue(struct usb_ep *ep, uint64_t phys, uint32_t len) {
    struct usb_dev *d = ep->dev;
    if (!ep->ring || !d->slot || len > 65536 || hcs[d->hc].dead) return false;
    ep->done = false; ep->busy = true; ep->residual = 0;
    ep->wait_trb = ring_push(ep, (uint32_t)phys, (uint32_t)(phys >> 32), len, T_TYPE(TRB_NORMAL) | T_IOC | ((ep->addr & 0x80) ? T_ISP : 0));
    barrier();
    hcs[d->hc].db[d->slot] = ep->dci;
    return true;
}

int xhci_control(struct usb_dev *d, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, void *data, uint16_t len, uint32_t ms) {
    struct xhc *h = &hcs[d->hc]; struct usb_ep *ep = &d->ep0;
    if (len > 4096 || !d->slot || h->dead) return -1;
    bool in = rt & 0x80;
    if (len && !in) memcpy(h->cbuf, data, len);
    ep->done = false; ep->data_residual = 0; ep->busy = true;
    ring_push(ep, rt | (uint32_t)req << 8 | (uint32_t)val << 16, idx | (uint32_t)len << 16, 8,
              T_TYPE(TRB_SETUP) | T_IDT | (len ? (in ? 3u : 2u) : 0u) << 16);
    if (len) ring_push(ep, (uint32_t)h->cbuf_phys, (uint32_t)(h->cbuf_phys >> 32), len, T_TYPE(TRB_DATA) | (in ? T_DIR_IN | T_ISP : 0));
    ep->wait_trb = ring_push(ep, 0, 0, 0, T_TYPE(TRB_STATUS) | T_IOC | (len && in ? 0 : T_DIR_IN));
    barrier();
    h->db[d->slot] = 1;
    if (!xhci_wait(ep, ms)) return -1;
    if (ep->cc != CC_SUCCESS && ep->cc != CC_SHORT) { xhci_recover(ep); return -1; }
    int got = (int)len - (int)MIN(ep->data_residual, len);
    if (in && got > 0) memcpy(data, h->cbuf, (size_t)got);
    return got;
}

/* ---- root ports ---- */
static volatile uint8_t *portsc(struct xhc *h, int port) { return h->op + 0x400 + 0x10 * (port - 1); }
int xhci_ports(int hc) { return hcs[hc].ports; }
bool xhci_port_usb3(int hc, int port) { return (hcs[hc].usb3[(port - 1) >> 5] >> ((port - 1) & 31)) & 1; }
uint32_t xhci_port_status(int hc, int port) {
    volatile uint8_t *r = portsc(&hcs[hc], port);
    uint32_t v = mmio_r32(r);
    if (v & P_CHANGES) mmio_w32(r, (v & P_KEEP) | (v & P_CHANGES));
    return v;
}
int xhci_port_enable(int hc, int port, int recovery_ms) {
    volatile uint8_t *r = portsc(&hcs[hc], port);
    uint32_t v = mmio_r32(r);
    if (!(v & P_CCS)) return 0;
    if (xhci_port_usb3(hc, port)) {                                  /* USB 3: the link trains itself into enabled */
        if (!wait_reg(r, 0, P_PED, P_PED, 300)) {
            mmio_w32(r, (mmio_r32(r) & P_KEEP) | P_WPR);             /* stuck: a warm reset */
            wait_reg(r, 0, P_PRC, P_PRC, 500);
        }
    } else {
        mmio_w32(r, (v & P_KEEP) | P_PR);
        if (!wait_reg(r, 0, P_PRC, P_PRC, 500)) logf("xhci: port %d reset timed out", port);
    }
    v = mmio_r32(r);
    mmio_w32(r, (v & P_KEEP) | (v & P_CHANGES));
    if ((v & (P_CCS | P_PED)) != (P_CCS | P_PED)) return 0;
    sleep_ms(recovery_ms);                                           /* reset recovery: 10 ms by the spec, more for slow devices */
    return (int)((v >> 10) & 15);
}

/* ---- bring-up ---- */
/* The extended capabilities can sit far above the other registers (Intel's start at 0x8000): each one's page is mapped
   before it is read, or the 64-bit kernel faults there. */
static void handoff(struct xhc *h, uint32_t hcc1) {
    uint32_t off = (hcc1 >> 16) << 2;
    for (int n = 0; off && off < (1u << 20) && n < 64; n++) {
        mmio_map(h->phys + off, 16);
        uint32_t v = rd(h->cap, off);
        if ((v & 0xFF) == 1) {                                       /* legacy support: the firmware's SMM driver */
            if (v & (1u << 16)) {
                wr(h->cap, off, v | 1u << 24);
                if (!wait_reg(h->cap, off, 1u << 16, 0, 1000)) { logf("xhci: the firmware kept the controller; taken anyway"); wr(h->cap, off, rd(h->cap, off) & ~(1u << 16)); }
                else logf("xhci: taken over from the firmware");
            }
            uint32_t c = rd(h->cap, off + 4);                        /* its SMIs off, pending ones cleared */
            wr(h->cap, off + 4, (c & ((7u << 1) | (0xFFu << 5) | (7u << 17))) | 7u << 29);
        } else if ((v & 0xFF) == 2 && (v >> 24) == 3) {              /* supported protocol: which ports are USB 3 */
            uint32_t d2 = rd(h->cap, off + 8), first = d2 & 0xFF, count = (d2 >> 8) & 0xFF;
            for (uint32_t p = first; p && p < first + count && p <= 255; p++) h->usb3[(p - 1) >> 5] |= 1u << ((p - 1) & 31);
        }
        uint32_t next = (v >> 8) & 0xFF;
        off = next ? off + next * 4 : 0;
    }
}

/* Intel 7, 8 and 9 series chipsets start with their USB 2 ports on the EHCI controller; move them here */
static void intel_ports(const struct pci_dev *p) {
    if (p->vendor != 0x8086) return;
    switch (p->device) { case 0x1E31: case 0x8C31: case 0x9C31: case 0x8CB1: case 0x9CB1: break; default: return; }
    uint32_t usb3 = pci_read32(p->bus, p->dev, p->fn, 0xDC), usb2 = pci_read32(p->bus, p->dev, p->fn, 0xD4);
    pci_write32(p->bus, p->dev, p->fn, 0xD8, usb3);
    pci_write32(p->bus, p->dev, p->fn, 0xD0, usb2);
    logf("xhci: ports moved over from EHCI (usb2 %x, usb3 %x)", usb2, usb3);
}

static bool hc_start(struct xhc *h, const struct pci_dev *p) {
    bool mem; uint64_t bar = pci_bar(p, 0, &mem);
    if (!mem || !bar) return false;
    if (sizeof(void *) == 4 && (bar >> 32)) { logf("xhci: registers above 4 GiB, out of this 32-bit build's reach"); return false; }
    pci_enable(p);
    pci_write32(p->bus, p->dev, p->fn, 4, pci_read32(p->bus, p->dev, p->fn, 4) | 0x400);   /* polled: its interrupt line off */
    intel_ports(p);
    volatile uint8_t *base = mmio_map(bar, 0x1000);
    uint32_t caplen = rd(base, 0) & 0xFF, hcs1 = rd(base, 4), hcs2 = rd(base, 8), hcc1 = rd(base, 0x10);
    uint32_t dboff = rd(base, 0x14) & ~3u, rtsoff = rd(base, 0x18) & ~0x1Fu;
    if (hcs1 == 0xFFFFFFFF || !caplen) { logf("xhci: registers read all ones"); return false; }
    h->ports = (uint8_t)(hcs1 >> 24);
    uint32_t size = MAX(MAX(caplen + 0x400 + 0x10u * h->ports, dboff + 4u * 256), rtsoff + 0x40);
    base = mmio_map(bar, size);
    h->phys = bar;
    h->cap = base; h->op = base + caplen; h->rt = base + rtsoff; h->db = (volatile uint32_t *)(base + dboff);
    h->csz = (hcc1 & 4) ? 64 : 32;
    handoff(h, hcc1);

    wr(h->op, USBCMD, rd(h->op, USBCMD) & ~1u);                      /* stop, then reset */
    wait_reg(h->op, USBSTS, 1, 1, 50);
    wr(h->op, USBCMD, 2);
    sleep_ms(2);                                                     /* some Intel controllers hang if read during the first ms */
    if (!wait_reg(h->op, USBCMD, 2, 0, 1000) || !wait_reg(h->op, USBSTS, 1u << 11, 0, 1000)) { logf("xhci: reset did not finish"); return false; }
    if (!(rd(h->op, PAGESIZE) & 1)) { logf("xhci: no 4 KiB pages"); return false; }

    h->slots = (uint8_t)MIN(hcs1 & 0xFF, SLOTS);
    wr(h->op, CONFIG, h->slots);
    uint64_t dphys, cphys, ephys;
    h->dcbaa = usb_dma(&dphys);
    void *cmd = usb_dma(&cphys);
    h->evt = usb_mem(EVT_N * sizeof(struct trb), 4096, &ephys); h->evt_phys = ephys;
    h->in = usb_dma(&h->in_phys);
    h->cbuf = usb_dma(&h->cbuf_phys);
    if (!h->dcbaa || !cmd || !h->evt || !h->in || !h->cbuf) { logf("xhci: out of memory"); return false; }
    uint32_t nsp = ((hcs2 >> 27) & 0x1F) | ((hcs2 >> 21) & 0x1F) << 5;   /* scratchpad pages the controller wants */
    if (nsp) {
        uint64_t aphys, pphys;
        uint64_t *arr = usb_mem(nsp * 8, 64, &aphys);
        if (!arr || !usb_mem(nsp * 4096, 4096, &pphys)) { logf("xhci: no memory for %u scratchpad pages", nsp); return false; }
        for (uint32_t i = 0; i < nsp; i++) arr[i] = pphys + (uint64_t)i * 4096;
        h->dcbaa[0] = aphys;
    }
    wr64(h->op, DCBAAP, dphys);
    h->cmd = (struct usb_ep){ 0 };
    ring_init(&h->cmd, cmd, cphys);
    wr64(h->op, CRCR, cphys | 1);
    uint64_t *erst = (uint64_t *)((uint8_t *)h->dcbaa + 2048);        /* one event ring segment */
    erst[0] = ephys; erst[1] = EVT_N;
    h->evt_deq = 0; h->evt_cycle = 1;
    wr(h->rt, ERSTSZ, 1);
    wr64(h->rt, ERDP, ephys);
    wr64(h->rt, ERSTBA, dphys + 2048);
    wr(h->rt, IMOD, 0);
    wr(h->rt, IMAN, 1);                                              /* pending cleared, interrupt off: we poll */
    wr(h->op, USBCMD, 1);
    if (!wait_reg(h->op, USBSTS, 1, 0, 100)) { logf("xhci: would not run"); return false; }
    if (hcc1 & 8)                                                    /* port power switches: all on */
        for (int i = 1; i <= h->ports; i++) { uint32_t v = mmio_r32(portsc(h, i)); if (!(v & P_PP)) mmio_w32(portsc(h, i), (v & P_KEEP) | P_PP); }
    int n3 = 0; for (int i = 1; i <= h->ports; i++) n3 += xhci_port_usb3((int)(h - hcs), i);
    logf("xhci: %02x:%02x.%d %04x:%04x v%x, %u ports (%d USB 3), %u slots, %u scratchpads, capabilities at %x", p->bus, p->dev, p->fn,
         p->vendor, p->device, rd(base, 0) >> 16, h->ports, n3, h->slots, nsp, (hcc1 >> 16) << 2);
    return true;
}

/* what usb_prepare needs to know before anything is touched: the controllers, and their scratchpad pages */
int xhci_probe(uint32_t *pages) {
    struct pci_dev devs[HCS * 2];
    int n = pci_find_all_class(0x0C, 0x03, devs, HCS * 2), found = 0;
    *pages = 0;
    for (int i = 0; i < n && found < HCS; i++) {
        bool mem; uint64_t bar = pci_bar(&devs[i], 0, &mem);
        if (devs[i].prog_if != 0x30 || !mem || !bar || (sizeof(void *) == 4 && (bar >> 32))) continue;
        if (!(pci_read32(devs[i].bus, devs[i].dev, devs[i].fn, 4) & 2)) pci_enable(&devs[i]);   /* the BIOS may be using it: hands off */
        volatile uint8_t *base = mmio_map(bar, 0x1000);
        uint32_t hcs2 = rd(base, 8);
        if (hcs2 == 0xFFFFFFFF) continue;
        *pages += ((hcs2 >> 27) & 0x1F) | ((hcs2 >> 21) & 0x1F) << 5;
        found++;
    }
    return found;
}

int xhci_init(void) {
    struct pci_dev devs[HCS * 2];
    int n = pci_find_all_class(0x0C, 0x03, devs, HCS * 2), older = 0;
    for (int i = 0; i < n; i++) {
        if (devs[i].prog_if != 0x30) { older++; continue; }
        if (nhc == HCS) break;
        struct xhc *h = &hcs[nhc];
        memset(h, 0, sizeof *h);
        if (hc_start(h, &devs[i])) nhc++;
    }
    if (older) logf("usb: %d older controller(s) (UHCI/OHCI/EHCI): no driver for them", older);
    return nhc;
}
