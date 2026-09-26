/* The HPET in legacy-replacement mode: its timer 0 takes over IRQ 0 from the 8254, which many UEFI-only laptops leave
   switched off. Chipsets put it at FED00000 (ACPI's HPET table says the same on every PC seen so far). */
#include "hpet.h"
#include "mem.h"
#include "log.h"

#define GCAP     0x000       /* id, legacy-route capable (bit 15); counter period in fs at +4 */
#define GCONF    0x010       /* bit 0 enable, bit 1 legacy route */
#define COUNTER  0x0F0
#define T0_CONF  0x100
#define T0_CMP   0x108

bool hpet_start_legacy(uint32_t hz) {
    volatile uint32_t *h = mmio_map(0xFED00000, 0x400);
    uint32_t id = h[GCAP / 4], period = h[GCAP / 4 + 1];
    if (id == 0xFFFFFFFFu || id == 0 || period == 0 || period > 100000000u) { logf("hpet: none at fed00000"); return false; }
    if (!(id & 0x8000)) { logf("hpet: can't take over IRQ 0"); return false; }
    uint32_t t0 = h[T0_CONF / 4];
    if (!(t0 & 0x10)) { logf("hpet: timer 0 can't repeat"); return false; }
    uint32_t step = (uint32_t)(1000000000000000ull / period / hz);          /* counts per tick; 1 s = 1e15 fs */
    h[GCONF / 4] &= ~3u;                                                    /* stop while setting up */
    h[COUNTER / 4] = 0; h[COUNTER / 4 + 1] = 0;
    /* timer 0: interrupt on, periodic, set the period with the next writes, 32-bit; edge-triggered, no FSB route */
    h[T0_CONF / 4] = (t0 & ~0x7E02u) | 0x014Cu;
    h[T0_CMP / 4] = step;                                                   /* the first expiry */
    h[T0_CMP / 4] = step;                                                   /* the period (the set bit clears itself) */
    h[GCONF / 4] |= 3u;                                                     /* run, timer 0 on IRQ 0 */
    logf("hpet: %04x, %u fs per count, %u counts per tick", id >> 16, period, step);
    return true;
}
