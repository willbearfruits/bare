#pragma once
#include <stdint.h>
typedef void (*irq_handler_t)(void);
void cpu_init(void);                        /* GDT + IDT + PIC remap (all IRQs masked) */
void irq_install(int irq, irq_handler_t h); /* 0..15, unmasks the line */
/* implemented in cpu.c of each arch, used by isr dispatch */
void pic_eoi(int irq);
