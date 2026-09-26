/* ACPI tables, read-only, and a few facts out of the DSDT/SSDT bytecode found as patterns rather than by running it:
   Device blocks (0x5B 0x82, a package length, a name), Name(_HID/_ADR, …) inside them, and resource descriptors in the
   literal buffers resource templates compile to (I2cSerialBus 0x8E, Memory32Fixed 0x86). Good enough to say where a
   touchpad sits; a full interpreter is what an operating system brings. */
#include "acpi.h"
#include "mem.h"
#include "libc.h"
#include "log.h"

static uint8_t rsdp[36]; static bool have_rsdp, inited;
#define TABLES 64
static struct { char sig[5]; uint64_t phys; uint32_t len; } tab[TABLES];
static int ntab;

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t)u32(p + 4) << 32; }
static const uint8_t *map(uint64_t phys, uint32_t len) {
    if (!phys || (sizeof(void *) == 4 && (phys >> 32))) return 0;
    return phys_map(phys, len);
}
static bool sum_ok(const uint8_t *p, uint32_t n) { uint8_t s = 0; for (uint32_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]); return s == 0; }

void acpi_set_rsdp(const void *r) { memcpy(rsdp, r, sizeof rsdp); have_rsdp = !memcmp(rsdp, "RSD PTR ", 8); }
void acpi_set_rsdp_phys(uint64_t phys) { const uint8_t *p = map(phys, sizeof rsdp); if (p) acpi_set_rsdp(p); }

/* a BIOS machine: the RSDP is on a 16-byte boundary in the EBDA's first KiB or between E0000 and FFFFF */
static void find_rsdp(void) {
    const uint8_t *bda = map(0x40E, 2);
    uint64_t ebda = bda ? (uint64_t)u16(bda) << 4 : 0;
    uint64_t from[2] = { ebda, 0xE0000 }, to[2] = { ebda + 1024, 0x100000 };
    for (int r = 0; r < 2 && !have_rsdp; r++) {
        if (from[r] < 0x80000 || from[r] >= 0xA0000) { if (r == 0) continue; }
        const uint8_t *p = map(from[r], (uint32_t)(to[r] - from[r]));
        for (uint32_t a = 0; p && a + 20 <= to[r] - from[r]; a += 16)
            if (!memcmp(p + a, "RSD PTR ", 8) && sum_ok(p + a, 20)) { acpi_set_rsdp(p + a); break; }
    }
}

static void add_table(uint64_t phys) {
    const uint8_t *h = map(phys, 36);
    if (!h || ntab == TABLES) return;
    uint32_t len = u32(h + 4);
    if (len < 36 || len > (4u << 20)) return;
    for (int i = 0; i < ntab; i++) if (tab[i].phys == phys) return;
    memcpy(tab[ntab].sig, h, 4); tab[ntab].sig[4] = 0; tab[ntab].phys = phys; tab[ntab].len = len;
    ntab++;
}

bool acpi_init(void) {
    if (inited) return ntab > 0;
    inited = true;
    if (!have_rsdp) find_rsdp();
    if (!have_rsdp) { logf("acpi: no RSDP"); return false; }
    uint64_t xsdt = rsdp[15] >= 2 ? u64(rsdp + 24) : 0, rsdt = u32(rsdp + 16);
    uint64_t root = xsdt; int es = 8;
    if (!root || (sizeof(void *) == 4 && (root >> 32))) { root = rsdt; es = 4; }
    const uint8_t *h = map(root, 36);
    uint32_t len = h ? u32(h + 4) : 0;
    if (len < 36 || len > 65536) { logf("acpi: no root table"); return false; }
    h = map(root, len);
    for (uint32_t off = 36; off + (uint32_t)es <= len; off += (uint32_t)es) add_table(es == 8 ? u64(h + off) : u32(h + off));
    for (int i = 0; i < ntab; i++) {
        if (memcmp(tab[i].sig, "FACP", 4)) continue;                  /* the DSDT is named by the FADT */
        const uint8_t *f = map(tab[i].phys, tab[i].len);
        uint64_t dsdt = tab[i].len >= 148 ? u64(f + 140) : 0;
        if (!dsdt || (sizeof(void *) == 4 && (dsdt >> 32))) dsdt = u32(f + 40);
        add_table(dsdt);
        break;
    }
    int ssdt = 0; uint32_t dsdt_len = 0;
    for (int i = 0; i < ntab; i++) { if (!memcmp(tab[i].sig, "SSDT", 4)) ssdt++; if (!memcmp(tab[i].sig, "DSDT", 4)) dsdt_len = tab[i].len; }
    logf("acpi: revision %u, %d tables, DSDT %u bytes, %d SSDTs", rsdp[15], ntab, dsdt_len, ssdt);
    return ntab > 0;
}

/* ---- AML, looked at as bytes ---- */
struct span { uint32_t start, end; char name[5]; };
#define SPANS 8192
static struct span spans[SPANS]; static int nspans;

static int pkglen(const uint8_t *p, uint32_t n, uint32_t at, uint32_t *len) {
    if (at >= n) return 0;
    int count = p[at] >> 6;
    if (at + (uint32_t)count >= n) return 0;
    if (!count) { *len = p[at] & 0x3F; return 1; }
    uint32_t v = p[at] & 0x0F;
    for (int k = 0; k < count; k++) v |= (uint32_t)p[at + 1 + k] << (4 + 8 * k);
    *len = v;
    return 1 + count;
}
static bool seg_char(uint8_t c, bool first) { return (c >= 'A' && c <= 'Z') || c == '_' || (!first && c >= '0' && c <= '9'); }
/* the NameString at `at`: its last segment into name, its length in bytes (0: not a name) */
static int namestring(const uint8_t *p, uint32_t n, uint32_t at, char name[5]) {
    uint32_t i = at; int segs = 1;
    while (i < n && (p[i] == '\\' || p[i] == '^')) i++;
    if (i < n && p[i] == 0x2E) { segs = 2; i++; }
    else if (i + 1 < n && p[i] == 0x2F) { segs = p[i + 1]; i += 2; }
    if (segs < 1 || segs > 16 || i + 4u * (uint32_t)segs > n) return 0;
    for (int s = 0; s < segs; s++) for (int k = 0; k < 4; k++) if (!seg_char(p[i + (uint32_t)s * 4 + (uint32_t)k], k == 0)) return 0;
    memcpy(name, p + i + 4u * (uint32_t)(segs - 1), 4); name[4] = 0;
    return (int)(i + 4u * (uint32_t)segs - at);
}
static void find_devices(const uint8_t *p, uint32_t n) {
    nspans = 0;
    for (uint32_t i = 36; i + 8 < n && nspans < SPANS; i++) {
        if (p[i] != 0x5B || p[i + 1] != 0x82) continue;
        uint32_t len; int lb = pkglen(p, n, i + 2, &len);
        if (!lb || len < (uint32_t)lb + 4 || i + 2 + len > n) continue;
        char name[5];
        if (!namestring(p, n, i + 2 + (uint32_t)lb, name)) continue;
        spans[nspans].start = i; spans[nspans].end = i + 2 + len; memcpy(spans[nspans].name, name, 5);
        nspans++;
    }
}
static int owner(uint32_t at) {                                     /* the innermost device around a byte */
    int best = -1;
    for (int k = 0; k < nspans; k++) if (spans[k].start < at && at < spans[k].end && (best < 0 || spans[k].start > spans[best].start)) best = k;
    return best;
}
static void eisa(uint32_t v, char out[9]) {                         /* a compressed EISA ID, e.g. 41 D0 0C 50 = PNP0C50 */
    uint16_t a = (uint16_t)((v & 0xFF) << 8 | ((v >> 8) & 0xFF)), b = (uint16_t)(((v >> 16) & 0xFF) << 8 | (v >> 24));
    static const char hex[] = "0123456789ABCDEF";
    out[0] = (char)('@' + ((a >> 10) & 31)); out[1] = (char)('@' + ((a >> 5) & 31)); out[2] = (char)('@' + (a & 31));
    for (int k = 0; k < 4; k++) out[3 + k] = hex[(b >> (12 - 4 * k)) & 15];
    out[7] = 0;
}
/* Name(seg, …) in [a, b): as text (a string, or an EISA ID spelled out) and as a number */
static bool name_value(const uint8_t *p, uint32_t a, uint32_t b, const char *seg, char *text, int cap, uint64_t *num) {
    for (uint32_t i = a; i + 6 < b; i++) {
        if (p[i] != 0x08 || memcmp(p + i + 1, seg, 4)) continue;
        uint32_t v = i + 5; uint64_t x = 0;
        switch (p[v]) {
        case 0x0D: {
            int k = 0;
            for (uint32_t j = v + 1; j < b && p[j] && k < cap - 1; j++) text[k++] = (char)p[j];
            if (cap) text[k] = 0;
            return true; }
        case 0x0C: x = u32(p + v + 1); if (cap >= 9) eisa((uint32_t)x, text); break;
        case 0x0B: x = u16(p + v + 1); break;
        case 0x0A: x = p[v + 1]; break;
        case 0x00: case 0x01: x = p[v]; break;
        default: continue;
        }
        if (num) *num = x;
        return true;
    }
    return false;
}
static bool has_bytes(const uint8_t *p, uint32_t a, uint32_t b, const void *pat, uint32_t n) {
    for (uint32_t i = a; i + n <= b; i++) if (!memcmp(p + i, pat, n)) return true;
    return false;
}

/* the controller a device's I2cSerialBus names: a Device of that name with an _ADR (a Name, or a Method returning a
   constant) for a PCI function, or a Memory32Fixed for registers at a fixed address */
static void resolve_bus(struct acpi_i2c *d) {
    for (int t = 0; t < ntab; t++) {
        if (memcmp(tab[t].sig, "DSDT", 4) && memcmp(tab[t].sig, "SSDT", 4)) continue;
        const uint8_t *p = map(tab[t].phys, tab[t].len); uint32_t n = tab[t].len;
        for (uint32_t i = 36; i + 8 < n; i++) {
            if (p[i] != 0x5B || p[i + 1] != 0x82) continue;
            uint32_t len; int lb = pkglen(p, n, i + 2, &len);
            char name[5];
            if (!lb || i + 2 + len > n || !namestring(p, n, i + 2 + (uint32_t)lb, name) || memcmp(name, d->bus, 4)) continue;
            uint32_t a = i, b = i + 2 + len; uint64_t adr; char tmp[9];
            if (name_value(p, a, b, "_ADR", tmp, 0, &adr)) { d->bus_pci = true; d->bus_dev = (uint8_t)(adr >> 16); d->bus_fn = (uint8_t)adr; return; }
            for (uint32_t k = a; k + 12 < b; k++) {                   /* Method (_ADR) { Return (constant) } */
                uint32_t mlen; int lb;
                if (p[k] != 0x14 || !(lb = pkglen(p, b, k + 1, &mlen)) || memcmp(p + k + 1 + lb, "_ADR", 4)) continue;
                uint32_t r = k + 1 + (uint32_t)lb + 5;                /* past the name and the method's flags */
                if (r + 5 < b && p[r] == 0xA4 && p[r + 1] == 0x0C) { adr = u32(p + r + 2); d->bus_pci = true; d->bus_dev = (uint8_t)(adr >> 16); d->bus_fn = (uint8_t)adr; return; }
            }
            for (uint32_t k = a; k + 12 <= b; k++)
                if (p[k] == 0x86 && p[k + 1] == 0x09 && p[k + 2] == 0x00 && u32(p + k + 4)) { d->bus_mmio = u32(p + k + 4); return; }
        }
    }
}

int acpi_tables(void) { return acpi_init() ? ntab : 0; }
uint64_t acpi_ecam(void) {
    for (int i = 0; i < ntab; i++)
        if (!memcmp(tab[i].sig, "MCFG", 4) && tab[i].len >= 60) { const uint8_t *m = map(tab[i].phys, tab[i].len); return m ? u64(m + 44) : 0; }
    return 0;
}
/* Memory32Fixed descriptors with an address (motherboard resources, devices at fixed places): a descriptor is 12 bytes
   and another descriptor or the end tag follows it, which keeps stray 0x86 bytes in the bytecode out */
#define FIXED 128
static struct { uint32_t base, len; } fixed[FIXED];
static int nfixed = -1;
static bool res_tag(uint8_t c) { return c == 0x79 || (c >= 0x85 && c <= 0x8E) || c == 0x47 || c == 0x4B || c == 0x22 || c == 0x23 || c == 0x2A; }
bool acpi_fixed_mmio(uint64_t base, uint64_t len, uint64_t *start) {
    if (nfixed < 0) {
        nfixed = 0;
        for (int t = 0; acpi_init() && t < ntab; t++) {
            if (memcmp(tab[t].sig, "DSDT", 4) && memcmp(tab[t].sig, "SSDT", 4)) continue;
            const uint8_t *p = map(tab[t].phys, tab[t].len); uint32_t n = tab[t].len;
            for (uint32_t i = 36; p && i + 12 < n && nfixed < FIXED; i++) {
                if (p[i] != 0x86 || p[i + 1] != 0x09 || p[i + 2] || p[i + 3] > 1 || !res_tag(p[i + 12])) continue;
                uint32_t b = u32(p + i + 4), l = u32(p + i + 8);
                if (b && l && l <= (256u << 20)) { fixed[nfixed].base = b; fixed[nfixed].len = l; nfixed++; }
                i += 11;
            }
        }
    }
    for (int i = 0; i < nfixed; i++)
        if (fixed[i].base < base + len && base < (uint64_t)fixed[i].base + fixed[i].len) { *start = fixed[i].base; return true; }
    return false;
}

bool acpi_table(int i, char sig[5], const uint8_t **data, uint32_t *len) {
    if (i < 0 || i >= ntab) return false;
    memcpy(sig, tab[i].sig, 5); *len = tab[i].len; *data = map(tab[i].phys, tab[i].len);
    return *data != 0;
}

int acpi_blank;
int acpi_i2c_devices(struct acpi_i2c *out, int max) {
    if (!acpi_init()) return 0;
    int n = 0; acpi_blank = 0;
    static const uint8_t pnp0c50[4] = { 0x41, 0xD0, 0x0C, 0x50 };
    for (int t = 0; t < ntab && n < max; t++) {
        if (memcmp(tab[t].sig, "DSDT", 4) && memcmp(tab[t].sig, "SSDT", 4)) continue;
        const uint8_t *p = map(tab[t].phys, tab[t].len); uint32_t len = tab[t].len;
        find_devices(p, len);
        for (uint32_t i = 36; i + 20 < len && n < max; i++) {
            if (p[i] != 0x8E) continue;
            uint16_t dl = u16(p + i + 1), tdl = u16(p + i + 10), addr = u16(p + i + 16);
            uint8_t rev = p[i + 3], type = p[i + 5]; uint32_t speed = u32(p + i + 12);
            if (type != 1 || !rev || rev > 2 || tdl < 6 || tdl > 64 || dl < 9u + tdl || dl > 256 || i + 3u + dl > len) continue;
            if (speed < 10000 || speed > 3400000 || addr >= 0x400) continue;
            char path[40]; int k = 0; uint32_t s = i + 12u + tdl, e = i + 3u + dl;
            for (; s < e && p[s] && k < 39; s++) {
                uint8_t c = p[s];
                if (!(seg_char(c, false) || c == '\\' || c == '^' || c == '.')) break;
                path[k++] = (char)c;
            }
            path[k] = 0;
            if (k < 4 || s >= e || p[s]) continue;                    /* not a name ending in a zero: not a descriptor */
            struct acpi_i2c d; memset(&d, 0, sizeof d);
            d.addr = addr; d.speed = speed;
            memcpy(d.bus, path + k - 4, 4); d.bus[4] = 0;
            int o = owner(i);
            if (o >= 0) {
                memcpy(d.dev, spans[o].name, 5);
                if (!name_value(p, spans[o].start, spans[o].end, "_HID", d.hid, sizeof d.hid, 0))
                    for (uint32_t k = spans[o].start; k + 8 < spans[o].end && !d.hid[0]; k++) {   /* Method (_HID): its first string */
                        uint32_t mlen; int lb;
                        if (p[k] != 0x14 || !(lb = pkglen(p, spans[o].end, k + 1, &mlen)) || memcmp(p + k + 1 + lb, "_HID", 4)) continue;
                        for (uint32_t j = k + 1 + (uint32_t)lb; j + 2 < k + 1 + mlen && j + 2 < spans[o].end; j++)
                            if (p[j] == 0x0D && seg_char(p[j + 1], true)) {
                                int c = 0;
                                for (uint32_t q = j + 1; q < spans[o].end && p[q] && c < (int)sizeof d.hid - 1; q++) d.hid[c++] = (char)p[q];
                                d.hid[c] = 0;
                                break;
                            }
                    }
                d.hid_i2c = has_bytes(p, spans[o].start, spans[o].end, "PNP0C50", 7) || has_bytes(p, spans[o].start, spans[o].end, "ACPI0C50", 8)
                         || has_bytes(p, spans[o].start, spans[o].end, pnp0c50, 4);
            }
            bool dup = false;
            for (int j = 0; j < n; j++) dup |= out[j].addr == d.addr && !memcmp(out[j].bus, d.bus, 4) && !memcmp(out[j].dev, d.dev, 4);
            if (!d.addr && !d.hid_i2c) { acpi_blank++; dup = true; }  /* a template the firmware's code fills in: counted only */
            if (!dup) out[n++] = d;
            i += 2u + dl;
        }
    }
    for (int k = 0; k < n; k++) resolve_bus(&out[k]);
    return n;
}
