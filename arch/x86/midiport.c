/* MIDI ports: the serial ports, run at 38400 baud (the "PC mode" of Roland and Yamaha modules and keyboards; a 16550
   can't make MIDI's own 31250), an MPU-401 in UART mode at 0x330 (sound cards and their game ports), and USB MIDI
   devices when our own USB stack runs (drivers/usbmidi.c), listed after them. Bytes coming in on the old ports are
   collected from the 1 kHz tick, so the 16-byte FIFOs never overflow. */
#include "platform.h"
#include "io.h"
#include "serial.h"
#include "libc.h"
#include "log.h"
#include "drivers/usb.h"

enum { P_COM1, P_COM2, P_MPU, NPORTS };
static const char *const port_names[NPORTS] = { "SERIAL 1", "SERIAL 2", "MPU-401" };
static const uint16_t com_base[2] = { 0x3F8, 0x2F8 };
#define MPU_DATA 0x330
#define MPU_STAT 0x331
static bool present[NPORTS], probed;
static int open_port = -1;                   /* NPORTS: a USB one */
static uint8_t rx[512]; static volatile uint32_t rx_w, rx_r;

static bool com_probe(uint16_t b) { outb(b + 7, 0xA5); if (inb(b + 7) != 0xA5) return false; outb(b + 7, 0x5A); return inb(b + 7) == 0x5A; }
/* the MPU answers a reset with 0xFE; nothing there reads 0xFF */
static bool mpu_cmd(uint8_t c) {
    for (int i = 0; i < 20000 && (inb(MPU_STAT) & 0x40); i++) ;
    outb(MPU_STAT, c);
    for (int i = 0; i < 100000; i++) if (!(inb(MPU_STAT) & 0x80) && inb(MPU_DATA) == 0xFE) return true;
    return false;
}
static void probe(void) {
    if (probed) return;
    probed = true;
    for (int i = 0; i < 2; i++) present[i] = com_probe(com_base[i]);
    present[P_MPU] = inb(MPU_STAT) != 0xFF && mpu_cmd(0xFF);
    logf("midi: serial 1 %s, serial 2 %s, MPU-401 %s", present[0] ? "yes" : "no", present[1] ? "yes" : "no", present[2] ? "yes" : "no");
}

int plat_midi_ports(const char **names, int max) {
    probe();
    int n = 0;
    for (int i = 0; i < NPORTS && n < max; i++) if (present[i]) names[n++] = port_names[i];
    return n + usb_midi_ports(names + n, max - n);
}
static int nth_present(int k) { for (int i = 0; i < NPORTS; i++) if (present[i] && k-- == 0) return i; return -1; }

bool plat_midi_open(int k) {
    probe();
    if (open_port == P_COM1) serial_log(true);
    if (open_port == NPORTS) usb_midi_open(-1);
    open_port = -1;
    if (k < 0) return true;
    int p = nth_present(k);
    if (p < 0) {
        int old = 0; for (int i = 0; i < NPORTS; i++) old += present[i];
        if (!usb_midi_open(k - old)) return false;
        open_port = NPORTS;
        return true;
    }
    if (p == P_MPU) { if (!mpu_cmd(0x3F)) return false; }         /* UART mode */
    else {
        uint16_t b = com_base[p];
        if (p == P_COM1) serial_log(false);                        /* the log would talk MIDI nonsense */
        outb(b + 1, 0x00); outb(b + 3, 0x80); outb(b + 0, 3); outb(b + 1, 0x00);   /* 115200 / 3 = 38400 */
        outb(b + 3, 0x03); outb(b + 2, 0xC7); outb(b + 4, 0x03);                    /* 8N1, FIFOs on */
    }
    rx_w = rx_r = 0;
    open_port = p;
    logf("midi: %s open", port_names[p]);
    return true;
}

/* from the 1 kHz tick */
void midi_hw_poll(void) {
    if (open_port < 0 || open_port == NPORTS) return;
    for (int i = 0; i < 64; i++) {
        uint8_t b;
        if (open_port == P_MPU) { if (inb(MPU_STAT) & 0x80) break; b = inb(MPU_DATA); }
        else { uint16_t base = com_base[open_port]; if (!(inb(base + 5) & 1)) break; b = inb(base); }
        if (rx_w - rx_r < sizeof rx) rx[rx_w++ % sizeof rx] = b;
    }
}

int plat_midi_read(uint8_t *buf, int max) {
    if (open_port == NPORTS) return usb_midi_read(buf, max);
    uint32_t st = plat_irq_save();
    int n = 0;
    while (n < max && rx_r != rx_w) buf[n++] = rx[rx_r++ % sizeof rx];
    plat_irq_restore(st);
    return n;
}

int plat_midi_write(const uint8_t *buf, int n) {
    if (open_port < 0) return n;                                   /* nowhere to go: dropped */
    if (open_port == NPORTS) return usb_midi_write(buf, n);
    int sent = 0;
    if (open_port == P_MPU) {
        while (sent < n && !(inb(MPU_STAT) & 0x40)) outb(MPU_DATA, buf[sent++]);
    } else {
        uint16_t b = com_base[open_port];
        if (inb(b + 5) & 0x20) while (sent < n && sent < 16) outb(b, buf[sent++]);   /* the FIFO is empty: 16 fit */
    }
    return sent;
}
