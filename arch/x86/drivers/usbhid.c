/* HID: keyboards and mice in the boot protocol, which every one of them speaks; anything else that points — tablets,
   touchscreens, QEMU's tablet — and the consumer keys (volume) through the report descriptor. Keys and pointer events
   join the PS/2 driver's queues, so the rest of the system can't tell the difference. */
#include "usb.h"
#include "usbclass.h"
#include "ps2.h"
#include "libc.h"
#include "log.h"

enum { H_KBD, H_MOUSE, H_REPORT };
#define HIDS 8
struct hid {
    bool used, dead;
    struct usb_dev *d; struct usb_ep *ep;
    uint8_t iface, kind, errors;
    uint16_t rdesc_len;
    uint8_t last[8];                    /* the previous boot keyboard report */
    struct hid_parse p;                 /* report protocol: where the fields are */
    struct hid_state st;
};
static struct hid hids[HIDS];

static void *probe(struct usb_dev *d, const struct usb_iface *i) {
    if (i->cls != 3) return 0;
    const uint8_t *hd = usb_next_desc(i, 0, 0x21);
    struct usb_ep *ep = 0;
    for (const uint8_t *e = usb_next_desc(i, 0, 5); e && !ep; e = usb_next_desc(i, e, 5))
        if ((e[2] & 0x80) && (e[3] & 3) == EP_INTR) ep = usb_add_ep(d, i, e);
    if (!ep) return 0;
    struct hid *h = 0;
    for (int k = 0; k < HIDS && !h; k++) if (!hids[k].used) h = &hids[k];
    if (!h) return 0;
    memset(h, 0, sizeof *h);
    h->used = true; h->d = d; h->ep = ep; h->iface = i->num;
    h->kind = i->sub == 1 && i->proto == 1 ? H_KBD : i->sub == 1 && i->proto == 2 ? H_MOUSE : H_REPORT;
    if (hd && hd[0] >= 9) h->rdesc_len = (uint16_t)(hd[7] | hd[8] << 8);
    if (h->kind == H_REPORT && !h->rdesc_len) { h->used = false; return 0; }
    return h;
}

static void start(void *s) {
    struct hid *h = s;
    if (h->kind != H_REPORT) usb_control(h->d, 0x21, 0x0B, 0, h->iface, 0, 0);    /* SET_PROTOCOL: boot */
    usb_control(h->d, 0x21, 0x0A, 0, h->iface, 0, 0);                           /* SET_IDLE 0: only changes */
    if (h->kind == H_REPORT) {
        static uint8_t rd[4096];
        int n = usb_control(h->d, 0x81, 6, 0x2200, h->iface, rd, (uint16_t)MIN(h->rdesc_len, sizeof rd));
        if (n <= 0 || !hid_parse(&h->p, rd, n)) { h->dead = true; return; }
        if (h->p.touchpad && h->p.has_mode) {                                  /* a precision touchpad: out of mouse mode */
            uint8_t rep[40]; int len = hid_mode_report(&h->p, 3, rep, sizeof rep);   /* SET_REPORT, a feature, its ID first if it has one */
            bool ok = len && usb_control(h->d, 0x21, 0x09, (uint16_t)(0x0300 | rep[0]), h->iface, rep[0] ? rep : rep + 1, (uint16_t)(rep[0] ? len : len - 1)) >= 0;
            logf("usb: %s: precision mode %s", h->d->name, ok ? "on" : "could not be set");
        }
    }
    logf("usb: %s: %s", h->d->name, h->kind == H_KBD ? "keyboard" : h->kind == H_MOUSE ? "mouse" : h->p.touchpad ? "touchpad" :
         h->p.pointer ? (h->p.absolute ? "absolute pointer (tablet, touchscreen)" : "pointer") : "consumer keys");
}

static void report(struct hid *h, const uint8_t *r, int n) {
    struct hid_out o;
    if (h->kind == H_KBD) { if (n >= 8) hid_boot_keys(h->last, r, &o); else return; }
    else if (h->kind == H_MOUSE) { if (n >= 3) hid_boot_mouse(r, n, &o); else return; }
    else hid_report(&h->p, &h->st, r, n, &o);
    for (int k = 0; k < o.nkeys; k++) ps2_inject_key(o.keys[k].code, o.keys[k].down);
    for (int e = 0; e < o.nev; e++) ps2_inject_pointer(&o.ev[e]);
}

static void poll(void) {
    for (int k = 0; k < HIDS; k++) {
        struct hid *h = &hids[k];
        if (!h->used || h->dead || !h->d->configured) continue;
        struct usb_ep *ep = h->ep;
        if (ep->done) {
            ep->done = false;
            if (ep->cc == 1 || ep->cc == 13) { report(h, ep->buf, (int)(ep->mps - MIN(ep->residual, ep->mps))); h->errors = 0; }
            else if (++h->errors > 8) { logf("usb: %s stopped answering", h->d->name); h->dead = true; continue; }
            else usb_clear_halt(ep);
        }
        if (!ep->busy && !ep->done) xhci_queue(ep, ep->buf_phys, MIN(ep->mps, USB_EP_BUF));
    }
}

static void gone(void *s) {
    struct hid *h = s;
    struct hid_out o;
    if (h->kind == H_KBD) { uint8_t none[8] = { 0 }; hid_boot_keys(h->last, none, &o); for (int k = 0; k < o.nkeys; k++) ps2_inject_key(o.keys[k].code, false); }
    h->used = false;
}

const struct usb_driver usb_hid_driver = { "HID", probe, start, poll, gone };
