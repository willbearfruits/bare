/* Hubs, USB 2 and 3: ports powered, each connection reset and handed to usb_attach, changes read from the hub's
   status-change endpoint (a plug going in gets 100 ms to settle first). A USB 3 hub is two hubs to the host — its USB 2
   half sits on the paired USB 2 port. */
#include "usb.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

#define HUBS 8
#define HUB_PORTS 15
struct hub {
    bool used, dead;
    struct usb_dev *d; struct usb_ep *ep;
    uint8_t nports, errors; bool ss;
    struct usb_dev *child[HUB_PORTS + 1];
    bool failed[HUB_PORTS + 1];
    uint64_t settle[HUB_PORTS + 1];                                  /* when a new connection has settled */
};
static struct hub hubs[HUBS];

static void *probe(struct usb_dev *d, const struct usb_iface *i) {
    if (i->cls != 9) return 0;
    if (d->depth >= 4) { logf("usb: hub %s is too deep in a chain of hubs", d->name); return 0; }
    struct hub *h = 0;
    for (int k = 0; k < HUBS && !h; k++) if (!hubs[k].used) h = &hubs[k];
    if (!h) return 0;
    struct usb_ep *ep = usb_add_ep(d, i, usb_next_desc(i, 0, 5));
    if (!ep || ep->type != EP_INTR || !(ep->addr & 0x80)) return 0;
    memset(h, 0, sizeof *h);
    h->used = true; h->d = d; h->ep = ep; h->ss = d->speed >= USB_SUPER;
    return h;
}

static bool port_status(struct hub *h, int p, uint16_t *status, uint16_t *change) {
    uint8_t st[4];
    if (usb_control(h->d, 0xA3, 0, 0, (uint16_t)p, st, 4) < 4) return false;
    *status = (uint16_t)(st[0] | st[1] << 8); *change = (uint16_t)(st[2] | st[3] << 8);
    return true;
}
static void feature(struct hub *h, bool set, uint16_t f, int p) { usb_control(h->d, 0x23, set ? 3 : 1, f, (uint16_t)p, 0, 0); }

static void port_check(struct hub *h, int p, bool fresh) {
    uint16_t status, change;
    if (!port_status(h, p, &status, &change)) return;
    /* acknowledge every change: connection 16, enable 17, suspend 18, over-current 19, reset 20; USB 3 also link
       state 25, config error 26, warm reset 29 */
    static const uint8_t c2[] = { 16, 17, 18, 19, 20 }, c3[] = { 16, 0, 0, 19, 20, 29, 25, 26 };
    for (int b = 0; b < 8; b++) {
        if (!(change & (1u << b))) continue;
        uint8_t f = h->ss ? c3[b] : b < 5 ? c2[b] : 0;
        if (f) feature(h, false, f, p);
    }
    bool conn = status & 1;
    if (h->child[p] && (!conn || (change & 1))) { usb_detach(h->child[p]); h->child[p] = 0; }
    if (!conn) { h->failed[p] = false; h->settle[p] = 0; return; }
    if (h->child[p] || h->failed[p]) return;
    if (fresh && (change & 1)) { h->settle[p] = pit_ticks() + 100; return; }
    feature(h, true, 4, p);                                          /* PORT_RESET */
    uint64_t end = pit_ticks() + 500;
    do {
        sleep_ms(10);
        if (!port_status(h, p, &status, &change)) return;
    } while (!(change & 0x10) && pit_ticks() < end);
    feature(h, false, 20, p);
    if (!(status & 2)) { h->failed[p] = true; logf("usb: %s port %d would not enable", h->d->name, p); return; }
    int speed = h->ss ? USB_SUPER : (status & 0x200) ? USB_LOW : (status & 0x400) ? USB_HIGH : USB_FULL;
    sleep_ms(10);                                                    /* reset recovery */
    h->child[p] = usb_attach(h->d, h->d->hc, p, speed);
    if (!h->child[p]) h->failed[p] = true;
}

static void start(void *s) {
    struct hub *h = s; struct usb_dev *d = h->d;
    uint8_t desc[16];
    int n = usb_control(d, 0xA0, 6, (uint16_t)((h->ss ? 0x2A : 0x29) << 8), 0, desc, sizeof desc);
    if (n < 7) { logf("usb: hub %s: no hub descriptor", d->name); h->dead = true; return; }
    h->nports = (uint8_t)MIN(desc[2], HUB_PORTS);
    d->hub = true; d->hub_ports = desc[2]; d->hub_ttt = (uint8_t)((desc[3] >> 5) & 3);
    if (!xhci_configure(d)) { h->dead = true; return; }                  /* the slot becomes a hub */
    if (h->ss) usb_control(d, 0x20, 12, d->depth, 0, 0, 0);          /* SET_HUB_DEPTH */
    for (int p = 1; p <= h->nports; p++) feature(h, true, 8, p);     /* PORT_POWER */
    sleep_ms(MAX(desc[5] * 2u, 20u) + 100);                          /* power good, and devices' connect time */
    for (int p = 1; p <= h->nports; p++) port_check(h, p, false);  /* the wait above was their settling time */
    logf("usb: hub %s, %u ports", d->name, h->nports);
}

static void poll(void) {
    for (int k = 0; k < HUBS; k++) {
        struct hub *h = &hubs[k];
        if (!h->used || h->dead || !h->d->configured) continue;
        struct usb_ep *ep = h->ep;
        if (ep->done) {
            ep->done = false;
            if (ep->cc == 1 || ep->cc == 13) {
                uint32_t n = MIN(ep->mps, 4u) - MIN(ep->residual, MIN(ep->mps, 4u));
                uint32_t bits = 0; for (uint32_t i = 0; i < n; i++) bits |= (uint32_t)ep->buf[i] << (8 * i);
                for (int p = 1; p <= h->nports; p++) if (bits & (1u << p)) port_check(h, p, true);
                h->errors = 0;
            } else if (++h->errors > 8) { logf("usb: hub %s stopped answering", h->d->name); h->dead = true; continue; }
            else usb_clear_halt(ep);
        }
        for (int p = 1; p <= h->nports; p++)
            if (h->settle[p] && pit_ticks() >= h->settle[p]) { h->settle[p] = 0; port_check(h, p, false); }
        if (!ep->busy && !ep->done) xhci_queue(ep, ep->buf_phys, MIN(ep->mps, 4u));
    }
}

static void gone(void *s) { struct hub *h = s; h->used = false; }

const struct usb_driver usb_hub_driver = { "hub", probe, start, poll, gone };
