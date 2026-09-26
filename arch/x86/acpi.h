#pragma once
/* ACPI: the firmware's tables. Only what drivers need is read — the list of tables, and, without running any AML, the
   I2C devices the DSDT and SSDTs declare: a resource template is a literal buffer in the bytecode, so its
   I2cSerialBus descriptors can be found as bytes, with the names of the devices around them. */
#include <stdint.h>
#include <stdbool.h>

void acpi_set_rsdp(const void *rsdp);          /* a copy the loader handed over (multiboot2's ACPI tags) */
void acpi_set_rsdp_phys(uint64_t phys);        /* where the RSDP is (Limine) */
bool acpi_init(void);                          /* finds the tables; on a BIOS machine, searches for the RSDP itself */

struct acpi_i2c {
    char     dev[5];                           /* the device's name in the namespace, e.g. "TPD0" */
    char     hid[9];                           /* its _HID: "ELAN1200", "PNP0C50" … */
    bool     hid_i2c;                          /* HID over I2C (PNP0C50 / ACPI0C50 in its _HID or _CID) */
    uint16_t addr;                             /* 7-bit address on the bus */
    uint32_t speed;                            /* Hz */
    char     bus[5];                           /* the controller's name, e.g. "I2C1" */
    bool     bus_pci; uint8_t bus_dev, bus_fn; /* the controller: a PCI function on bus 0 (_ADR) … */
    uint64_t bus_mmio;                         /* … or registers at a fixed address (Memory32Fixed), 0 = unknown */
};
int acpi_i2c_devices(struct acpi_i2c *out, int max);   /* those with an address, or HID ones; the rest counted: */
extern int acpi_blank;                                   /* descriptors without an address (templates firmware code fills in) */
uint64_t acpi_ecam(void);                                /* PCI configuration space (MCFG), 0 = unknown */
bool acpi_fixed_mmio(uint64_t base, uint64_t len, uint64_t *start);   /* overlaps a Memory32Fixed the tables declare */
int  acpi_tables(void);
bool acpi_table(int i, char sig[5], const uint8_t **data, uint32_t *len);   /* for the hardware report */
