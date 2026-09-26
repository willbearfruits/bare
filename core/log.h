#pragma once
#include "libc.h"
#include "platform.h"

/* The log goes to the platform's sink (the serial port) and into a ring in memory, which the FILE page shows (TAB):
   most laptops have no serial port. */
void log_keep(char c);
int  log_line(int back, char *out, int cap);    /* line `back` counted from the newest (0); -1 past the oldest */

static inline void log_str(const char *s) { while (*s) { log_keep(*s); plat_putc(*s++); } }
static inline void logf(const char *fmt, ...) {
    char buf[256]; va_list ap; va_start(ap, fmt); vsnfmt(buf, sizeof buf, fmt, ap); va_end(ap);
    log_str(buf); log_keep('\n'); plat_putc('\n');
}
