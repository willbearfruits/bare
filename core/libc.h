#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

/* Tiny printf: %s %c %d %i %u %x %X %p %%, with optional 0-pad width and + for signed numbers.
   %ld %lu %lx always take a 64-bit argument (long is 32-bit on i386, so pass int64_t/uint64_t, never long). */
int  vsnfmt(char *out, size_t cap, const char *fmt, va_list ap);
int  snfmt(char *out, size_t cap, const char *fmt, ...);

uint32_t crc32(const void *data, size_t n);

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
