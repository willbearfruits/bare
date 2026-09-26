#include "sensors.h"
#include "io.h"
#include "mem.h"
#include "libc.h"
#include "log.h"

static bool has_dts, thinkpad, ec_ok;
static int tjmax = 100;
static char machine[64];

static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}
static inline uint64_t rdmsr(uint32_t msr) { uint32_t lo, hi; __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr)); return ((uint64_t)hi << 32) | lo; }

/* ---- SMBIOS: find manufacturer/product so EC writes only happen on ThinkPads ---- */
static const char *smbios_string(const uint8_t *s, int idx) {
    /* s points at the start of the string area after a structure */
    for (int i = 1; i < idx; i++) { while (*s) s++; s++; if (!*s) return ""; }
    return (const char *)s;
}
static void dmi_scan(void) {
    const uint8_t *base = mmio_map(0xF0000, 0x10000);
    for (uint32_t off = 0; off < 0x10000; off += 16) {
        const uint8_t *p = base + off;
        uint32_t table = 0, len = 0; int eplen = 0;
        if (memcmp(p, "_SM_", 4) == 0 && p[5] == 0x1F) { eplen = 0x1F; table = *(const uint32_t *)(p + 0x18); len = *(const uint16_t *)(p + 0x16); }
        else if (memcmp(p, "_SM3_", 5) == 0 && p[6] == 0x18) { eplen = 0x18; table = (uint32_t)*(const uint64_t *)(p + 0x10); len = *(const uint32_t *)(p + 0x0C); }
        else continue;
        uint8_t sum = 0; for (int i = 0; i < eplen; i++) sum += p[i];
        if (sum != 0 || !table || !len || len > 0x10000) continue;
        const uint8_t *t = mmio_map(table, len), *end = t + len;
        while (t + 4 <= end) {
            uint8_t type = t[0], tlen = t[1];
            if (tlen < 4 || type == 127) break;
            const uint8_t *strs = t + tlen;
            if (type == 1 && tlen >= 8) {
                const char *man = smbios_string(strs, t[4]), *prod = smbios_string(strs, t[5]), *ver = smbios_string(strs, t[6]);
                snfmt(machine, sizeof machine, "%s %s %s", man, prod, ver);
                for (const char *q = man; *q; q++) if ((q[0] == 'L' && q[1] == 'E' && q[2] == 'N') || (q[0] == 'I' && q[1] == 'B' && q[2] == 'M')) thinkpad = true;
                return;
            }
            const uint8_t *q = strs;
            while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) q++;
            t = q + 2;
        }
        return;
    }
}

/* ---- ACPI embedded controller at 0x62/0x66 (ThinkPad register map from thinkpad_acpi / thinkfan) ---- */
static bool ec_wait_ibf(void) { for (int i = 0; i < 100000; i++) if (!(inb(0x66) & 2)) return true; return false; }
static bool ec_wait_obf(void) { for (int i = 0; i < 100000; i++) if (inb(0x66) & 1) return true; return false; }
static int ec_read(uint8_t addr) {
    if (!ec_wait_ibf()) return -1; outb(0x66, 0x80);
    if (!ec_wait_ibf()) return -1; outb(0x62, addr);
    if (!ec_wait_obf()) return -1; return inb(0x62);
}
static void ec_write(uint8_t addr, uint8_t v) {
    if (!ec_wait_ibf()) return; outb(0x66, 0x81);
    if (!ec_wait_ibf()) return; outb(0x62, addr);
    if (!ec_wait_ibf()) return; outb(0x62, v);
    ec_wait_ibf();
}

void sensors_init(void) {
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    if (a >= 6) { cpuid(6, &a, &b, &c, &d); has_dts = a & 1; }
    if (has_dts) { int t = (int)((rdmsr(0x1A2) >> 16) & 0xFF); if (t > 50 && t < 130) tjmax = t; }
    dmi_scan();
    if (thinkpad) { int v = ec_read(0x78); ec_ok = v >= 0 && v != 0xFF; }
    logf("sensors: machine '%s'%s, dts %s (tjmax %d), ec %s", machine, thinkpad ? " [ThinkPad]" : "", has_dts ? "yes" : "no", tjmax, ec_ok ? "ok" : "no");
}
bool sensors_is_thinkpad(void) { return thinkpad; }
const char *sensors_machine(void) { return machine; }

int sensors_cpu_temp(void) {
    if (has_dts) {
        uint64_t st = rdmsr(0x19C);
        if (st & (1u << 31)) return tjmax - (int)((st >> 16) & 0x7F);
    }
    if (ec_ok) { int v = ec_read(0x78); if (v > 0 && v < 128) return v; }
    return -1;
}
int sensors_fan_rpm(void) {
    if (!ec_ok) return -1;
    int lo = ec_read(0x84), hi = ec_read(0x85);
    if (lo < 0 || hi < 0) return -1;
    int rpm = lo | (hi << 8);
    return rpm == 0xFFFF ? -1 : rpm;
}
void sensors_led(int led, int mode) {
    if (!ec_ok) return;
    ec_write(0x0C, (uint8_t)(led | (mode == 1 ? 0x80 : mode == 2 ? 0xC0 : 0x00)));
}
