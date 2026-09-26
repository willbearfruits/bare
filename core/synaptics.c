#include "synaptics.h"
#include "libc.h"

void syn_start(struct syn_state *s, bool agm) {
    memset(s, 0, sizeof *s);
    s->agm = agm;
    s->x0 = 1472; s->x1 = 5472; s->y0 = 1408; s->y1 = 4448;          /* the usual ranges */
}

static uint16_t norm(int v, int lo, int hi) { return (uint16_t)((CLAMP(v, lo, hi) - lo) * 32767 / (hi - lo)); }
static int apart(const struct syn_state *s, int x, int y, int k) { int dx = x - s->slot[k].x, dy = y - s->slot[k].y; return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy); }

/* n fingers now (0..2) onto the two slots: which finger is which isn't told, so each keeps the slot it had, by nearness */
static int fingers(struct syn_state *s, int n, const uint16_t *tx, const uint16_t *ty, const uint8_t *z, uint8_t size, uint8_t buttons,
                   struct pointer_event *out) {
    int to[2] = { 0, 1 }, ne = 0;
    if (n == 1 && s->slot[1].on && (!s->slot[0].on || apart(s, tx[0], ty[0], 1) < apart(s, tx[0], ty[0], 0))) to[0] = 1;
    if (n == 2) {
        bool swap = s->slot[0].on && s->slot[1].on ? apart(s, tx[0], ty[0], 1) + apart(s, tx[1], ty[1], 0) < apart(s, tx[0], ty[0], 0) + apart(s, tx[1], ty[1], 1)
                  : s->slot[0].on ? apart(s, tx[1], ty[1], 0) < apart(s, tx[0], ty[0], 0)
                  : s->slot[1].on ? apart(s, tx[0], ty[0], 1) < apart(s, tx[1], ty[1], 1) : false;
        if (swap) { to[0] = 1; to[1] = 0; }
    }
    bool used[2] = { false, false };
    for (int i = 0; i < n; i++) {
        int k = to[i]; used[k] = true; s->slot[k].on = true; s->slot[k].x = tx[i]; s->slot[k].y = ty[i];
        out[ne++] = (struct pointer_event){ .buttons = buttons, .touch = 1, .z = z[i], .tx = tx[i], .ty = ty[i], .finger = (uint8_t)k, .size = i ? 0 : size };
    }
    for (int k = 0; k < 2; k++)
        if (!used[k] && s->slot[k].on) {
            s->slot[k].on = false;
            out[ne++] = (struct pointer_event){ .buttons = buttons, .touch = 1, .tx = (uint16_t)s->slot[k].x, .ty = (uint16_t)s->slot[k].y, .finger = (uint8_t)k };
        }
    if (!ne) out[ne++] = (struct pointer_event){ .buttons = buttons, .touch = 1 };   /* no finger: the buttons still count */
    return ne;
}

/* W mode: 10ww0wRL YYYYXXXX Z 11yx0wRL X Y. w: finger width 4-15; 0 two fingers, 1 three or more; 2 (advanced gesture
   mode) a packet about the second finger; 3 a pass-through packet */
int syn_packet(struct syn_state *s, const uint8_t p[6], struct pointer_event out[3], uint8_t rel[3]) {
    int w = ((p[0] & 0x30) >> 2) | ((p[0] & 0x04) >> 1) | ((p[3] & 0x04) >> 2);
    if (w == 3) { rel[0] = p[1]; rel[1] = p[4]; rel[2] = p[5]; return -1; }
    if (s->agm && w == 2) {                                           /* the second finger's position comes first */
        if (((p[5] & 0x30) >> 4) == 1) {
            s->agm_x = (((p[4] & 0x0F) << 8) | p[1]) << 1; s->agm_y = (((p[4] & 0xF0) << 4) | p[2]) << 1;
            s->agm_z = ((p[3] & 0x30) | (p[5] & 0x0F)) << 1;
        }
        return 0;
    }
    int x = ((p[3] & 0x10) << 8) | ((p[1] & 0x0F) << 8) | p[4];
    int y = ((p[3] & 0x20) << 7) | ((p[1] & 0xF0) << 4) | p[5];
    int z = p[2];
    uint8_t buttons = (uint8_t)((((p[0] ^ p[3]) | p[0]) & 1) | (p[0] & 2));   /* a clickpad's click is the L bits xor'ed */
    if (z > 0 && x > 0 && y > 0) {
        if (x < s->x0) s->x0 = x; if (x > s->x1) s->x1 = x;
        if (y < s->y0) s->y0 = y; if (y > s->y1) s->y1 = y;
    }
    int n = !z ? 0 : w >= 4 || !s->agm ? 1 : 2;                       /* three and more fingers: we follow two */
    if (n == 2 && !s->agm_x && !s->agm_y) n = 1;                        /* the second finger's packet hasn't come yet */
    uint16_t tx[2] = { norm(x, s->x0, s->x1), norm(s->agm_x, s->x0, s->x1) };
    uint16_t ty[2] = { (uint16_t)(32767 - norm(y, s->y0, s->y1)), (uint16_t)(32767 - norm(s->agm_y, s->y0, s->y1)) };   /* its y grows upwards */
    uint8_t zz[2] = { (uint8_t)z, (uint8_t)CLAMP(s->agm_z, 30, 255) };
    int ne = fingers(s, n, tx, ty, zz, (uint8_t)(w >= 4 ? (w - 4) * 255 / 11 : 0), buttons, out);
    if (n < 2) s->agm_x = s->agm_y = 0;
    return ne;
}
