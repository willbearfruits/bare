/* Touchpads and touchscreens on I2C, "HID over I2C": found through ACPI (a device whose _HID or _CID says PNP0C50),
   its HID descriptor read from one of the registers devices commonly use, powered on and reset, its report
   descriptor parsed by the same code as USB's (core/usbclass.c). A precision touchpad is switched out of its mouse
   mode; its first finger then feeds the strum plate and the pen like a Synaptics pad does. There is no interrupt line
   here: the main loop asks for a report every few milliseconds. Every step logs, for a machine only a photo of the
   log view can tell about. */
#include "i2c.h"
#include "acpi.h"
#include "usbclass.h"
#include "ps2.h"
#include "pit.h"
#include "libc.h"
#include "log.h"

#define DEVS 2
#define MAX_READ 128                              /* a report and its length; more is left unread */
struct ihid {
    int bus; uint16_t addr;
    uint16_t rdesc_len, rdesc_reg, in_reg, max_in, cmd_reg, data_reg, vid, pid;
    struct hid_parse p; struct hid_state st;
    int shown, errors;
    char name[12];
};
static struct ihid devs[DEVS];
static int ndev;
static uint8_t buf[MAX_READ];

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static bool command(struct ihid *d, uint8_t op) {
    uint8_t w[4] = { (uint8_t)d->cmd_reg, (uint8_t)(d->cmd_reg >> 8), 0x00, op };
    return i2c_xfer(d->bus, d->addr, w, 4, 0, 0) == 0;
}
/* SET_REPORT of a feature: the command, then the data register, a length and the report (its ID first) */
static bool set_feature(struct ihid *d, const uint8_t *rep, int len) {
    uint8_t w[64]; int k = 0; uint8_t id = rep[0];
    if (len > 48) return false;
    w[k++] = (uint8_t)d->cmd_reg; w[k++] = (uint8_t)(d->cmd_reg >> 8);
    w[k++] = (uint8_t)(0x30 | (id < 15 ? id : 15)); w[k++] = 0x03;
    if (id >= 15) w[k++] = id;
    w[k++] = (uint8_t)d->data_reg; w[k++] = (uint8_t)(d->data_reg >> 8);
    int payload = id ? len : len - 1;
    w[k++] = (uint8_t)(payload + 2); w[k++] = 0;
    memcpy(w + k, id ? rep : rep + 1, (size_t)payload); k += payload;
    return i2c_xfer(d->bus, d->addr, w, k, 0, 0) == 0;
}

static bool try_desc(int bus, uint16_t addr, uint16_t reg, struct ihid *d) {
    uint8_t w[2] = { (uint8_t)reg, (uint8_t)(reg >> 8) }, h[30];
    int got = i2c_xfer(bus, addr, w, 2, h, 30);
    if (got != 30) { logf("i2c-hid: %s %02x, register %04x: %s", i2c_name(bus), addr, reg, got == -1 ? "no answer" : got == -2 ? "timed out" : "failed"); return false; }
    if (le16(h) != 30 || le16(h + 2) != 0x0100) {
        logf("i2c-hid: %s %02x, register %04x: not a HID descriptor (%02x %02x %02x %02x)", i2c_name(bus), addr, reg, h[0], h[1], h[2], h[3]);
        return false;
    }
    memset(d, 0, sizeof *d);
    d->bus = bus; d->addr = addr;
    d->rdesc_len = le16(h + 4); d->rdesc_reg = le16(h + 6); d->in_reg = le16(h + 8); d->max_in = le16(h + 10);
    d->cmd_reg = le16(h + 16); d->data_reg = le16(h + 18); d->vid = le16(h + 20); d->pid = le16(h + 22);
    logf("i2c-hid: %s %02x: %04x:%04x, report descriptor %u bytes at %04x, input %u bytes, command %04x, data %04x",
         i2c_name(bus), addr, d->vid, d->pid, d->rdesc_len, d->rdesc_reg, d->max_in, d->cmd_reg, d->data_reg);
    return true;
}

static bool start(struct ihid *d) {
    if (!command(d, 0x08)) logf("i2c-hid: power on: no answer");            /* SET_POWER on */
    sleep_ms(60);
    if (!command(d, 0x01)) logf("i2c-hid: reset: no answer");                /* RESET */
    sleep_ms(100);
    i2c_xfer(d->bus, d->addr, 0, 0, buf, MIN(d->max_in, (uint16_t)MAX_READ));   /* the reset's empty report */
    static uint8_t rd[4096];
    uint8_t w[2] = { (uint8_t)d->rdesc_reg, (uint8_t)(d->rdesc_reg >> 8) };
    int n = i2c_xfer(d->bus, d->addr, w, 2, rd, MIN(d->rdesc_len, (uint16_t)sizeof rd));
    if (n <= 0) { logf("i2c-hid: the report descriptor would not come (%d)", n); return false; }
    bool any = hid_parse(&d->p, rd, n);
    logf("i2c-hid: %d fields: %s%s%s, input mode %s (report %u, %u bytes)", d->p.nf, d->p.touchpad ? "a touchpad" : d->p.pointer ? "a pointer" : "",
         d->p.absolute ? " (absolute)" : "", d->p.consumer ? " and keys" : "", d->p.has_mode ? "found" : "not found", d->p.mode_id, d->p.mode_len);
    if (!any) return false;
    if (d->p.touchpad && d->p.has_mode) {
        uint8_t rep[40]; int len = hid_mode_report(&d->p, 3, rep, sizeof rep);
        logf("i2c-hid: precision mode %s", len && set_feature(d, rep, len) ? "on" : "could not be set");
    }
    return true;
}

static void add(int bus, uint16_t addr, const char *what) {
    static const uint16_t regs[] = { 0x0001, 0x0020, 0x0000, 0x0002 };
    for (unsigned k = 0; k < ARRAY_LEN(regs) && ndev < DEVS; k++) {
        struct ihid *d = &devs[ndev];
        if (!try_desc(bus, addr, regs[k], d)) continue;
        snfmt(d->name, sizeof d->name, "%s", what);
        if (start(d)) { ndev++; logf("i2c-hid: %s at %02x is ours", what, addr); }
        return;
    }
}

void i2chid_init(void) {
    struct acpi_i2c a[32];
    int na = acpi_i2c_devices(a, 32), blank = acpi_blank;
    for (int i = 0; i < na; i++) {
        logf("acpi: I2C %s (%s%s) at %02x, %u kHz, on %s: %s", a[i].dev, a[i].hid[0] ? a[i].hid : "no _HID", a[i].hid_i2c ? ", HID" : "",
             a[i].addr, a[i].speed / 1000, a[i].bus, a[i].bus_pci ? "PCI" : a[i].bus_mmio ? "fixed address" : "not found");
    }
    if (blank) logf("acpi: %d more I2C descriptors without an address (the firmware fills those in when an OS runs it)", blank);
    i2c_init();
    for (int i = 0; i < na; i++) if (a[i].hid_i2c && !a[i].bus_pci && a[i].bus_mmio) i2c_add_mmio(a[i].bus_mmio);
    if (!i2c_buses()) { logf("i2c: no controller"); return; }
    static uint8_t answers[I2C_BUSES][16];
    for (int b = 0; b < i2c_buses(); b++) i2c_scan(b, answers[b]);
    for (int i = 0; i < na && ndev < DEVS; i++) {                     /* what ACPI names, where it names it */
        if (!a[i].hid_i2c || !a[i].addr) continue;
        int bus = i2c_find(a[i].bus_pci, a[i].bus_dev, a[i].bus_fn, a[i].bus_mmio);
        for (int b = 0; b < i2c_buses() && ndev < DEVS; b++)
            if ((bus < 0 || b == bus) && (answers[b][a[i].addr >> 3] >> (a[i].addr & 7) & 1)) add(b, a[i].addr, a[i].hid[0] ? a[i].hid : a[i].dev);
    }
    /* otherwise the usual addresses of touchpads (ELAN, Synaptics, FocalTech, …) and touchscreens, where something answers */
    static const uint8_t usual[] = { 0x15, 0x2C, 0x38, 0x10, 0x20, 0x2A, 0x4B, 0x14, 0x5D, 0x09, 0x0A };
    for (int b = 0; b < i2c_buses() && ndev < DEVS; b++)
        for (unsigned k = 0; k < sizeof usual && ndev < DEVS; k++)
            if (answers[b][usual[k] >> 3] >> (usual[k] & 7) & 1) {
                bool have = false;
                for (int j = 0; j < ndev; j++) have |= devs[j].bus == b && devs[j].addr == usual[k];
                if (!have) add(b, usual[k], "HID device?");
            }
    if (!ndev) logf("i2c-hid: no touchpad or touchscreen found");
}

void i2chid_service(void) {
    static uint64_t last;
    uint64_t now = pit_ticks();
    if (!ndev || now - last < 8) return;
    last = now;
    for (int i = 0; i < ndev; i++) {
        struct ihid *d = &devs[i];
        if (d->errors > 50) continue;
        int want = MIN(d->max_in, (uint16_t)MAX_READ);
        int got = i2c_xfer(d->bus, d->addr, 0, 0, buf, want);
        if (got != want) { if (++d->errors == 50) logf("i2c-hid: %s stopped answering", d->name); continue; }
        d->errors = 0;
        int len = le16(buf);
        if (len <= 2 || len > want) continue;                         /* nothing new */
        if (d->shown < 6) {
            char hex[64]; int k = 0;
            for (int j = 0; j < MIN(len, 16) && k < 60; j++) k += snfmt(hex + k, sizeof hex - (size_t)k, "%02x ", buf[j]);
            logf("i2c-hid: report: %s", hex);
            d->shown++;
        }
        struct hid_out o;
        hid_report(&d->p, &d->st, buf + 2, len - 2, &o);
        for (int k = 0; k < o.nkeys; k++) ps2_inject_key(o.keys[k].code, o.keys[k].down);
        for (int e = 0; e < o.nev; e++) ps2_inject_pointer(&o.ev[e]);
    }
}
