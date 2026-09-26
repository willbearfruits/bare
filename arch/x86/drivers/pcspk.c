#include "pcspk.h"
#include "io.h"

static uint32_t cur;
void pcspk_tone(uint32_t hz) {
    if (hz == cur) return;
    cur = hz;
    if (!hz) { outb(0x61, inb(0x61) & ~3); return; }
    uint32_t div = 1193182 / hz; if (div > 65535) div = 65535; if (div < 1) div = 1;
    outb(0x43, 0xB6);                       /* channel 2, lo/hi, mode 3 square wave */
    outb(0x42, div & 0xFF); outb(0x42, (div >> 8) & 0xFF);
    uint8_t p = inb(0x61);
    if ((p & 3) != 3) outb(0x61, p | 3);
}
