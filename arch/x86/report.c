/* The hardware report's files: HB-PCI.TXT, every PCI function with its IDs, class and address registers, and
   HB-ACPI.BIN, the firmware's ACPI tables one after the other (each starts with its signature and length, so they
   split apart again). */
#include "platform.h"
#include "acpi.h"
#include "pci.h"
#include "libc.h"

static char pci_text[32768];
static uint32_t pci_len;

static void pci_listing(void) {
    int n = snfmt(pci_text, sizeof pci_text, "bus:dev.fn vendor:device class   rev header  BAR0     BAR1     BAR2     BAR3     BAR4     BAR5\r\n");
    for (int bus = 0; bus < 64; bus++)
        for (int dev = 0; dev < 32; dev++)
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);
                if ((id & 0xFFFF) == 0xFFFF || n > (int)sizeof pci_text - 160) continue;
                uint32_t cl = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 8), hd = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x0C);
                n += snfmt(pci_text + n, sizeof pci_text - (size_t)n, "%02x:%02x.%d   %04x:%04x   %06x %02x  %02x     ", bus, dev, fn,
                           id & 0xFFFF, id >> 16, cl >> 8, cl & 0xFF, (hd >> 16) & 0xFF);
                for (int b = 0; b < 6; b++) n += snfmt(pci_text + n, sizeof pci_text - (size_t)n, " %08x", pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, (uint8_t)(0x10 + 4 * b)));
                n += snfmt(pci_text + n, sizeof pci_text - (size_t)n, "\r\n");
            }
    pci_len = (uint32_t)n;
}

bool plat_report_file(int index, char *name, int cap, uint32_t *size) {
    if (index == 0) { pci_listing(); snfmt(name, (size_t)cap, "HB-PCI.TXT"); *size = pci_len; return true; }
    if (index == 1 && acpi_tables()) {
        uint32_t total = 0; char sig[5]; const uint8_t *d; uint32_t len;
        for (int i = 0; acpi_table(i, sig, &d, &len); i++) total += len;
        snfmt(name, (size_t)cap, "HB-ACPI.BIN"); *size = total;
        return true;
    }
    return false;
}

bool plat_report_piece(int index, int piece, const uint8_t **data, uint32_t *len) {
    if (index == 0) { if (piece) return false; *data = (const uint8_t *)pci_text; *len = pci_len; return true; }
    char sig[5];
    return index == 1 && acpi_table(piece, sig, data, len);
}
