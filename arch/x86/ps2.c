#include "ps2.h"
#include "synaptics.h"
#include "io.h"
#include "cpu.h"
#include "keys.h"
#include "log.h"
#include "vmmouse.h"

#define QLEN 64
static struct key_event queue[QLEN];
static volatile unsigned qhead, qtail;
static bool e0;
static uint8_t down_state[256];

static const uint8_t set1[128] = {
    [0x01] = KEY_ESC, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6', [0x08] = '7',
    [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=', [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB,
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']', [0x1C] = KEY_ENTER, [0x1D] = KEY_LCTRL,
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2A] = KEY_LSHIFT, [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x36] = KEY_RSHIFT, [0x37] = '*', [0x38] = KEY_LALT, [0x39] = KEY_SPACE, [0x3A] = KEY_CAPS,
    [0x3B] = KEY_F1, [0x3C] = KEY_F2, [0x3D] = KEY_F3, [0x3E] = KEY_F4, [0x3F] = KEY_F5, [0x40] = KEY_F6,
    [0x41] = KEY_F7, [0x42] = KEY_F8, [0x43] = KEY_F9, [0x44] = KEY_F10, [0x57] = KEY_F11, [0x58] = KEY_F12,
    [0x48] = KEY_UP, [0x50] = KEY_DOWN, [0x4B] = KEY_LEFT, [0x4D] = KEY_RIGHT,   /* keypad without E0 too */
    [0x47] = KEY_HOME, [0x4F] = KEY_END, [0x49] = KEY_PGUP, [0x51] = KEY_PGDN, [0x52] = KEY_INSERT, [0x53] = KEY_DELETE,
    [0x46] = KEY_SCROLL,
};

static void push(uint8_t code, bool down) {
    if (!code) return;
    if (down && down_state[code]) return;          /* typematic repeat */
    down_state[code] = down;
    unsigned next = (qhead + 1) % QLEN;
    if (next == qtail) return;
    queue[qhead] = (struct key_event){ code, down };
    qhead = next;
}

/* ---- mouse (aux port): a plain PS/2 mouse, or a Synaptics touchpad in absolute mode ---- */
#define PLEN 32
static struct pointer_event pqueue[PLEN];
static volatile unsigned phead, ptail;
static uint8_t mpkt[6]; static int mpos;
static bool mouse_ok, synaptics;

static bool wait_write(void) { for (int i = 0; i < 100000; i++) if (!(inb(0x64) & 2)) return true; return false; }
static bool wait_read(void)  { for (int i = 0; i < 100000; i++) if (inb(0x64) & 1) return true; return false; }
static uint8_t mouse_cmd(uint8_t c) {
    wait_write(); outb(0x64, 0xD4); wait_write(); outb(0x60, c);
    return wait_read() ? inb(0x60) : 0;
}
static void pq_push(struct pointer_event e) {
    unsigned next = (phead + 1) % PLEN;
    if (next == ptail) return;
    pqueue[phead] = e; phead = next;
}

/* a standard 3-byte packet: a mouse, or the TrackPoint behind a touchpad's pass-through port */
static void rel_packet(uint8_t b0, uint8_t b1, uint8_t b2) {
    if (b0 & 0xC0) return;                                               /* overflow */
    int dx = b1 - ((b0 << 4) & 0x100), dy = b2 - ((b0 << 3) & 0x100);
    pq_push((struct pointer_event){ (int16_t)dx, (int16_t)-dy, 0, 0, 0, (uint8_t)(b0 & 7) });
}

static struct syn_state syn;
static void syn_bytes(const uint8_t *p) {
    struct pointer_event ev[3]; uint8_t rel[3];
    int n = syn_packet(&syn, p, ev, rel);
    if (n < 0) rel_packet(rel[0], rel[1], rel[2]);
    for (int i = 0; i < n; i++) pq_push(ev[i]);
}

static void mouse_byte(uint8_t b) {
    if (vmmouse_active()) return;                      /* the absolute device delivers data through its own port */
    if (synaptics) {
        if (mpos == 0 && (b & 0xC8) != 0x80) return;               /* resync: the first byte is 10xx0xxx */
        if (mpos == 3 && (b & 0xC8) != 0xC0) { mpos = 0; return; }  /* the fourth 11xx0xxx */
        mpkt[mpos++] = b;
        if (mpos == 6) { mpos = 0; syn_bytes(mpkt); }
        return;
    }
    if (mpos == 0 && !(b & 0x08)) return;              /* resync on the always-set bit */
    mpkt[mpos++] = b;
    if (mpos < 3) return;
    mpos = 0;
    rel_packet(mpkt[0], mpkt[1], mpkt[2]);
}
static void kbd_byte(uint8_t sc);
/* A byte is taken only when the controller says one is waiting, so the IRQ handlers and ps2_poll can both run. */
static void take(void) {
    uint8_t st = inb(0x64);
    if (!(st & 1)) return;
    uint8_t b = inb(0x60);
    if (st & 0x20) mouse_byte(b); else kbd_byte(b);
}
static void mouse_irq(void) { take(); }
static void kbd_irq(void) { take(); }

/* The main loop empties the controller too: on a machine whose interrupts don't arrive, keys still work. */
static void ps2_poll(void) {
    uint32_t f = read_flags(); cli();
    for (int i = 0; i < 16 && (inb(0x64) & 1); i++) take();
    if (f & 0x200) sti();
}

bool ps2_pointer_poll(struct pointer_event *ev) {
    ps2_poll();
    if (vmmouse_poll(ev)) return true;
    if (ptail == phead) return false;
    *ev = pqueue[ptail]; ptail = (ptail + 1) % PLEN;
    return true;
}
static bool mouse_send(uint8_t c) { return mouse_cmd(c) == 0xFA; }
static bool mouse_arg(uint8_t c, uint8_t a) { return mouse_send(c) && mouse_send(a); }
/* A Synaptics "special command": the argument as four 2-bit Set Resolution commands after Set Scaling 1:1 */
static bool syn_special(uint8_t arg) {
    if (!mouse_send(0xE6)) return false;
    for (int i = 6; i >= 0; i -= 2) if (!mouse_arg(0xE8, (uint8_t)((arg >> i) & 3))) return false;
    return true;
}
static bool syn_query(uint8_t q, uint8_t out[3]) {
    if (!syn_special(q) || !mouse_send(0xE9)) return false;          /* status request: three bytes after the ack */
    for (int i = 0; i < 3; i++) out[i] = wait_read() ? inb(0x60) : 0;
    return true;
}
/* A Synaptics touchpad goes into absolute mode with finger widths (W mode), so the core gets where the finger is — the
   strum plate and the pen. A TrackPoint behind it (ThinkPads) keeps working through the pass-through port. Anything
   else stays a relative mouse. */
static void synaptics_init(void) {
    uint8_t id[3], cap[3], c0c[3] = { 0 };
    if (!syn_query(0x00, id) || id[1] != 0x47) { mouse_send(0xF6); return; }    /* not one: undo the probe's settings */
    bool ext = syn_query(0x02, cap) && (cap[0] & 0x80);
    bool pass = ext && (cap[2] & 0x80);
    uint32_t ex0c = 0;                                                   /* extended capabilities (query 0x0C), if it has them */
    if (ext && ((cap[0] >> 4) & 7) >= 4 && syn_query(0x0C, c0c)) ex0c = (uint32_t)c0c[0] << 16 | (uint32_t)c0c[1] << 8 | c0c[2];
    if (!syn_special(ext ? 0xC1 : 0xC0) || !mouse_arg(0xF3, 0x14)) { logf("ps2: Synaptics %d.%d: no absolute mode", id[2] & 15, id[0]); mouse_send(0xF6); return; }
    /* advanced gesture mode (the pad has it, or an image sensor): the second finger too — as Linux switches it on */
    bool agm = (ex0c & 0x080800) && syn_special(0x03) && mouse_arg(0xF3, 0xC8);
    syn_start(&syn, agm);
    synaptics = true; mpos = 0;
    if (pass && syn_special(0xF4)) mouse_arg(0xF3, 0x28);             /* enable the device on the pass-through port */
    logf("ps2: Synaptics touchpad %d.%d in absolute mode%s%s (capabilities %02x%02x%02x, %06x)", id[2] & 15, id[0],
         pass ? ", with a pass-through port" : "", agm ? ", two fingers" : "", cap[0], cap[1], cap[2], ex0c);
}

static void mouse_init(void) {
    wait_write(); outb(0x64, 0xA8);                      /* enable aux port */
    wait_write(); outb(0x64, 0x20);                      /* read controller config */
    if (!wait_read()) return;
    uint8_t cfg = inb(0x60);
    cfg |= 0x02; cfg &= ~0x20;                           /* IRQ12 on, mouse clock on */
    wait_write(); outb(0x64, 0x60); wait_write(); outb(0x60, cfg);
    if (mouse_cmd(0xF6) != 0xFA) { logf("ps2: no mouse"); return; }
    synaptics_init();
    mouse_cmd(0xF4);
    mouse_ok = true;
    irq_install(12, mouse_irq);
    logf("ps2: mouse enabled");
    vmmouse_init();
}

static void kbd_byte(uint8_t sc) {
    if (sc == 0xE0) { e0 = true; return; }
    bool down = !(sc & 0x80);
    uint8_t code = set1[sc & 0x7F];
    if (e0) {
        e0 = false;
        switch (sc & 0x7F) {
        case 0x1D: code = KEY_RCTRL; break;
        case 0x38: code = KEY_RALT; break;
        case 0x5B: code = KEY_LMETA; break;
        case 0x37: code = KEY_PRTSC; break;          /* E0 2A E0 37: the fake shift was dropped above */
        case 0x2A: case 0x36: return;              /* fake shifts */
        case 0x20: code = KEY_MUTE; break;         /* multimedia keys: laptops send their volume keys as these */
        case 0x2E: code = KEY_VOLDOWN; break;
        case 0x30: code = KEY_VOLUP; break;
        }
    }
    push(code, down);
}

void ps2_init(void) {
    for (int i = 0; i < 32 && (inb(0x64) & 1); i++) inb(0x60);   /* drain */
    cli();
    mouse_init();                                                 /* before IRQ1 is live: replies come via the output buffer */
    for (int i = 0; i < 32 && (inb(0x64) & 1); i++) inb(0x60);
    irq_install(1, kbd_irq);
    sti();
}

bool ps2_has_touchpad(void) { return synaptics; }
void ps2_inject_key(uint8_t code, bool down) { uint32_t f = read_flags(); cli(); push(code, down); if (f & 0x200) sti(); }
void ps2_inject_pointer(const struct pointer_event *e) { uint32_t f = read_flags(); cli(); pq_push(*e); if (f & 0x200) sti(); }

bool ps2_key_poll(struct key_event *ev) {
    ps2_poll();
    if (qtail == qhead) return false;
    *ev = queue[qtail]; qtail = (qtail + 1) % QLEN;
    return true;
}
