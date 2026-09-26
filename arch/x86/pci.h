#pragma once
#include <stdint.h>
#include <stdbool.h>
struct pci_dev { uint8_t bus, dev, fn; uint16_t vendor, device; uint8_t class, subclass, prog_if; };
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v);
bool     pci_find_class(uint8_t class, uint8_t subclass, struct pci_dev *out);
int      pci_find_all_class(uint8_t class, uint8_t subclass, struct pci_dev *out, int max);
uint64_t pci_bar(const struct pci_dev *d, int bar, bool *is_mem);
void     pci_wake(const struct pci_dev *d);     /* out of D3, its BARs kept */
void     pci_enable(const struct pci_dev *d);   /* awake, memory space + bus master */
uint64_t pci_place_bar(const struct pci_dev *d, int bar);   /* an address for a BAR the firmware left empty, 0 if none */
