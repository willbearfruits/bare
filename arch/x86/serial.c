#include "serial.h"
#include "io.h"
#define COM1 0x3F8
void serial_init(void) {
    outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01); outb(COM1 + 1, 0x00);      /* 115200 */
    outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 0x0B);
}
static bool log_on = true;
void serial_log(bool on) { log_on = on; if (on) serial_init(); }
void serial_putc(char c) {
    if (!log_on) return;
    if (c == '\n') serial_putc('\r');
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++) ;
    outb(COM1, (uint8_t)c);
}
