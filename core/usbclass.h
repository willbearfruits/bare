#pragma once
/* The arithmetic half of the USB and I2C class drivers (the platform's drivers do the transfers): HID reports into key,
   pointer and touchpad events, MIDI bytes into USB-MIDI packets and back, a camera's descriptors and its payloads into
   pictures. Nothing here touches hardware, so the host checks run it too. */
#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

#define HID_EVENTS 12
struct hid_out { struct key_event keys[16]; int nkeys; int nev; struct pointer_event ev[HID_EVENTS]; };

void hid_boot_keys(uint8_t last[8], const uint8_t *r, struct hid_out *o);    /* a boot keyboard report: what changed */
void hid_boot_mouse(const uint8_t *r, int n, struct hid_out *o);

/* the report protocol: the fields a report descriptor declares that mean something here. A precision touchpad (the
   Touch Pad collection) reports touches instead of pointer moves: each Finger collection its contact's ID, tip,
   confidence, position and, on some pads, pressure and size, and the report the number of contacts in the frame. It
   starts in mouse mode until its Input Mode feature is set to 3 (hid_mode_report builds that report). A frame may
   come as one report (all fingers) or several ("hybrid": the first carries the count, the rest 0). Contacts become
   touch events on finger slots 0..HID_FINGERS-1, kept per contact ID until it lifts. */
enum { HF_X, HF_Y, HF_WHEEL, HF_TIP, HF_BUTTON, HF_CONSUMER, HF_CONTACT, HF_CONFIDENCE, HF_COUNT, HF_PRESSURE, HF_WIDTH, HF_HEIGHT };
#define NO_FINGER 0xFF
struct hid_field { uint8_t id, what, size, index, count, finger; bool rel, array, touch; uint16_t bit, usage; int32_t lmin, lmax; };
#define HID_FIELDS 64
#define HID_FINGERS 5
struct hid_parse {
    struct hid_field f[HID_FIELDS]; int nf;
    bool ids, pointer, absolute, consumer, touchpad;
    bool has_mode; uint8_t mode_id, mode_size, mode_len; uint16_t mode_bit;   /* the Input Mode feature: where it sits */
};
struct hid_state {
    uint16_t consumer[4];
    uint8_t left, down, seen;                             /* contacts still due in this frame; slots down; reported */
    int32_t cid[HID_FINGERS]; uint16_t tx[HID_FINGERS], ty[HID_FINGERS];   /* each slot's contact and where it was */
};
bool hid_parse(struct hid_parse *p, const uint8_t *desc, int n);            /* false: nothing in it for us */
void hid_report(const struct hid_parse *p, struct hid_state *s, const uint8_t *r, int n, struct hid_out *o);
int  hid_mode_report(const struct hid_parse *p, uint8_t mode, uint8_t *out, int cap);   /* the feature report: its ID, then its bytes */

/* USB-MIDI 1.0 event packets (cable 0) */
int  umidi_decode(const uint8_t *pk, int n, uint8_t *out, int cap);          /* packets → MIDI bytes; returns bytes */
struct umidi_enc { uint8_t msg[3], n, need, running; bool sysex; };
bool umidi_encode(struct umidi_enc *e, uint8_t b, uint8_t pk[4]);            /* one byte in; true when a packet is out */

/* UVC, the USB video class: a webcam's VideoControl interface names its VideoStreaming ones; the streaming interface's
   alternate setting 0 lists formats (YUY2 and NV12 are read here — uncompressed, the brightness is right there; MJPEG
   would need a decoder) and their frame sizes. Streaming: a probe (the format, size and interval asked for) answered
   by the camera with the bytes it will send a frame and a transfer, committed; then payloads, each with a header (FID
   toggles every frame, EOF ends one, ERR spoils one) and picture bytes, which uvc_payload puts back together. */
enum { UVC_YUY2 = 1, UVC_NV12 = 2 };
#define UVC_FRAMES 24
struct uvc_frame { uint8_t format, index, kind; uint16_t w, h; uint32_t interval; };     /* interval: 100 ns units */
struct uvc_info {
    uint16_t bcd;                                    /* the UVC version (VideoControl header): the probe's length */
    uint8_t vc_if, vs_if, bulk_ep;                   /* interfaces; a bulk endpoint in alternate 0 (0: isochronous) */
    int nframes; struct uvc_frame frames[UVC_FRAMES];
};
bool uvc_parse(const uint8_t *cfg, int len, uint8_t vc_if, struct uvc_info *u);   /* false: no usable streaming format */
int  uvc_pick(const struct uvc_info *u, int want_w);               /* the frame closest to want_w wide, at least 160 */
int  uvc_probe_fill(const struct uvc_info *u, const struct uvc_frame *f, uint8_t out[48]);   /* returns its length */
uint32_t uvc_probe_payload(const uint8_t *p);                     /* dwMaxPayloadTransferSize */
uint32_t uvc_frame_bytes(const struct uvc_frame *f);
struct uvc_asm {
    uint8_t *buf[2]; uint32_t cap;                   /* two frame buffers: one filling, the other the last whole frame */
    int fill, fid; uint32_t len, want; bool err;
    const uint8_t *ready;                            /* the newest whole frame (valid until the next one) */
    uint32_t frames, dropped;
};
void uvc_asm_init(struct uvc_asm *a, uint8_t *b0, uint8_t *b1, uint32_t cap, uint32_t want);
int  uvc_payload(struct uvc_asm *a, const uint8_t *p, int n);    /* 1: a frame finished (a->ready) */
void uvc_luma(const struct uvc_frame *f, const uint8_t *pic, uint8_t *luma);   /* the picture as 8-bit brightness */

