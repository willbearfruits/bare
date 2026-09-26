#pragma once
/* I2C controllers: Synopsys DesignWare ones, which Intel's chipsets (the LPSS "Serial IO" functions on PCI) and AMD's
   (at fixed addresses ACPI gives) carry. Polled, master only, 7-bit addresses. */
#include <stdint.h>
#include <stdbool.h>

#define I2C_BUSES 8
int  i2c_init(void);                              /* the PCI controllers, found and set up; returns how many */
void i2c_add_mmio(uint64_t base);                 /* one ACPI places at a fixed address (AMD, or Intel in ACPI mode) */
int  i2c_buses(void);
int  i2c_find(bool pci, uint8_t dev, uint8_t fn, uint64_t mmio);   /* the bus ACPI names by _ADR or address, -1 = none */
const char *i2c_name(int bus);
/* write wn bytes then read rn (a repeated start between): rn, or < 0 — -1 nobody answered, -2 timed out, -3 other */
int  i2c_xfer(int bus, uint16_t addr, const uint8_t *w, int wn, uint8_t *r, int rn);
int  i2c_scan(int bus, uint8_t map[16]);         /* the addresses that answer, a bit each (and logged) */

/* i2chid.c: HID over I2C — the touchpads and touchscreens ACPI names */
void i2chid_init(void);
void i2chid_service(void);                        /* main loop: a report every few milliseconds */
