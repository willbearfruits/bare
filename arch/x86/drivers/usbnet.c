/* USB Ethernet adapters of the communications class: CDC-ECM (a frame per bulk transfer) and CDC-NCM (frames packed
   into "transfer blocks"), which USB-C adapters made for Macs and iPads speak, often in their second configuration
   (usb.c tries it). The control interface's functional descriptors name the data interface and a string holding the
   MAC address; the data interface's second alternate setting has the bulk endpoints; the link state comes as a
   NETWORK_CONNECTION notification on the interrupt endpoint. */
#include "usb.h"
#include "netdev.h"
#include "libc.h"
#include "log.h"

#define UNETS 2
#define QN 8
struct unet {
    bool used, ncm, link, told;
    struct usb_dev *d; struct usb_ep *in, *out, *irq;
    uint8_t ctl_if, data_if, data_alt, mac_str;
    struct netdev *nd;
    uint16_t seq, out_div, out_rem;
    uint8_t q[QN][1536]; int qlen[QN]; unsigned qh, qt;             /* frames received, for the stack */
};
static struct unet nets[UNETS];

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void put16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* an alternate setting of an interface, from the whole configuration */
static bool find_alt(const struct usb_iface *i, uint8_t num, uint8_t alt, struct usb_iface *out) {
    const uint8_t *p = i->cfg, *end = i->cfg + i->cfg_len, *at = 0;
    for (; p + 2 <= end && p[0] >= 2; p += p[0]) {
        if (p[1] != 4 || p[0] < 9) continue;
        if (at) { out->len = (int)(p - at); return true; }
        if (p[2] == num && p[3] == alt) { at = p; *out = (struct usb_iface){ p[2], p[5], p[6], p[7], p, 0, i->cfg, i->cfg_len }; }
    }
    if (at) { out->len = (int)(end - at); return true; }
    return false;
}

static void *probe(struct usb_dev *d, const struct usb_iface *i) {
    if (i->cls != 2 || (i->sub != 6 && i->sub != 0x0D)) return 0;       /* communications: ECM, NCM */
    struct unet *u = 0;
    for (int k = 0; k < UNETS && !u; k++) if (!nets[k].used) u = &nets[k];
    if (!u) return 0;
    int data_if = -1, mac_str = 0;
    for (const uint8_t *p = i->desc + i->desc[0]; p + 3 <= i->desc + i->len && p[0] >= 3; p += p[0]) {
        if (p[1] != 0x24) continue;                                       /* class-specific interface descriptors */
        if (p[2] == 0x06 && p[0] >= 5) data_if = p[4];                    /* union: the data interface */
        if (p[2] == 0x0F && p[0] >= 4) mac_str = p[3];                    /* Ethernet: the MAC address's string */
    }
    if (data_if < 0) return 0;
    struct usb_iface di; struct usb_ep *in = 0, *out = 0, *irq = 0;
    int alt = 1;
    if (!find_alt(i, (uint8_t)data_if, 1, &di)) { if (!find_alt(i, (uint8_t)data_if, 0, &di)) return 0; alt = 0; }
    for (const uint8_t *e = usb_next_desc(&di, 0, 5); e; e = usb_next_desc(&di, e, 5)) {
        if ((e[3] & 3) != EP_BULK) continue;
        if ((e[2] & 0x80) && !in) in = usb_add_ep(d, &di, e);
        else if (!(e[2] & 0x80) && !out) out = usb_add_ep(d, &di, e);
    }
    const uint8_t *ie = usb_next_desc(i, 0, 5);
    if (ie && (ie[3] & 3) == EP_INTR && (ie[2] & 0x80)) irq = usb_add_ep(d, i, ie);
    if (!in || !out) return 0;
    memset(u, 0, sizeof *u);
    u->used = true; u->ncm = i->sub == 0x0D; u->d = d; u->in = in; u->out = out; u->irq = irq;
    u->ctl_if = i->num; u->data_if = (uint8_t)data_if; u->data_alt = (uint8_t)alt; u->mac_str = (uint8_t)mac_str;
    u->out_div = 4; u->out_rem = 0;
    return u;
}

static bool link(struct netdev *n) { struct unet *u = n->ctx; return u->used && u->link; }
static int recv(struct netdev *n, void *f, int cap) {
    struct unet *u = n->ctx;
    if (u->qt == u->qh) return 0;
    int len = MIN(u->qlen[u->qt % QN], cap);
    memcpy(f, u->q[u->qt % QN], (size_t)len);
    u->qt++;
    return len;
}
static void keep(struct unet *u, const uint8_t *f, int len) {
    if (len < 14 || len > 1536 || u->qh - u->qt >= QN) return;             /* too short, too long, or the queue is full */
    memcpy(u->q[u->qh % QN], f, (size_t)len); u->qlen[u->qh % QN] = len; u->qh++;
}
static bool send(struct netdev *n, const void *f, int len) {
    struct unet *u = n->ctx;
    if (!u->used || len > 1514) return false;
    for (int i = 0; i < 50 && u->out->busy; i++) xhci_events();          /* the last one still going */
    if (u->out->busy) return false;
    uint8_t *b = u->out->buf; int total;
    if (!u->ncm) { memcpy(b, f, (size_t)len); total = len; }
    else {                                                                /* a transfer block: its header, one index, the frame */
        int dg = ((28 + u->out_div - 1 - u->out_rem) / u->out_div) * u->out_div + u->out_rem;
        total = dg + len;
        memset(b, 0, (size_t)dg);
        memcpy(b, "NCMH", 4); put16le(b + 4, 12); put16le(b + 6, u->seq++); put16le(b + 8, (uint16_t)total); put16le(b + 10, 12);
        memcpy(b + 12, "NCM0", 4); put16le(b + 16, 16); put16le(b + 18, 0); put16le(b + 20, (uint16_t)dg); put16le(b + 22, (uint16_t)len);
        memcpy(b + dg, f, (size_t)len);
    }
    if (total % u->out->mps == 0) b[total++] = 0;                           /* never a whole packet at the end: a byte more */
    if (u->ncm) put16le(b + 8, (uint16_t)total);
    return xhci_queue(u->out, u->out->buf_phys, (uint32_t)total);
}
static void mc(struct netdev *n, bool all) { (void)n; (void)all; }       /* the filter takes every multicast already */

static void start(void *s) {
    struct unet *u = s; struct usb_dev *d = u->d;
    if (u->ncm) {                                                         /* its block sizes; ours in: 2 KiB */
        uint8_t p[28];
        if (usb_control(d, 0xA1, 0x80, 0, u->ctl_if, p, sizeof p) >= 28) { u->out_div = MAX(le16(p + 18), 1); u->out_rem = le16(p + 20) % u->out_div; }
        uint8_t sz[4] = { 0x00, 0x08, 0, 0 };
        usb_control(d, 0x21, 0x86, 0, u->ctl_if, sz, 4);
    }
    if (u->data_alt) usb_control(d, 0x01, 11, u->data_alt, u->data_if, 0, 0);             /* SET_INTERFACE: the endpoints on */
    usb_control(d, 0x21, 0x43, 0x0E, u->ctl_if, 0, 0);                   /* frames to us, broadcasts, every multicast */
    uint8_t mac[6] = { 0x02, 0x48, 0x42, 0, 0, 0 };                      /* a local one, if it has none */
    mac[3] = (uint8_t)d->vid; mac[4] = (uint8_t)d->pid; mac[5] = (uint8_t)(d->root_port * 16 + d->port);
    if (u->mac_str) {
        uint8_t b[64]; char hex[13] = "";
        if (usb_control(d, 0x80, 6, 0x0300, 0, b, 4) >= 4) {
            uint16_t lang = (uint16_t)(b[2] | b[3] << 8);
            int n = usb_control(d, 0x80, 6, (uint16_t)(0x0300 | u->mac_str), lang, b, sizeof b);
            for (int i = 0; i < 12 && 2 + 2 * i < n; i++) hex[i] = (char)b[2 + 2 * i];
        }
        int ok = 0;
        for (int i = 0; i < 12; i++) ok += (hex[i] >= '0' && hex[i] <= '9') || ((hex[i] | 32) >= 'a' && (hex[i] | 32) <= 'f');
        if (ok == 12) for (int i = 0; i < 6; i++) {
            int hi = hex[2 * i] <= '9' ? hex[2 * i] - '0' : (hex[2 * i] | 32) - 'a' + 10, lo = hex[2 * i + 1] <= '9' ? hex[2 * i + 1] - '0' : (hex[2 * i + 1] | 32) - 'a' + 10;
            mac[i] = (uint8_t)(hi << 4 | lo);
        }
    }
    u->nd = netdev_add();
    if (!u->nd) return;
    snfmt(u->nd->name, sizeof u->nd->name, "USB %s (%s)", d->name, u->ncm ? "NCM" : "ECM");
    memcpy(u->nd->mac, mac, 6); u->nd->link = link; u->nd->send = send; u->nd->recv = recv; u->nd->multicast = mc; u->nd->ctx = u;
    if (!u->irq) u->link = true;                                          /* nothing will say: assume a cable */
    logf("usb: %s: Ethernet %02x:%02x:%02x:%02x:%02x:%02x", u->nd->name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* an NCM transfer block in: its datagram tables, each frame out of it */
static void ntb_in(struct unet *u, const uint8_t *b, int n) {
    if (n < 12 || memcmp(b, "NCMH", 4)) return;
    int ndp = le16(b + 10);
    for (int hops = 0; ndp && ndp + 8 <= n && hops < 8; hops++) {
        if (memcmp(b + ndp, "NCM", 3)) return;
        int nl = le16(b + ndp + 4);
        for (int e = ndp + 8; e + 4 <= ndp + nl && e + 4 <= n; e += 4) {
            int at = le16(b + e), len = le16(b + e + 2);
            if (!at || !len) break;
            if (at + len <= n) keep(u, b + at, len);
        }
        ndp = le16(b + ndp + 6);
    }
}

static void poll(void) {
    for (int k = 0; k < UNETS; k++) {
        struct unet *u = &nets[k];
        if (!u->used || !u->d->configured || !u->nd) continue;
        if (u->in->done) {
            u->in->done = false;
            if (u->in->cc == 1 || u->in->cc == 13) {
                int n = (int)(USB_EP_BUF - MIN(u->in->residual, (uint32_t)USB_EP_BUF));
                if (u->ncm) ntb_in(u, u->in->buf, n); else keep(u, u->in->buf, n);
            } else usb_clear_halt(u->in);
        }
        if (!u->in->busy && !u->in->done) xhci_queue(u->in, u->in->buf_phys, USB_EP_BUF);
        if (u->irq) {
            if (u->irq->done) {
                u->irq->done = false;
                const uint8_t *m = u->irq->buf;
                if ((u->irq->cc == 1 || u->irq->cc == 13) && m[0] == 0xA1 && m[1] == 0x00) {   /* NETWORK_CONNECTION */
                    bool l = le16(m + 2) != 0;
                    if (l != u->link || !u->told) logf("usb: %s: %s", u->nd->name, l ? "cable in" : "no cable");
                    u->link = l; u->told = true;
                }
            }
            if (!u->irq->busy && !u->irq->done) xhci_queue(u->irq, u->irq->buf_phys, MIN(u->irq->mps, 64u));
        }
        if (u->out->done) u->out->done = false;
    }
}

static void gone(void *s) {
    struct unet *u = s;
    if (u->nd) netdev_remove(u->nd);
    u->used = false;
}

const struct usb_driver usb_net_driver = { "Ethernet", probe, start, poll, gone };
