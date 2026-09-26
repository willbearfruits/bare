#pragma once
/* 8259 PIC helpers shared by both x86 CPU layers. */
#include <stdint.h>
#include <stdbool.h>
#include "io.h"
#define PIC1 0x20
#define PIC2 0xA0
static inline void pic_init(void) {
    outb(PIC1, 0x11); io_wait(); outb(PIC2, 0x11); io_wait();
    outb(PIC1 + 1, 0x20); io_wait(); outb(PIC2 + 1, 0x28); io_wait();   /* vectors 0x20..0x2F */
    outb(PIC1 + 1, 4); io_wait(); outb(PIC2 + 1, 2); io_wait();
    outb(PIC1 + 1, 1); io_wait(); outb(PIC2 + 1, 1); io_wait();
    outb(PIC1 + 1, 0xFF); outb(PIC2 + 1, 0xFF);                         /* mask everything */
}
static inline void pic_unmask(int irq) {
    uint16_t port = irq < 8 ? PIC1 + 1 : PIC2 + 1;
    outb(port, inb(port) & ~(1 << (irq & 7)));
    if (irq >= 8) outb(PIC1 + 1, inb(PIC1 + 1) & ~(1 << 2));            /* cascade */
}
static inline bool pic_spurious(int irq) {
    if (irq != 7 && irq != 15) return false;
    uint16_t p = irq == 7 ? PIC1 : PIC2;
    outb(p, 0x0B);
    if (inb(p) & 0x80) return false;
    if (irq == 15) outb(PIC1, 0x20);
    return true;
}
