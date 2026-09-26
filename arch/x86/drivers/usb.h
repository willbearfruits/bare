#pragma once
/* Our own USB stack, for when the firmware's is gone (UEFI boots; on a BIOS boot the BIOS keeps serving the stick and
   the keyboard). xhci.c drives USB 3 host controllers — every PC since about 2012 — polled from the main loop, no
   interrupts. usb.c finds devices on the root ports and behind hubs and hands their interfaces to the class drivers:
   keyboards, mice and tablets (usbhid.c), sticks (usbmsc.c) and MIDI (usbmidi.c). */
#include <stdint.h>
#include <stdbool.h>

enum { USB_FULL = 1, USB_LOW = 2, USB_HIGH = 3, USB_SUPER = 4 };     /* xHCI's speed ids */
enum { EP_CONTROL = 0, EP_ISO = 1, EP_BULK = 2, EP_INTR = 3 };      /* bmAttributes & 3 */

#define USB_DEVS     32
#define USB_EPS      6                  /* endpoints a device's drivers may use, besides EP0 */
#define USB_EP_BUF   3072               /* each endpoint's own DMA buffer (reports, MIDI packets, CBW/CSW) */
#define USB_ISO_SLOTS 255               /* an isochronous endpoint's transfers in flight at most (its ring: one 4 KiB block);
                                           32 ms at one a microframe */
#define USB_CAMERA_DMA (768u << 10)     /* set aside at boot for a camera's transfers: 255 of 3 KiB */

struct trb;
struct usb_dev;

struct usb_ep {
    struct usb_dev *dev;
    uint8_t  addr;                      /* 0x81 = EP 1 IN */
    uint8_t  type;                      /* EP_BULK, EP_INTR */
    uint8_t  dci;                       /* device context index: number * 2 + IN (EP0 = 1) */
    uint8_t  interval, burst;           /* bInterval; SuperSpeed companion bMaxBurst, or a high-speed isochronous
                                           endpoint's extra transactions a microframe (wMaxPacketSize bits 11-12) */
    uint8_t  mult;                      /* SuperSpeed isochronous: the companion's Mult */
    uint16_t ring_n;                    /* TRBs in its ring (0: the usual 64) */
    uint16_t mps;
    /* the controller's side */
    volatile struct trb *ring; uint64_t ring_phys; uint16_t enq; uint8_t cycle;
    uint8_t *buf; uint64_t buf_phys;    /* USB_EP_BUF bytes, beside the ring (EP0 uses the controller's buffer) */
    volatile bool busy, done;
    volatile uint8_t cc;                /* completion code: 1 success, 13 short packet, 6 stall, … */
    volatile uint32_t residual;         /* bytes not transferred */
    volatile uint32_t data_residual;    /* control transfers: what the data stage fell short by */
    uint64_t wait_trb;                  /* the TRB whose event ends the transfer */
    void    *mem;
    struct usb_iso *iso;                /* isochronous IN while it streams (xhci_iso_start) */
};

/* isochronous IN: a ring of transfers kept queued, slot k always TRB k of the endpoint's ring and its bytes at
   buf + k * size; the driver's, handed to xhci_iso_start */
struct usb_iso {
    uint16_t n, size, head, tail, expect;
    uint8_t *buf; uint64_t phys;
    volatile uint8_t state[USB_ISO_SLOTS];   /* 0 free, 1 queued, 2 done */
    volatile uint16_t len[USB_ISO_SLOTS];
    volatile uint32_t missed, overruns;
};

struct usb_dev {
    bool     used, configured;
    uint8_t  hc, slot, speed;
    uint8_t  root_port, port, depth;    /* port: on the parent hub, or the root port for depth 0 */
    uint32_t route;                     /* xHCI route string: a hub port per tier below the root */
    struct usb_dev *parent;
    uint8_t  tt_slot, tt_port;          /* full/low speed behind a high-speed hub: that hub's transaction translator */
    uint16_t vid, pid, mps0;
    uint8_t  dclass;
    char     name[32];                  /* the product string, or "vid:pid" */
    struct usb_ep ep0;
    struct usb_ep ep[USB_EPS]; int neps;
    void    *ctx; uint64_t ctx_phys;    /* the output device context */
    bool     hub; uint8_t hub_ports, hub_ttt; /* for the controller's slot context */
};

struct usb_iface { uint8_t num, cls, sub, proto; const uint8_t *desc; int len; const uint8_t *cfg; int cfg_len; };
/* desc: its descriptors, to the next interface; cfg: the whole configuration (other alternate settings live there) */

/* class drivers: probe claims an interface (adding its endpoints with usb_add_ep) and returns its state; start runs
   once the device is configured; poll from the main loop; gone when it is unplugged */
struct usb_driver {
    const char *name;
    void *(*probe)(struct usb_dev *d, const struct usb_iface *i);
    void  (*start)(void *s);
    void  (*poll)(void);
    void  (*gone)(void *s);
};
extern const struct usb_driver usb_hid_driver, usb_msc_driver, usb_midi_driver, usb_hub_driver, usb_net_driver, usb_uvc_driver;

/* usb.c */
bool  usb_prepare(void);                /* at boot: memory set aside for the controllers there are (false: none) */
void  usb_init(void);                   /* every xHCI controller: taken from the firmware, reset, devices found */
void  usb_service(void);                /* main loop (from the key/pointer/MIDI polls): events, reports, hot-plug */
bool  usb_running(void);
bool  usb_prepared(void);               /* memory set aside: usb_init can still run (a BIOS boot) */
struct usb_ep *usb_add_ep(struct usb_dev *d, const struct usb_iface *i, const uint8_t *epdesc);   /* with its SuperSpeed companion */
const uint8_t *usb_next_desc(const struct usb_iface *i, const uint8_t *at, uint8_t type);   /* the next descriptor of a type in i, 0 = none */
int   usb_control(struct usb_dev *d, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, void *data, uint16_t len);
void  usb_clear_halt(struct usb_ep *ep);                    /* after a stall: both ends of the endpoint reset */
struct usb_dev *usb_attach(struct usb_dev *parent, int hc, int port, int speed);   /* a device just reset on a port */
void  usb_detach(struct usb_dev *d);
void *usb_dma(uint64_t *phys);          /* a zeroed 4 KiB block the controller can reach */
void *usb_mem(uint32_t size, uint32_t align, uint64_t *phys);   /* from what usb_prepare set aside, never freed */
void  usb_dma_free(void *p);

/* xhci.c */
int   xhci_probe(uint32_t *scratch_pages);                  /* controllers there are, and the scratchpad pages they want */
int   xhci_init(void);                  /* controllers running */
int   xhci_ports(int hc);
bool  xhci_port_usb3(int hc, int port);
uint32_t xhci_port_status(int hc, int port);                /* PORTSC; changes acknowledged */
int   xhci_port_enable(int hc, int port, int recovery_ms);  /* reset (USB 2) or wait for the link (USB 3): speed, 0 = none */
void  xhci_events(void);
bool  xhci_address(struct usb_dev *d);                     /* a slot, EP0, SET_ADDRESS */
bool  xhci_set_mps0(struct usb_dev *d, uint16_t mps);
bool  xhci_configure(struct usb_dev *d);                   /* the endpoints in d->ep, and the hub fields */
void  xhci_free(struct usb_dev *d);                        /* the slot and its memory */
int   xhci_control(struct usb_dev *d, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, void *data, uint16_t len, uint32_t ms);
bool  xhci_queue(struct usb_ep *ep, uint64_t phys, uint32_t len);   /* one transfer, ≤ 64 KiB, not crossing 64 KiB */
bool  xhci_wait(struct usb_ep *ep, uint32_t ms);           /* false: timed out, and the transfer was cancelled */
/* isochronous IN: a transfer for every TRB of the endpoint's ring but the link (ep->ring_n - 1, set before
   xhci_configure: at most USB_ISO_SLOTS), each a service interval's worth, `size` bytes, at buf + k * size; taken back
   in order */
bool  xhci_iso_start(struct usb_ep *ep, struct usb_iso *iso, uint16_t size, uint8_t *buf, uint64_t phys);
int   xhci_iso_take(struct usb_ep *ep, const uint8_t **data); /* the next finished transfer's length (0: nothing came), -1: none yet */
void  xhci_iso_refill(struct usb_ep *ep);                     /* the slots taken, queued again; the doorbell */
void  xhci_iso_stop(struct usb_ep *ep);                       /* the endpoint stopped, its ring forgotten */
void  xhci_recover(struct usb_ep *ep);                     /* a halted endpoint: reset, and past the failed transfer */

/* class drivers' faces towards the platform */
int   usb_blk_drives(void);
void  usb_blk_rescan(void);                /* forget the numbers of drives that were unplugged */
uint64_t usb_blk_sectors(int d);
const char *usb_blk_name(int d);
bool  usb_blk_io(int d, uint64_t lba, uint32_t n, void *buf, bool write);
int   usb_midi_ports(const char **names, int max);
bool  usb_midi_open(int index);         /* -1 = close */
int   usb_midi_read(uint8_t *buf, int max);
int   usb_midi_write(const uint8_t *buf, int n);
bool  usb_camera(char *name, int cap);
bool  usb_camera_on(bool on);
uint32_t usb_camera_frame(const uint8_t **luma, int *w, int *h);
