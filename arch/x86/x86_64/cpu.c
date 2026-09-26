#include "cpu.h"
#include "io.h"
#include "pic.h"
#include "log.h"
#include "ui.h"           /* a fault paints itself on screen: a laptop has no serial port to read */

struct int_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

/* ---- GDT ---- */
static uint64_t gdt[] = {
    0,
    0x00AF9A000000FFFFull,   /* 0x08: 64-bit code */
    0x00CF92000000FFFFull,   /* 0x10: data */
};
struct __attribute__((packed)) descr { uint16_t limit; uint64_t base; };

/* ---- IDT ---- */
struct __attribute__((packed)) idt_entry {
    uint16_t off0; uint16_t sel; uint8_t ist; uint8_t type; uint16_t off1; uint32_t off2; uint32_t zero;
};
static struct idt_entry idt[256];
extern char isr_stubs[];
static irq_handler_t irq_handlers[16];

static void set_gate(int v, void *fn) {
    uint64_t a = (uint64_t)fn;
    idt[v] = (struct idt_entry){ a & 0xFFFF, 0x08, 0, 0x8E, (a >> 16) & 0xFFFF, (uint32_t)(a >> 32), 0 };
}

void pic_eoi(int irq) { if (irq >= 8) outb(PIC2, 0x20); outb(PIC1, 0x20); }

static const char *const exc_names[32] = {
    "#DE divide", "#DB debug", "NMI", "#BP breakpoint", "#OF overflow", "#BR bound", "#UD invalid opcode", "#NM no fpu",
    "#DF double fault", "coproc", "#TS bad tss", "#NP segment not present", "#SS stack", "#GP general protection",
    "#PF page fault", "reserved", "#MF x87", "#AC alignment", "#MC machine check", "#XM simd", "#VE virt", "#CP cp",
    "r", "r", "r", "r", "r", "r", "r", "r", "r", "r" };

void isr_dispatch(struct int_frame *f) {
    if (f->vector < 32) {
        uint64_t cr2; __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        logf("\n*** EXCEPTION %lu (%s) err=%lx rip=%lx rsp=%lx cr2=%lx", f->vector, exc_names[f->vector], f->error, f->rip, f->rsp, cr2);
        char l1[64], l2[80];
        snfmt(l1, sizeof l1, "EXCEPTION %lu %s", f->vector, exc_names[f->vector]);
        snfmt(l2, sizeof l2, "rip=%lx err=%lx cr2=%lx", f->rip, f->error, cr2);
        ui_boot_message(l1, l2);
        logf("rax=%lx rbx=%lx rcx=%lx rdx=%lx rsi=%lx rdi=%lx rbp=%lx", f->rax, f->rbx, f->rcx, f->rdx, f->rsi, f->rdi, f->rbp);
        for (;;) { cli(); hlt(); }
    }
    if (f->vector >= 0x20 && f->vector < 0x30) {
        int irq = (int)f->vector - 0x20;
        if (pic_spurious(irq)) return;
        if (irq_handlers[irq]) irq_handlers[irq]();
        pic_eoi(irq);
    }
}

void irq_install(int irq, irq_handler_t h) { irq_handlers[irq] = h; pic_unmask(irq); }

void cpu_init(void) {
    struct descr g = { sizeof gdt - 1, (uint64_t)gdt };
    __asm__ volatile("lgdt %0" :: "m"(g));
    __asm__ volatile(
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t movw %%ax, %%es\n\t movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t movw %%ax, %%fs\n\t movw %%ax, %%gs\n\t"
        ::: "rax", "memory");
    for (int i = 0; i < 256; i++) set_gate(i, isr_stubs + 16 * i);
    struct descr d = { sizeof idt - 1, (uint64_t)idt };
    __asm__ volatile("lidt %0" :: "m"(d));
    pic_init();
}
