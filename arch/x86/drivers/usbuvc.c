/* USB video (UVC) cameras: a laptop's own webcam, or one plugged in. core/usbclass.c reads the descriptors and puts
   payloads back together; here are the transfers. Streaming runs only while something wants pictures (the camera's
   light is on then, and only then): the probe and commit, the streaming interface's alternate setting whose
   isochronous endpoint carries what the camera asked for each microframe (or the bulk endpoint some cameras use),
   then every finished transfer's payload into the frame being put together; each whole frame becomes 8-bit
   brightness for the core (plat_camera_frame). */
#include "usb.h"
#include "usbclass.h"
#include "platform.h"
#include "libc.h"
#include "log.h"

#define ALTS 8
struct alt { uint8_t num; uint16_t bytes; uint8_t desc[9 + 7 + 6]; int len; };   /* an interface descriptor, its endpoint, a companion */
struct cam {
    bool used, on, started;
    struct usb_dev *d;
    struct uvc_info u; struct uvc_frame f;
    struct alt alts[ALTS]; int nalts;
    struct usb_ep *ep; uint8_t alt; uint16_t size;       /* the endpoint, its alternate setting, bytes a transfer */
    uint32_t frame_bytes;
    uint8_t *dma; uint64_t dma_phys;
    uint8_t *pic[2], *luma[2]; uint32_t pic_cap, luma_cap;
    volatile int luma_cur; volatile uint32_t frame_no;
    struct uvc_asm a;
    uint32_t bytes; uint64_t since; uint32_t since_frames;
};
static struct cam cam;
static struct usb_iso iso;
static uint8_t probe_buf[48];
static uint8_t *dma_mem; static uint64_t dma_phys_mem;               /* USB_CAMERA_DMA, taken once, kept for the next camera */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* the streaming interface's alternate settings with an isochronous IN endpoint: copied now, the configuration's
   buffer is the enumerator's and gets reused */
static void collect_alts(struct cam *c, const uint8_t *cfg, int len) {
    c->nalts = 0;
    const uint8_t *iface = 0;
    for (int o = 0; o + 2 <= len && cfg[o] >= 2 && o + cfg[o] <= len; o += cfg[o]) {
        const uint8_t *d = cfg + o;
        if (d[1] == 4 && d[0] >= 9) { iface = d[2] == c->u.vs_if && d[3] ? d : 0; continue; }
        if (!iface || d[1] != 5 || d[0] < 7 || (d[3] & 3) != EP_ISO || !(d[2] & 0x80) || c->nalts >= ALTS) continue;
        struct alt *a = &c->alts[c->nalts++];
        memcpy(a->desc, iface, 9); memcpy(a->desc + 9, d, 7); a->len = 16;
        const uint8_t *cp = d + d[0];
        if (cp + 6 <= cfg + len && cp[1] == 0x30 && cp[0] >= 6) { memcpy(a->desc + 16, cp, 6); a->len = 22; }
        a->num = iface[3];
        uint16_t w = le16(d + 4), mps = w & 0x7FF;
        a->bytes = (uint16_t)(mps * (a->len == 22 ? (cp[2] + 1u) * ((cp[3] & 3) + 1u) : ((w >> 11) & 3) + 1u));
        iface = 0;
    }
}

static void *probe(struct usb_dev *d, const struct usb_iface *i) {
    if (i->cls != 0x0E || i->sub != 1 || cam.used) return 0;          /* the VideoControl interface stands for the camera */
    struct uvc_info u;
    if (!uvc_parse(i->cfg, i->cfg_len, i->num, &u)) { logf("uvc: %s: interface %u: no YUY2 or NV12 pictures (MJPEG only, or infrared)", d->name, i->num); return 0; }
    uint8_t *p0 = cam.pic[0], *p1 = cam.pic[1], *l0 = cam.luma[0], *l1 = cam.luma[1]; uint32_t pc = cam.pic_cap, lc = cam.luma_cap;
    memset(&cam, 0, sizeof cam);
    cam.pic[0] = p0; cam.pic[1] = p1; cam.luma[0] = l0; cam.luma[1] = l1; cam.pic_cap = pc; cam.luma_cap = lc;
    cam.used = true; cam.d = d; cam.u = u;
    collect_alts(&cam, i->cfg, i->cfg_len);
    if (!cam.nalts && !u.bulk_ep) { logf("uvc: %s: no streaming endpoint", d->name); cam.used = false; return 0; }
    return &cam;
}

static void start(void *s) {
    struct cam *c = s;
    int k = uvc_pick(&c->u, 320);
    if (k < 0) { logf("uvc: %s: no picture size we can use", c->d->name); c->used = false; return; }
    c->f = c->u.frames[k];
    c->frame_bytes = uvc_frame_bytes(&c->f);
    uint32_t lb = (uint32_t)c->f.w * c->f.h;
    if (c->pic_cap < c->frame_bytes) { c->pic[0] = plat_alloc(c->frame_bytes); c->pic[1] = plat_alloc(c->frame_bytes); c->pic_cap = c->frame_bytes; }
    if (c->luma_cap < lb) { c->luma[0] = plat_alloc(lb); c->luma[1] = plat_alloc(lb); c->luma_cap = lb; }
    if (!c->pic[0] || !c->pic[1] || !c->luma[0] || !c->luma[1]) { logf("uvc: no memory for %ux%u pictures", c->f.w, c->f.h); c->used = false; return; }
    c->started = true;
    char sizes[80] = ""; int o = 0;
    for (int i = 0; i < c->u.nframes && o < 70; i++) o += snfmt(sizes + o, sizeof sizes - o, "%s%ux%u", i ? " " : "", c->u.frames[i].w, c->u.frames[i].h);
    logf("uvc: %s: UVC %x.%02x, %s; %s %ux%u at %u fps%s, %d isochronous settings", c->d->name, c->u.bcd >> 8, c->u.bcd & 0xFF, sizes,
         c->f.kind == UVC_NV12 ? "NV12" : "YUY2", c->f.w, c->f.h, c->f.interval ? 10000000u / c->f.interval : 0, c->u.bulk_ep ? ", bulk" : "", c->nalts);
}

static bool stream_on(struct cam *c) {
    struct usb_dev *d = c->d;
    int n = uvc_probe_fill(&c->u, &c->f, probe_buf);                /* the probe: what we want; the answer: what comes */
    if (usb_control(d, 0x21, 0x01, 0x0100, c->u.vs_if, probe_buf, (uint16_t)n) < 0) { logf("uvc: the probe was refused"); return false; }
    int got = usb_control(d, 0xA1, 0x81, 0x0100, c->u.vs_if, probe_buf, (uint16_t)n);
    if (got < 26) { logf("uvc: no answer to the probe (%d)", got); return false; }
    uint32_t payload = uvc_probe_payload(probe_buf), fsize = probe_buf[18] | probe_buf[19] << 8 | (uint32_t)probe_buf[20] << 16;
    if (usb_control(d, 0x21, 0x01, 0x0200, c->u.vs_if, probe_buf, (uint16_t)got) < 0) { logf("uvc: the commit was refused"); return false; }
    if (!c->ep) {
        if (!c->nalts) { logf("uvc: a bulk camera: not supported yet"); return false; }
        int best = -1;                                               /* the smallest setting that carries it, else the biggest */
        for (int i = 0; i < c->nalts; i++) {
            bool fits = c->alts[i].bytes >= payload, bf = best >= 0 && c->alts[best].bytes >= payload;
            if (best < 0 || (fits && (!bf || c->alts[i].bytes < c->alts[best].bytes)) || (!fits && !bf && c->alts[i].bytes > c->alts[best].bytes)) best = i;
        }
        struct alt *a = &c->alts[best];
        struct usb_iface ai = { a->desc[2], 0x0E, 2, 0, a->desc, a->len, a->desc, a->len };
        c->ep = usb_add_ep(d, &ai, a->desc + 9);
        if (!c->ep || !a->bytes) { logf("uvc: its endpoint is unusable"); c->ep = 0; return false; }
        c->ep->ring_n = (uint16_t)(MIN(USB_ISO_SLOTS, USB_CAMERA_DMA / a->bytes) + 1);   /* as many transfers as the memory holds */
        if (!xhci_configure(d)) { logf("uvc: its endpoint would not configure"); c->ep = 0; return false; }
        c->alt = a->num; c->size = a->bytes;
    }
    if (!dma_mem) dma_mem = usb_mem(USB_CAMERA_DMA, 4096, &dma_phys_mem);
    if (!dma_mem) { logf("uvc: no DMA memory"); return false; }
    if (usb_control(d, 0x01, 11, c->alt, c->u.vs_if, 0, 0) < 0) { logf("uvc: alternate setting %u refused", c->alt); return false; }
    uvc_asm_init(&c->a, c->pic[0], c->pic[1], c->pic_cap, c->frame_bytes);
    if (!xhci_iso_start(c->ep, &iso, c->size, dma_mem, dma_phys_mem)) { logf("uvc: the stream would not start"); usb_control(d, 0x01, 11, 0, c->u.vs_if, 0, 0); return false; }
    c->on = true; c->bytes = 0; c->since = 0; c->since_frames = 0;
    logf("uvc: streaming: setting %u, %u bytes a transfer (the camera asked for %u), %u in flight, %u-byte frames (%u said)", c->alt, c->size, payload,
         iso.n, c->frame_bytes, fsize);
    return true;
}

static void stream_off(struct cam *c) {
    if (!c->on) return;
    c->on = false;
    if (c->ep) xhci_iso_stop(c->ep);
    usb_control(c->d, 0x01, 11, 0, c->u.vs_if, 0, 0);               /* alternate 0: no bandwidth, the light off */
    logf("uvc: stopped after %u frames (%u dropped, %u transfers missed)", c->a.frames, c->a.dropped, iso.missed);
}

static void poll(void) {
    struct cam *c = &cam;
    if (!c->used || !c->on || !c->ep || !c->d->configured) return;
    const uint8_t *data; int n;
    while ((n = xhci_iso_take(c->ep, &data)) >= 0) {
        if (!n) continue;
        c->bytes += (uint32_t)n;
        if (uvc_payload(&c->a, data, n)) {
            int next = c->luma_cur ^ 1;
            uvc_luma(&c->f, c->a.ready, c->luma[next]);
            c->luma_cur = next; c->frame_no++;
        }
    }
    xhci_iso_refill(c->ep);
    uint64_t now = plat_ms();                                        /* a line in the log every few seconds while it runs */
    if (!c->since) c->since = now;
    if (now - c->since >= 5000) {
        logf("uvc: %u frames in %u ms (%u dropped), %u KB, %u missed, %u overruns", c->a.frames - c->since_frames, (unsigned)(now - c->since),
             c->a.dropped, c->bytes >> 10, iso.missed, iso.overruns);
        c->since = now; c->since_frames = c->a.frames; c->bytes = 0;
    }
}

static void gone(void *s) {
    struct cam *c = s;
    c->on = false; c->ep = 0; c->used = false; c->started = false;
    logf("uvc: the camera went");
}

const struct usb_driver usb_uvc_driver = { "camera", probe, start, poll, gone };

/* for the platform */
bool usb_camera(char *name, int cap) { if (cam.used && cam.started && name) snfmt(name, cap, "%s", cam.d->name); return cam.used && cam.started; }
bool usb_camera_on(bool on) {
    if (!cam.used || !cam.started) return false;
    if (on == cam.on) return true;
    if (on) return stream_on(&cam);
    stream_off(&cam);
    return true;
}
uint32_t usb_camera_frame(const uint8_t **luma, int *w, int *h) {
    if (!cam.used || !cam.frame_no) return 0;
    *luma = cam.luma[cam.luma_cur]; *w = cam.f.w; *h = cam.f.h;
    return cam.frame_no;
}
