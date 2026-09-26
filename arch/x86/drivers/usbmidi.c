/* USB MIDI (audio class, MIDI streaming): keyboards, controllers, interfaces. Packets come in on a bulk endpoint
   that is always kept waiting; bytes going out are packed four to a packet and sent at once. */
#include "usb.h"
#include "usbclass.h"
#include "libc.h"
#include "log.h"

#define PORTS 4
struct umidi {
    bool used;
    struct usb_dev *d; struct usb_ep *in, *out;
    uint8_t errors;
    char name[24];
};
static struct umidi ports[PORTS];
static struct umidi *open_port;
static uint8_t rx[1024]; static uint32_t rx_w, rx_r;
static struct umidi_enc enc;

static void *probe(struct usb_dev *d, const struct usb_iface *i) {
    if (i->cls != 1 || i->sub != 3) return 0;
    struct umidi *m = 0;
    for (int k = 0; k < PORTS && !m; k++) if (!ports[k].used) m = &ports[k];
    if (!m) return 0;
    struct usb_ep *in = 0, *out = 0;
    for (const uint8_t *e = usb_next_desc(i, 0, 5); e; e = usb_next_desc(i, e, 5)) {
        if ((e[3] & 3) != EP_BULK && (e[3] & 3) != EP_INTR) continue;
        if ((e[2] & 0x80) && !in) in = usb_add_ep(d, i, e);
        else if (!(e[2] & 0x80) && !out) out = usb_add_ep(d, i, e);
    }
    if (!in && !out) return 0;
    memset(m, 0, sizeof *m);
    m->used = true; m->d = d; m->in = in; m->out = out;
    snfmt(m->name, sizeof m->name, "USB %s", d->name);
    return m;
}
static void start(void *s) { (void)s; }

static void poll(void) {
    for (int k = 0; k < PORTS; k++) {
        struct umidi *m = &ports[k];
        if (!m->used || !m->in || !m->d->configured || m->errors > 8) continue;
        struct usb_ep *ep = m->in;
        if (ep->done) {
            ep->done = false;
            if (ep->cc == 1 || ep->cc == 13) {
                uint8_t b[USB_EP_BUF / 4 * 3];
                int n = umidi_decode(ep->buf, (int)(ep->mps - MIN(ep->residual, ep->mps)), b, sizeof b);
                if (m == open_port) for (int i = 0; i < n; i++) if (rx_w - rx_r < sizeof rx) rx[rx_w++ % sizeof rx] = b[i];
                m->errors = 0;
            } else if (++m->errors > 8) { logf("usb: %s stopped answering", m->d->name); continue; }
            else usb_clear_halt(ep);
        }
        if (!ep->busy && !ep->done) xhci_queue(ep, ep->buf_phys, MIN(ep->mps, USB_EP_BUF));
    }
}

static void gone(void *s) {
    struct umidi *m = s;
    if (open_port == m) open_port = 0;
    m->used = false;
}

const struct usb_driver usb_midi_driver = { "MIDI", probe, start, poll, gone };

int usb_midi_ports(const char **names, int max) {
    int n = 0;
    for (int k = 0; k < PORTS && n < max; k++) if (ports[k].used) names[n++] = ports[k].name;
    return n;
}
bool usb_midi_open(int index) {
    open_port = 0; rx_w = rx_r = 0; memset(&enc, 0, sizeof enc);
    if (index < 0) return true;
    for (int k = 0; k < PORTS; k++) if (ports[k].used && index-- == 0) { open_port = &ports[k]; logf("midi: %s open", ports[k].name); return true; }
    return false;
}
int usb_midi_read(uint8_t *b, int max) {
    usb_service();
    int n = 0;
    while (n < max && rx_r != rx_w) b[n++] = rx[rx_r++ % sizeof rx];
    return n;
}
int usb_midi_write(const uint8_t *b, int n) {
    struct umidi *m = open_port;
    if (!m || !m->out || !m->d->configured) return n;                /* nowhere to go: dropped */
    if (m->out->busy) { xhci_events(); if (m->out->busy) return 0; }
    if (m->out->done && m->out->cc != 1) usb_clear_halt(m->out);
    int used = 0, len = 0;
    while (used < n && len + 4 <= (int)MIN(m->out->mps * 4u, USB_EP_BUF)) {
        if (umidi_encode(&enc, b[used++], m->out->buf + len)) len += 4;
    }
    if (len) xhci_queue(m->out, m->out->buf_phys, (uint32_t)len);    /* reaped by the next write, or the poll */
    return used;
}
