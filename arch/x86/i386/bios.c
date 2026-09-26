#include "bios.h"
#include "io.h"
#include "pic.h"
#include "libc.h"
#include "log.h"
#include "pit.h"

extern char tramp_start[], tramp_end[];
static bool avail;

void bios_init(bool available) {
    avail = available;
    if (!avail) return;
    memcpy((void *)0x8000, tramp_start, (size_t)(tramp_end - tramp_start));
    logf("bios: trampoline %u bytes at 0x8000", (unsigned)(tramp_end - tramp_start));
}
bool bios_available(void) { return avail; }

static void pic_remap(uint8_t off1, uint8_t off2) {
    uint8_t m1 = inb(PIC1 + 1), m2 = inb(PIC2 + 1);
    outb(PIC1, 0x11); io_wait(); outb(PIC2, 0x11); io_wait();
    outb(PIC1 + 1, off1); io_wait(); outb(PIC2 + 1, off2); io_wait();
    outb(PIC1 + 1, 4); io_wait(); outb(PIC2 + 1, 2); io_wait();
    outb(PIC1 + 1, 1); io_wait(); outb(PIC2 + 1, 1); io_wait();
    outb(PIC1 + 1, m1); outb(PIC2 + 1, m2);
}

void bios_call(struct bios_regs *r) {
    if (!avail) { r->flags = 1; return; }
    uint32_t fl = read_flags();
    cli();
    memcpy((void *)0x7800, r, sizeof *r);
    pic_remap(0x08, 0x70);
    ((void (*)(void))0x8000)();
    pic_remap(0x20, 0x28);
    if (pit_restore()) logf("bios: int %02x had reprogrammed the timer; restored", r->intno);
    memcpy(r, (void *)0x7800, sizeof *r);
    for (int i = 0; i < 16 && (inb(0x64) & 1); i++) inb(0x60);   /* drop keys the BIOS handler buffered */
    if (fl & 0x200) sti();
}
