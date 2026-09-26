/* USB devices: found on the controllers' root ports (and, through usbhub.c, behind hubs), addressed, described, and
   their interfaces handed to the class drivers. Unplugging and plugging are noticed from the main loop. */
#include "usb.h"
#include "mem.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

/* DMA memory: one piece set aside at boot (later on, everything is the instrument's). From it: scratchpads and the
   stick's buffer (usb_mem, for good), and a pool of 4 KiB blocks for rings, contexts and buffers (usb_dma, freed when a
   device goes) */
#define POOL 192
static uint8_t *res; static uint64_t res_phys; static uint32_t res_size, res_used;
static uint8_t *pool; static uint64_t pool_phys; static uint8_t pool_used[POOL];
void *usb_mem(uint32_t size, uint32_t align, uint64_t *phys) {
    uint64_t p = (res_phys + res_used + align - 1) & ~(uint64_t)(align - 1);
    uint32_t at = (uint32_t)(p - res_phys);
    if (!res || at + size > res_size) return 0;
    res_used = at + size;
    memset(res + at, 0, size);
    *phys = p;
    return res + at;
}
void *usb_dma(uint64_t *phys) {
    for (int i = 0; pool && i < POOL; i++)
        if (!pool_used[i]) { pool_used[i] = 1; memset(pool + i * 4096, 0, 4096); *phys = pool_phys + (uint64_t)i * 4096; return pool + i * 4096; }
    return 0;
}
void usb_dma_free(void *p) { if (p && pool) pool_used[((uint8_t *)p - pool) / 4096] = 0; }

static struct usb_dev devs[USB_DEVS];
static const struct usb_driver *const drivers[] = { &usb_hub_driver, &usb_hid_driver, &usb_msc_driver, &usb_midi_driver, &usb_net_driver, &usb_uvc_driver };
#define BINDS 48
static struct bind { struct usb_dev *d; const struct usb_driver *drv; void *s; } binds[BINDS];
static bool running;
static uint8_t cfg[4096];

bool usb_running(void) { return running; }
bool usb_prepared(void) { return res != 0; }

int usb_control(struct usb_dev *d, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, void *data, uint16_t len) {
    return xhci_control(d, rt, req, val, idx, data, len, 2000);
}
void usb_clear_halt(struct usb_ep *ep) {
    xhci_recover(ep);
    usb_control(ep->dev, 0x02, 1, 0, ep->addr, 0, 0);               /* CLEAR_FEATURE(ENDPOINT_HALT) */
}

const uint8_t *usb_next_desc(const struct usb_iface *i, const uint8_t *at, uint8_t type) {
    const uint8_t *p = at ? at + at[0] : i->desc + i->desc[0], *end = i->desc + i->len;
    for (; p + 2 <= end && p[0] >= 2; p += p[0]) if (p[1] == type) return p;
    return 0;
}
struct usb_ep *usb_add_ep(struct usb_dev *d, const struct usb_iface *i, const uint8_t *e) {
    if (!e || e[0] < 7 || !(e[2] & 15) || d->neps == USB_EPS) return 0;
    struct usb_ep *ep = &d->ep[d->neps++];
    memset(ep, 0, sizeof *ep);
    ep->dev = d; ep->addr = e[2]; ep->type = e[3] & 3; ep->interval = e[6];
    ep->mps = (uint16_t)((e[4] | e[5] << 8) & 0x7FF);
    ep->dci = (uint8_t)((e[2] & 15) * 2 + ((e[2] & 0x80) ? 1 : 0));
    const uint8_t *c = e + e[0];
    if (c + 2 <= i->desc + i->len && c[1] == 0x30 && c[0] >= 6) { ep->burst = c[2]; if (ep->type == EP_ISO) ep->mult = c[3] & 3; }   /* SuperSpeed companion */
    else if (ep->type == EP_ISO && d->speed == USB_HIGH) ep->burst = (uint8_t)((e[5] >> 3) & 3);   /* high bandwidth: 1-3 a microframe */
    if (ep->type == EP_ISO) ep->ring_n = USB_ISO_SLOTS + 1;
    return ep;
}

/* a string descriptor as ASCII */
static void string(struct usb_dev *d, uint8_t index, char *out, int cap) {
    uint8_t b[64];
    if (usb_control(d, 0x80, 6, 0x0300, 0, b, 4) < 4) return;
    uint16_t lang = (uint16_t)(b[2] | b[3] << 8);
    int n = usb_control(d, 0x80, 6, (uint16_t)(0x0300 | index), lang, b, sizeof b);
    if (n < 4) return;
    int k = 0;
    for (int i = 2; i + 1 < MIN(n, b[0]) && k < cap - 1; i += 2) out[k++] = (b[i + 1] || b[i] < 32 || b[i] > 126) ? '?' : (char)b[i];
    while (k && out[k - 1] == ' ') k--;
    if (k) out[k] = 0;
}

static const char *speed_name(int s) { return s == USB_LOW ? "LS" : s == USB_FULL ? "FS" : s == USB_HIGH ? "HS" : "SS"; }

/* descriptors, then a driver for each interface that has one */
static bool describe(struct usb_dev *d) {
    uint8_t dd[18];
    if (usb_control(d, 0x80, 6, 0x0100, 0, dd, 8) < 8) { logf("usb: port %d: no device descriptor", d->port); return false; }
    uint16_t mps = d->speed >= USB_SUPER ? (uint16_t)(1u << MIN(dd[7], 9)) : dd[7];
    if (mps >= 8 && mps != d->mps0 && !xhci_set_mps0(d, mps)) return false;
    if (usb_control(d, 0x80, 6, 0x0100, 0, dd, 18) < 18) return false;
    d->vid = (uint16_t)(dd[8] | dd[9] << 8); d->pid = (uint16_t)(dd[10] | dd[11] << 8); d->dclass = dd[4];
    snfmt(d->name, sizeof d->name, "%04x:%04x", d->vid, d->pid);
    if (dd[15]) string(d, dd[15], d->name, sizeof d->name);
    /* the first configuration; when nothing here drives it, the others (a USB Ethernet adapter's second one speaks the
       standard class, its first the maker's own) */
    char what[48] = ""; int bound = 0, got = 0;
    for (int ci = 0; ci < MAX(dd[17], 1) && ci < 4 && !bound; ci++) {
        got = usb_control(d, 0x80, 6, (uint16_t)(0x0200 | ci), 0, cfg, 9);
        if (got < 9) return false;
        int total = MIN(cfg[2] | cfg[3] << 8, (int)sizeof cfg);
        if (total > 9) got = usb_control(d, 0x80, 6, (uint16_t)(0x0200 | ci), 0, cfg, (uint16_t)total);
        if (got < 9) return false;
        /* the interfaces (first alternate setting), each with its descriptors up to the next */
        struct usb_iface ifs[16]; int nif = 0;
        for (const uint8_t *p = cfg, *end = cfg + got; p + 2 <= end && p[0] >= 2; p += p[0]) {
            if (p[1] != 4 || p[0] < 9) continue;
            if (nif && ifs[nif - 1].len < 0) ifs[nif - 1].len = (int)(p - ifs[nif - 1].desc);
            if (p[3] == 0 && nif < 16) ifs[nif++] = (struct usb_iface){ p[2], p[5], p[6], p[7], p, -1, cfg, got };
        }
        if (nif && ifs[nif - 1].len < 0) ifs[nif - 1].len = (int)(cfg + got - ifs[nif - 1].desc);
        for (int i = 0; i < nif; i++) {
            if (ifs[i].len < 0) ifs[i].len = ifs[i].desc[0];
            for (unsigned k = 0; k < ARRAY_LEN(drivers); k++) {
                int before = d->neps;
                void *s = drivers[k]->probe(d, &ifs[i]);
                if (!s) { d->neps = before; continue; }
                int b = 0; while (b < BINDS && binds[b].d) b++;
                if (b == BINDS) { drivers[k]->gone(s); d->neps = before; break; }
                binds[b] = (struct bind){ d, drivers[k], s };
                snfmt(what + strlen(what), sizeof what - strlen(what), "%s%s", bound ? ", " : "", drivers[k]->name);
                bound++;
                break;
            }
        }
        if (bound && ci) logf("usb: %s: its configuration %d", d->name, ci + 1);
    }
    logf("usb: %d-%d%s %s %04x:%04x %s: %s", d->hc, d->root_port, d->depth ? "+" : "", speed_name(d->speed), d->vid, d->pid, d->name,
         bound ? what : "nothing here drives it");
    if (!bound) return true;
    if (usb_control(d, 0x00, 9, cfg[5], 0, 0, 0) < 0 || !xhci_configure(d)) { logf("usb: %s would not configure", d->name); return false; }
    d->configured = true;
    for (int b = 0; b < BINDS; b++) if (binds[b].d == d) binds[b].drv->start(binds[b].s);
    return true;
}

struct usb_dev *usb_attach(struct usb_dev *parent, int hc, int port, int speed) {
    struct usb_dev *d = 0;
    for (int i = 0; i < USB_DEVS && !d; i++) if (!devs[i].used) d = &devs[i];
    if (!d) { logf("usb: too many devices"); return 0; }
    memset(d, 0, sizeof *d);
    d->used = true; d->hc = (uint8_t)hc; d->speed = (uint8_t)speed; d->port = (uint8_t)port; d->parent = parent;
    if (parent) {
        d->root_port = parent->root_port; d->depth = (uint8_t)(parent->depth + 1);
        d->route = parent->route | (uint32_t)MIN(port, 15) << (4 * parent->depth);
        if (parent->speed == USB_HIGH && (speed == USB_FULL || speed == USB_LOW)) { d->tt_slot = parent->slot; d->tt_port = (uint8_t)port; }
        else { d->tt_slot = parent->tt_slot; d->tt_port = parent->tt_port; }
    } else d->root_port = (uint8_t)port;
    d->mps0 = speed >= USB_SUPER ? 512 : speed == USB_HIGH ? 64 : 8;
    snfmt(d->name, sizeof d->name, "port %d", port);
    if (!xhci_address(d)) { d->used = false; return 0; }
    if (!describe(d)) { usb_detach(d); return 0; }
    return d;
}

void usb_detach(struct usb_dev *d) {
    if (!d->used) return;
    for (int i = 0; i < USB_DEVS; i++) if (devs[i].used && devs[i].parent == d) usb_detach(&devs[i]);
    for (int b = 0; b < BINDS; b++) if (binds[b].d == d) { binds[b].drv->gone(binds[b].s); binds[b].d = 0; }
    xhci_free(d);
    if (d->configured) logf("usb: %s unplugged", d->name);
    d->used = false;
}

/* ---- root ports ----
   A connection is left 100 ms to settle before the port is reset (USB 2.0 7.1.7.3: contacts bounce as a plug goes in).
   A device that doesn't answer is tried again twice, 0.3 s and then 1 s later, with longer recovery after its reset. */
enum { R_EMPTY, R_SETTLING, R_DEVICE, R_FAILED };
#define ROOT_TRIES 3
static uint8_t root_state[4][256], root_tries[4][256];
static uint64_t root_at[4][256];                                     /* when it connected, or when to try again */
static struct usb_dev *root_dev(int hc, int port) {
    for (int i = 0; i < USB_DEVS; i++) if (devs[i].used && !devs[i].parent && devs[i].hc == hc && devs[i].root_port == port) return &devs[i];
    return 0;
}
/* one pass over the root ports: true while something is still on its way (a link training, a plug settling, a retry) */
static bool root_scan(int nhc) {
    bool pending = false;
    uint64_t now = pit_ticks();
    for (int hc = 0; hc < nhc; hc++)
        for (int port = 1; port <= xhci_ports(hc); port++) {
            uint32_t st = xhci_port_status(hc, port);
            bool conn = st & 1, changed = st & (1u << 17);
            uint8_t *state = &root_state[hc][port - 1], *tries = &root_tries[hc][port - 1];
            uint64_t *at = &root_at[hc][port - 1];
            if (*state != R_EMPTY && (!conn || changed)) {           /* gone, or something else there now */
                struct usb_dev *d = root_dev(hc, port);
                if (d) usb_detach(d);
                *state = R_EMPTY;
            }
            if (!conn) { uint32_t pls = (st >> 5) & 15; if (pls == 7 || pls == 8) pending = true; continue; }   /* polling, recovery */
            if (*state == R_DEVICE) continue;
            if (*state == R_EMPTY) { *state = R_SETTLING; *at = now; *tries = 0; }
            if (*state == R_SETTLING && now - *at < 100) { pending = true; continue; }
            if (*state == R_FAILED && (*tries >= ROOT_TRIES || now < *at)) { pending |= *tries < ROOT_TRIES; continue; }
            struct usb_dev *d = 0;
            for (int attempt = 0; attempt < 2 && !d; attempt++) {
                int speed = xhci_port_enable(hc, port, *tries || attempt ? 100 : 10);
                if (speed) d = usb_attach(0, hc, port, speed);
            }
            if (d) { *state = R_DEVICE; continue; }
            *state = R_FAILED; ++*tries; *at = pit_ticks() + (*tries == 1 ? 300 : 1000);
            logf("usb: %d-%d: a device that would not answer%s", hc, port, *tries < ROOT_TRIES ? "; trying again" : "");
            pending |= *tries < ROOT_TRIES;
        }
    return pending;
}

bool usb_prepare(void) {
    uint32_t pages;
    int n = xhci_probe(&pages);
    if (!n) return false;
    uint32_t size = POOL * 4096 + (192u << 10) + pages * 4096 + (uint32_t)n * (8192 + 16384)   /* + the stick's 64 KiB, aligned; event rings */
                  + USB_CAMERA_DMA;                                                          /* a camera's isochronous buffers */
    if (pmm_avail() < size + (4u << 20)) { logf("usb: not enough memory for the controllers"); return false; }
    res = dma_alloc(size, &res_phys); res_size = size; res_used = 0;
    pool = usb_mem(POOL * 4096, 4096, &pool_phys);
    return true;
}

static int nhc;
void usb_init(void) {
    if (running || (!res && !usb_prepare())) return;
    nhc = xhci_init();
    if (!nhc) { logf("usb: no xHCI controller"); return; }
    running = true;
    /* devices on the ports: a USB 2 device shows up as soon as the port has power, a USB 3 one once its link has
       trained; wait while ports are still busy, up to 2.5 s */
    uint64_t t0 = pit_ticks(), quiet = t0;
    while (pit_ticks() - t0 < 2500) {
        if (root_scan(nhc)) quiet = pit_ticks();
        if (pit_ticks() - t0 > 400 && pit_ticks() - quiet > 200) break;
        sleep_ms(10);
    }
    xhci_events();
}

void usb_service(void) {
    static uint64_t last, last_scan; static bool busy;
    if (!running || busy) return;
    uint64_t now = pit_ticks();
    if (now == last) return;
    busy = true; last = now;
    xhci_events();
    for (unsigned k = 0; k < ARRAY_LEN(drivers); k++) drivers[k]->poll();
    if (now - last_scan >= 100) { last_scan = now; root_scan(nhc); }
    busy = false;
}
