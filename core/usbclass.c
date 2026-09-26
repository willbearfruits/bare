#include "usbclass.h"
#include "keys.h"
#include "libc.h"

/* ---- keyboards: HID usage (page 7) → key code; the keypad acts as the arrows, as on PS/2 ---- */
static const uint8_t usage_key[0x68] = {
    [0x04] = 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z',
    [0x1E] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    [0x28] = KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_TAB, KEY_SPACE, '-', '=', '[', ']', '\\', '\\', ';', '\'', '`', ',', '.', '/',
    KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    [0x46] = KEY_PRTSC, KEY_SCROLL, 0, KEY_INSERT, KEY_HOME, KEY_PGUP, KEY_DELETE, KEY_END, KEY_PGDN,
    KEY_RIGHT, KEY_LEFT, KEY_DOWN, KEY_UP,
    [0x54] = '/', '*', '-', '=', KEY_ENTER, KEY_END, KEY_DOWN, KEY_PGDN, KEY_LEFT, 0, KEY_RIGHT, KEY_HOME, KEY_UP, KEY_PGUP,
    KEY_INSERT, KEY_DELETE, '\\',
};
static uint8_t key_of(uint8_t u) {
    if (u < sizeof usage_key) return usage_key[u];
    return u == 0x7F ? KEY_MUTE : u == 0x80 ? KEY_VOLUP : u == 0x81 ? KEY_VOLDOWN : 0;
}
static void add(struct hid_out *o, uint8_t code, bool down) {
    if (code && o->nkeys < (int)ARRAY_LEN(o->keys)) o->keys[o->nkeys++] = (struct key_event){ code, down };
}
static bool has(const uint8_t *r, uint8_t u) { for (int i = 2; i < 8; i++) if (r[i] == u) return true; return false; }

void hid_boot_keys(uint8_t last[8], const uint8_t *r, struct hid_out *o) {
    o->nkeys = 0; o->nev = 0;
    if (r[2] == 1) return;                                        /* too many keys at once: this report says nothing */
    static const uint8_t mods[8] = { KEY_LCTRL, KEY_LSHIFT, KEY_LALT, KEY_LMETA, KEY_RCTRL, KEY_RSHIFT, KEY_RALT, KEY_LMETA };
    for (int b = 0; b < 8; b++) if ((r[0] ^ last[0]) & (1u << b)) add(o, mods[b], r[0] & (1u << b));
    for (int i = 2; i < 8; i++) if (last[i] > 3 && !has(r, last[i])) add(o, key_of(last[i]), false);
    for (int i = 2; i < 8; i++) if (r[i] > 3 && !has(last, r[i])) add(o, key_of(r[i]), true);
    memcpy(last, r, 8);
}

void hid_boot_mouse(const uint8_t *r, int n, struct hid_out *o) {
    o->nkeys = 0; o->nev = n >= 3;
    o->ev[0] = (struct pointer_event){ (int8_t)r[1], (int8_t)r[2], 0, 0, 0, (uint8_t)(r[0] & 7) };   /* y grows downwards, as ours */
}

/* ---- report descriptors ---- */
struct pos { uint8_t id; uint16_t bit; };
static uint16_t *bitpos(struct pos *t, int *nt, uint8_t id) {
    for (int i = 0; i < *nt; i++) if (t[i].id == id) return &t[i].bit;
    if (*nt == 16) return &t[15].bit;
    t[*nt] = (struct pos){ id, 0 };
    return &t[(*nt)++].bit;
}

bool hid_parse(struct hid_parse *p, const uint8_t *d, int n) {
    memset(p, 0, sizeof *p);
    uint32_t page = 0, usages[16], umin = 0, umax = 0, rsize = 0, rcount = 0, app = 0;
    int nu = 0, depth = 0; bool range = false; int32_t lmin = 0, lmax = 0; uint32_t lmax_u = 0; uint8_t id = 0;
    struct pos pos[16], fpos[16], fingers[16]; int npos = 0, nfpos = 0, nfing = 0;
    uint8_t finger = NO_FINGER; int finger_depth = 0;             /* inside a touchpad's Finger collection: which one */
    bool seen[4] = { false };                                     /* the first X, Y, wheel, tip of a report only (touchpads repeat them per finger) */
    for (int i = 0; i < n;) {
        uint8_t b = d[i++];
        if (b == 0xFE) { if (i + 1 >= n) break; i += 2 + d[i]; continue; }   /* a long item */
        int sz = (b & 3) == 3 ? 4 : b & 3;
        if (i + sz > n) break;
        uint32_t v = 0;
        for (int k = 0; k < sz; k++) v |= (uint32_t)d[i + k] << (8 * k);
        int32_t sv = sz == 1 ? (int8_t)v : sz == 2 ? (int16_t)v : (int32_t)v;
        i += sz;
        switch (b & 0xFC) {
        case 0x04: page = v; break;                               /* usage page */
        case 0x14: lmin = sv; break;
        case 0x24: lmax = sv; lmax_u = v; break;
        case 0x74: rsize = v; break;
        case 0x84: id = (uint8_t)v; p->ids = true; memset(seen, 0, sizeof seen); break;
        case 0x94: rcount = v; break;
        case 0x08: if (nu < 16) usages[nu++] = sz == 4 ? v : page << 16 | v; break;
        case 0x18: umin = sz == 4 ? v : page << 16 | v; range = true; break;
        case 0x28: umax = sz == 4 ? v : page << 16 | v; range = true; break;
        case 0x80: {                                              /* input */
            uint16_t *bp = bitpos(pos, &npos, id);
            int32_t hi = lmax < lmin ? (int32_t)lmax_u : lmax;    /* a maximum written without room for its sign */
            bool touch = app == 0x0D0005;                         /* the Touch Pad application collection */
            if ((v & 1) || !rsize) { *bp = (uint16_t)(*bp + rsize * rcount); break; }
            if (!(v & 2)) {                                       /* an array: codes of what is pressed */
                uint32_t base = range ? umin : nu ? usages[0] : 0;
                if ((base >> 16) == 0x0C && p->nf < HID_FIELDS && rsize <= 16) {
                    p->f[p->nf++] = (struct hid_field){ id, HF_CONSUMER, (uint8_t)rsize, 0, (uint8_t)MIN(rcount, 4u), NO_FINGER, false, true, false, *bp, (uint16_t)base, lmin, hi };
                    p->consumer = true;
                }
                *bp = (uint16_t)(*bp + rsize * rcount);
                break;
            }
            for (uint32_t k = 0; k < rcount; k++, *bp = (uint16_t)(*bp + rsize)) {
                uint32_t u = nu ? usages[MIN(k, (uint32_t)nu - 1)] : range && umin + k <= umax ? umin + k : 0;
                int what = -1, index = 0;
                if (u == 0x010030) what = HF_X;
                else if (u == 0x010031) what = HF_Y;
                else if (u == 0x010038) what = HF_WHEEL;
                else if ((u >> 16) == 9 && (u & 0xFFFF) >= 1 && (u & 0xFFFF) <= 3) { what = HF_BUTTON; index = (int)(u & 0xFFFF) - 1; }
                else if (u == 0x0D0042) what = touch ? HF_TIP : HF_BUTTON;        /* a finger down; a pen's tip */
                else if (u == 0x0D0044) { what = HF_BUTTON; index = 1; }         /* the pen's barrel button */
                else if (u == 0x0C00E9 || u == 0x0C00EA || u == 0x0C00E2) what = HF_CONSUMER;
                else if (touch && u == 0x0D0051) what = HF_CONTACT;
                else if (touch && u == 0x0D0047) what = HF_CONFIDENCE;
                else if (touch && u == 0x0D0054) what = HF_COUNT;
                else if (touch && u == 0x0D0030) what = HF_PRESSURE;
                else if (touch && u == 0x0D0048) what = HF_WIDTH;
                else if (touch && u == 0x0D0049) what = HF_HEIGHT;
                if (what < 0 || rsize > 32 || p->nf == HID_FIELDS) continue;
                bool per_finger = touch && finger != NO_FINGER;
                if (per_finger && finger >= HID_FINGERS + 3) continue;    /* more fingers than any frame will use */
                if (what <= HF_TIP && !per_finger) { if (seen[what]) continue; seen[what] = true; }
                p->f[p->nf++] = (struct hid_field){ id, (uint8_t)what, (uint8_t)rsize, (uint8_t)index, 1, per_finger ? finger : NO_FINGER,
                                                    (v & 4) != 0, false, touch, *bp, (uint16_t)u, lmin, hi };
                if (what == HF_X) { p->pointer = true; p->absolute = !(v & 4); p->touchpad |= touch; }
                if (what == HF_CONSUMER) p->consumer = true;
            }
            nu = 0; range = false;
            break;
        }
        case 0xB0: {                                              /* feature: only the touchpad's Input Mode matters */
            uint16_t *bp = bitpos(fpos, &nfpos, id);
            for (uint32_t k = 0; k < rcount; k++, *bp = (uint16_t)(*bp + rsize)) {
                uint32_t u = (v & 1) ? 0 : nu ? usages[MIN(k, (uint32_t)nu - 1)] : range && umin + k <= umax ? umin + k : 0;
                if (u == 0x0D0052 && !p->has_mode && rsize <= 8) { p->has_mode = true; p->mode_id = id; p->mode_bit = *bp; p->mode_size = (uint8_t)rsize; }
            }
            nu = 0; range = false;
            break;
        }
        case 0xA0:                                                /* collection: an application one names the device */
            if (depth == 0 && v == 1) app = nu ? usages[nu - 1] : 0;
            else if (app == 0x0D0005 && finger == NO_FINGER && nu && usages[nu - 1] == 0x0D0022) {   /* a Finger */
                uint16_t *fc = bitpos(fingers, &nfing, id);
                finger = (uint8_t)MIN(*fc, 254); (*fc)++; finger_depth = depth;
            }
            depth++;
            nu = 0; range = false;
            break;
        case 0xC0:
            if (depth) depth--;
            if (!depth) app = 0;
            if (finger != NO_FINGER && depth == finger_depth) finger = NO_FINGER;
            nu = 0; range = false;
            break;
        case 0x90: nu = 0; range = false; break;                  /* output */
        }
    }
    if (p->has_mode) p->mode_len = (uint8_t)MIN((*bitpos(fpos, &nfpos, p->mode_id) + 7u) / 8, 32u);
    return p->pointer || p->consumer;
}

int hid_mode_report(const struct hid_parse *p, uint8_t mode, uint8_t *out, int cap) {
    if (!p->has_mode || cap < 1 + p->mode_len) return 0;
    memset(out, 0, (size_t)(1 + p->mode_len));
    out[0] = p->mode_id;
    for (int k = 0; k < p->mode_size; k++)
        if ((mode >> k) & 1) { uint32_t b = p->mode_bit + (uint32_t)k; out[1 + (b >> 3)] |= (uint8_t)(1u << (b & 7)); }
    return 1 + p->mode_len;
}

static int32_t field(const uint8_t *r, int n, uint32_t at, int size, bool sign) {
    uint32_t v = 0;
    for (int k = 0; k < size; k++) { uint32_t b = at + (uint32_t)k; if ((int)(b >> 3) < n && ((r[b >> 3] >> (b & 7)) & 1)) v |= 1u << k; }
    if (sign && size < 32 && ((v >> (size - 1)) & 1)) v |= ~0u << size;
    return (int32_t)v;
}
static uint8_t consumer_key(uint16_t u) { return u == 0xE9 ? KEY_VOLUP : u == 0xEA ? KEY_VOLDOWN : u == 0xE2 ? KEY_MUTE : 0; }

static int32_t scaled(const struct hid_field *f, int32_t v, int32_t to) {       /* lmin..lmax → 0..to */
    if (f->lmax <= f->lmin) return 0;
    return (int32_t)((int64_t)(CLAMP(v, f->lmin, f->lmax) - f->lmin) * to / (f->lmax - f->lmin));
}

static void touch_event(struct hid_out *o, int slot, uint8_t buttons, int z, int size, uint16_t x, uint16_t y) {
    if (o->nev < HID_EVENTS)
        o->ev[o->nev++] = (struct pointer_event){ 0, 0, 0, 0, 0, buttons, 1, (uint8_t)z, x, y, (uint8_t)slot, (uint8_t)size };
}
static int slot_of(struct hid_state *s, int32_t cid, bool take) {
    for (int k = 0; k < HID_FINGERS; k++) if ((s->down >> k & 1) && s->cid[k] == cid) return k;
    if (take) for (int k = 0; k < HID_FINGERS; k++) if (!(s->down >> k & 1)) return k;
    return -1;
}

/* a precision touchpad's report: its fingers, then, at the end of a frame, the ones it stopped mentioning are lifted */
static void touch_report(const struct hid_parse *p, struct hid_state *s, uint8_t id, const uint8_t *r, int n, struct hid_out *o) {
    struct { bool tip, conf, has_conf, has_cid, has_z, has_size; int32_t cid, x, y, z, w, h; } f[HID_FINGERS + 3];
    memset(f, 0, sizeof f);
    int nf = 0, count = -1; uint8_t buttons = 0;
    for (int i = 0; i < p->nf; i++) {
        const struct hid_field *d = &p->f[i];
        if (d->id != id || !d->touch || d->array) continue;
        int32_t v = field(r, n, d->bit, d->size, d->lmin < 0);
        if (d->finger == NO_FINGER) {
            if (d->what == HF_COUNT) count = v;
            else if (d->what == HF_BUTTON && v) buttons |= (uint8_t)(1u << d->index);
            continue;
        }
        int k = d->finger; if (k + 1 > nf) nf = k + 1;
        switch (d->what) {
        case HF_TIP: f[k].tip = v != 0; break;
        case HF_CONFIDENCE: f[k].has_conf = true; f[k].conf = v != 0; break;
        case HF_CONTACT: f[k].has_cid = true; f[k].cid = v; break;
        case HF_X: f[k].x = scaled(d, v, 32767); break;
        case HF_Y: f[k].y = scaled(d, v, 32767); break;
        case HF_PRESSURE: f[k].has_z = true; f[k].z = scaled(d, v, 225); break;
        case HF_WIDTH: f[k].has_size = true; f[k].w = scaled(d, v, 255); break;
        case HF_HEIGHT: f[k].has_size = true; f[k].h = scaled(d, v, 255); break;
        }
    }
    if (count > 0) { s->left = (uint8_t)MIN(count, 255); s->seen = 0; }           /* a frame starts */
    int live = count < 0 ? nf : MIN((int)s->left, nf);
    for (int k = 0; k < live; k++) {
        int32_t cid = f[k].has_cid ? f[k].cid : k;
        bool down = f[k].tip && (f[k].conf || !f[k].has_conf);   /* a palm (no confidence) counts as lifted */
        int slot = slot_of(s, cid, down);
        if (slot < 0) continue;
        s->seen |= (uint8_t)(1u << slot);
        if (down) {
            s->down |= (uint8_t)(1u << slot); s->cid[slot] = cid;
            s->tx[slot] = (uint16_t)f[k].x; s->ty[slot] = (uint16_t)f[k].y;
            int size = f[k].has_size ? (f[k].w && f[k].h ? (f[k].w + f[k].h) / 2 : MAX(f[k].w, f[k].h)) : 0;
            touch_event(o, slot, buttons, f[k].has_z ? 30 + f[k].z : 60, size, s->tx[slot], s->ty[slot]);
        } else {
            s->down &= (uint8_t)~(1u << slot);
            touch_event(o, slot, buttons, 0, 0, s->tx[slot], s->ty[slot]);
        }
    }
    if (count >= 0) {
        s->left = (uint8_t)(s->left > live ? s->left - live : 0);
        if (!s->left) {                                           /* the frame is complete: who wasn't in it has gone */
            for (int k = 0; k < HID_FINGERS; k++)
                if ((s->down >> k & 1) && !(s->seen >> k & 1)) { s->down &= (uint8_t)~(1u << k); touch_event(o, k, buttons, 0, 0, s->tx[k], s->ty[k]); }
            s->seen = 0;
        }
    }
    if (!o->nev) touch_event(o, 0, buttons, 0, 0, s->tx[0], s->ty[0]);   /* nothing down: still the button */
}

void hid_report(const struct hid_parse *p, struct hid_state *s, const uint8_t *r, int n, struct hid_out *o) {
    o->nkeys = 0; o->nev = 0;
    uint8_t id = 0;
    if (p->ids) { if (n < 1) return; id = r[0]; r++; n--; }
    if (p->touchpad)
        for (int i = 0; i < p->nf; i++)
            if (p->f[i].id == id && p->f[i].finger != NO_FINGER) { touch_report(p, s, id, r, n, o); return; }
    int32_t x = 0, y = 0, wheel = 0; uint8_t buttons = 0; bool point = false, cons = false, abs = false, touch = false, tip = false, has_tip = false;
    uint16_t now[4] = { 0 }; int nn = 0;
    for (int i = 0; i < p->nf; i++) {
        const struct hid_field *f = &p->f[i];
        if (f->id != id) continue;
        if (f->array) {
            cons = true;
            for (int c = 0; c < f->count; c++) {
                int32_t v = field(r, n, f->bit + (uint32_t)c * f->size, f->size, f->lmin < 0);
                if (v < f->lmin || v > f->lmax) continue;
                uint16_t u = (uint16_t)(f->usage + (v - f->lmin));
                if (u && nn < 4) now[nn++] = u;
            }
            continue;
        }
        int32_t v = field(r, n, f->bit, f->size, f->lmin < 0);
        switch (f->what) {
        case HF_TIP: has_tip = true; tip |= v != 0; break;
        case HF_X: case HF_Y: {
            point = true; abs |= !f->rel; touch |= f->touch;
            if (!f->rel && f->lmax > f->lmin) v = scaled(f, v, 32767);
            if (f->what == HF_X) x = v; else y = v;
            break; }
        case HF_WHEEL: wheel = v; break;
        case HF_BUTTON: point = true; if (v) buttons |= (uint8_t)(1u << f->index); break;
        case HF_CONSUMER: cons = true; if (v && nn < 4) now[nn++] = f->usage & 0xFFFF; break;
        }
    }
    (void)wheel;
    if (point && touch) {                                         /* a touchpad without Finger collections: one finger */
        o->nev = 1;
        o->ev[0] = (struct pointer_event){ 0, 0, 0, 0, 0, buttons, 1, (uint8_t)(has_tip && !tip ? 0 : 60), (uint16_t)x, (uint16_t)y };
    } else if (point && p->pointer) {
        o->nev = 1;
        o->ev[0] = abs ? (struct pointer_event){ 0, 0, (uint16_t)x, (uint16_t)y, 1, buttons }
                       : (struct pointer_event){ (int16_t)x, (int16_t)y, 0, 0, 0, buttons };
    }
    if (cons) {                                                   /* keys: released, then pressed */
        for (int i = 0; i < 4; i++) {
            bool still = false;
            for (int k = 0; k < nn; k++) still |= now[k] == s->consumer[i];
            if (s->consumer[i] && !still) add(o, consumer_key(s->consumer[i]), false);
        }
        for (int k = 0; k < nn; k++) {
            bool before = false;
            for (int i = 0; i < 4; i++) before |= now[k] == s->consumer[i];
            if (!before) add(o, consumer_key(now[k]), true);
        }
        memset(s->consumer, 0, sizeof s->consumer);
        memcpy(s->consumer, now, (size_t)nn * sizeof now[0]);
    }
}

/* ---- USB-MIDI: a packet is a code index number (what kind of message, so how many bytes) and three bytes ---- */
static const uint8_t cin_len[16] = { 0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1 };

int umidi_decode(const uint8_t *pk, int n, uint8_t *out, int cap) {
    int k = 0;
    for (int i = 0; i + 4 <= n; i += 4) {
        if (pk[i] >> 4) continue;                                 /* other cables: one port here */
        int len = cin_len[pk[i] & 15];
        for (int j = 0; j < len && k < cap; j++) out[k++] = pk[i + 1 + j];
    }
    return k;
}

static int msg_len(uint8_t st) {
    if (st < 0xF0) return (st & 0xE0) == 0xC0 ? 2 : 3;           /* program change and channel pressure are short */
    return st == 0xF1 || st == 0xF3 ? 2 : st == 0xF2 ? 3 : 1;
}
static bool packet(uint8_t pk[4], uint8_t cin, const uint8_t *m, int n) {
    pk[0] = cin; pk[1] = n > 0 ? m[0] : 0; pk[2] = n > 1 ? m[1] : 0; pk[3] = n > 2 ? m[2] : 0;
    return true;
}

bool umidi_encode(struct umidi_enc *e, uint8_t b, uint8_t pk[4]) {
    if (b >= 0xF8) return packet(pk, 0xF, &b, 1);                  /* real time: on its own, whenever */
    if (b == 0xF0) { e->sysex = true; e->msg[0] = b; e->n = 1; e->running = 0; return false; }
    if (e->sysex) {
        if (b == 0xF7) { e->msg[e->n++] = b; e->sysex = false; uint8_t c = (uint8_t)(4 + e->n); e->n = 0; return packet(pk, c, e->msg, c - 4); }
        if (!(b & 0x80)) {
            e->msg[e->n++] = b;
            if (e->n == 3) { e->n = 0; return packet(pk, 4, e->msg, 3); }
            return false;
        }
        e->sysex = false; e->n = 0;                               /* a status byte ends a sysex that lost its F7 */
    }
    if (b & 0x80) {
        e->msg[0] = b; e->n = 1; e->need = (uint8_t)msg_len(b);
        e->running = b < 0xF0 ? b : 0;
        if (e->need == 1) { e->n = 0; return packet(pk, 5, &b, 1); }   /* tune request */
        return false;
    }
    if (!e->n) {                                                  /* running status */
        if (!e->running) return false;
        e->msg[0] = e->running; e->n = 1; e->need = (uint8_t)msg_len(e->running);
    }
    e->msg[e->n++] = b;
    if (e->n < e->need) return false;
    uint8_t st = e->msg[0], c = st < 0xF0 ? st >> 4 : e->need == 2 ? 2 : 3;
    int len = e->n; e->n = 0;
    return packet(pk, c, e->msg, len);
}

/* ---- UVC ---- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32le(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* one streaming interface's alternate 0: its YUY2 and NV12 frames (other uncompressed formats, an infrared camera's
   greys, and MJPEG are skipped), and a bulk endpoint if it streams that way */
static int uvc_vs(const uint8_t *cfg, int len, int vs, struct uvc_info *u) {
    u->nframes = 0; u->bulk_ep = 0;
    int kind = 0, fmt = 0; bool in_vs = false;
    for (int o = 0; o + 2 <= len && cfg[o] >= 2 && o + cfg[o] <= len; o += cfg[o]) {
        const uint8_t *d = cfg + o;
        if (d[1] == 4 && d[0] >= 9) { in_vs = d[2] == vs && d[3] == 0 && d[5] == 0x0E && d[6] == 2; kind = 0; continue; }
        if (!in_vs) continue;
        if (d[1] == 5 && d[0] >= 7 && (d[3] & 3) == 2 && (d[2] & 0x80)) u->bulk_ep = d[2];            /* bulk streaming */
        if (d[1] != 0x24 || d[0] < 3) continue;
        if (d[2] == 0x04 && d[0] >= 21) {                                /* an uncompressed format: which one */
            kind = !memcmp(d + 5, "YUY2", 4) ? UVC_YUY2 : !memcmp(d + 5, "NV12", 4) ? UVC_NV12 : 0;
            fmt = d[3];
        } else if (d[2] == 0x06 || d[2] == 0x10) kind = 0;               /* MJPEG, frame-based (H.264): not ours */
        else if (d[2] == 0x05 && kind && d[0] >= 26 && u->nframes < UVC_FRAMES) {
            struct uvc_frame *f = &u->frames[u->nframes++];
            f->format = (uint8_t)fmt; f->index = d[3]; f->kind = (uint8_t)kind;
            f->w = rd16(d + 5); f->h = rd16(d + 7); f->interval = rd32le(d + 21);
        }
    }
    return u->nframes;
}

/* the camera whose VideoControl interface is vc_if: its header gives the UVC version and its streaming interfaces (a
   webcam can be two cameras, the picture and an infrared one for face login, each a VideoControl of its own); the
   first of them with frames we can read */
bool uvc_parse(const uint8_t *cfg, int len, uint8_t vc_if, struct uvc_info *u) {
    memset(u, 0, sizeof *u);
    u->vc_if = vc_if;
    uint8_t vs[8]; int nvs = 0, cur_if = -1, sub = 0;
    for (int o = 0; o + 2 <= len && cfg[o] >= 2 && o + cfg[o] <= len; o += cfg[o]) {
        const uint8_t *d = cfg + o;
        if (d[1] == 4 && d[0] >= 9) { cur_if = d[2]; sub = d[5] == 0x0E ? d[6] : 0; continue; }
        if (d[1] == 0x24 && sub == 1 && cur_if == vc_if && d[2] == 0x01 && d[0] >= 12 && !u->bcd) {   /* VC header */
            u->bcd = rd16(d + 3);
            for (int k = 0; k < d[11] && 12 + k < d[0] && nvs < 8; k++) vs[nvs++] = d[12 + k];
        }
    }
    for (int k = 0; k < nvs; k++) if (uvc_vs(cfg, len, vs[k], u)) { u->vs_if = vs[k]; return true; }
    return false;
}

int uvc_pick(const struct uvc_info *u, int want_w) {
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < u->nframes; i++) {
        const struct uvc_frame *f = &u->frames[i];
        if (f->w < 160) continue;
        int d = f->w >= want_w ? f->w - want_w : (want_w - f->w) * 2;   /* a little bigger beats a little smaller */
        if (f->kind == UVC_NV12) d += 8;                                 /* YUY2 first, all else equal */
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

uint32_t uvc_frame_bytes(const struct uvc_frame *f) { return f->kind == UVC_NV12 ? (uint32_t)f->w * f->h * 3 / 2 : (uint32_t)f->w * f->h * 2; }

int uvc_probe_fill(const struct uvc_info *u, const struct uvc_frame *f, uint8_t out[48]) {
    int n = u->bcd < 0x0110 ? 26 : u->bcd < 0x0150 ? 34 : 48;
    memset(out, 0, 48);
    wr16(out, 1);                                                        /* bmHint: keep the frame interval */
    out[2] = f->format; out[3] = f->index;
    wr32(out + 4, f->interval ? f->interval : 333333);
    wr32(out + 18, uvc_frame_bytes(f));
    return n;
}
uint32_t uvc_probe_payload(const uint8_t *p) { return rd32le(p + 22); }

void uvc_asm_init(struct uvc_asm *a, uint8_t *b0, uint8_t *b1, uint32_t cap, uint32_t want) {
    memset(a, 0, sizeof *a);
    a->buf[0] = b0; a->buf[1] = b1; a->cap = cap; a->want = MIN(want, cap); a->fid = -1;
}
static int asm_finish(struct uvc_asm *a) {
    bool ok = !a->err && a->len == a->want;
    if (ok) { a->ready = a->buf[a->fill]; a->fill ^= 1; a->frames++; }
    else if (a->len) a->dropped++;
    a->len = 0; a->err = false;
    return ok;
}
int uvc_payload(struct uvc_asm *a, const uint8_t *p, int n) {
    if (n < 2 || p[0] < 2 || p[0] > n) return 0;                        /* nothing, or no header */
    int hl = p[0], info = p[1], fid = info & 1, done = 0;
    if (a->fid >= 0 && fid != a->fid && a->len) done = asm_finish(a);    /* a new frame began without an EOF on the last */
    a->fid = fid;
    if (info & 0x40) a->err = true;                                      /* ERR: this frame is spoilt */
    uint32_t dn = (uint32_t)(n - hl);
    if (dn && a->len < a->cap) {
        uint32_t k = MIN(dn, a->cap - a->len);
        memcpy(a->buf[a->fill] + a->len, p + hl, k); a->len += k;
        if (k < dn) a->err = true;                                       /* more than a frame: not what was agreed */
    }
    if (info & 0x02) done |= asm_finish(a);                              /* EOF */
    return done;
}

void uvc_luma(const struct uvc_frame *f, const uint8_t *pic, uint8_t *luma) {
    uint32_t n = (uint32_t)f->w * f->h;
    if (f->kind == UVC_NV12) memcpy(luma, pic, n);                       /* NV12: the brightness plane comes first */
    else for (uint32_t i = 0; i < n; i++) luma[i] = pic[2 * i];          /* YUY2: every other byte */
}

