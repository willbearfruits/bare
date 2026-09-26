#pragma once
#include <stdint.h>
#include <stdbool.h>
struct __attribute__((packed)) bios_regs { uint32_t eax, ebx, ecx, edx, esi, edi; uint16_t ds, es, flags; uint8_t intno; };
void bios_init(bool available);          /* copies the trampoline into low memory; available=false on UEFI boots */
bool bios_available(void);
void bios_call(struct bios_regs *r);     /* r->intno selects the interrupt; flags returned (CF = bit 0) */
/* transfer buffer in low memory for disk I/O */
#define BIOS_BUF_ADDR 0x10000
#define BIOS_BUF_SIZE 0x10000
#define BIOS_DAP_ADDR 0x7900
