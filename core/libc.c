#include "libc.h"

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {           /* aligned: move words */
        uint32_t *dw = (uint32_t *)d; const uint32_t *sw = (const uint32_t *)s;
        for (; n >= 4; n -= 4) *dw++ = *sw++;
        d = (uint8_t *)dw; s = (const uint8_t *)sw;
    }
    while (n--) *d++ = *s++;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}
void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    if (((uintptr_t)d & 3) == 0) {
        uint32_t w = (uint8_t)c * 0x01010101u, *dw = (uint32_t *)d;
        for (; n >= 4; n -= 4) *dw++ = w;
        d = (uint8_t *)dw;
    }
    while (n--) *d++ = (uint8_t)c;
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static int put(char *out, size_t cap, size_t pos, char c) {
    if (pos + 1 < cap) out[pos] = c;
    return 1;
}

int vsnfmt(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t pos = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { pos += put(out, cap, pos, *fmt); continue; }
        fmt++;
        bool zero = false, plus = false; int width = 0; bool lng = false;
        if (*fmt == '+') { plus = true; fmt++; }
        if (*fmt == '0') { zero = true; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { lng = true; fmt++; }
        char buf[24]; int len = 0; const char *s = buf; bool neg = false;
        switch (*fmt) {
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; len = (int)strlen(s); zero = false; break;
        case 'c': buf[0] = (char)va_arg(ap, int); len = 1; break;
        case '%': buf[0] = '%'; len = 1; break;
        case 'd': case 'i': {
            int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            uint64_t u = v < 0 ? (neg = true, (uint64_t)(-v)) : (uint64_t)v;
            char t[24]; int n = 0; do { t[n++] = '0' + u % 10; u /= 10; } while (u);
            if (plus && !neg) buf[len++] = '+';
            while (n) buf[len++] = t[--n];
            break; }
        case 'u': {
            uint64_t u = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            char t[24]; int n = 0; do { t[n++] = '0' + u % 10; u /= 10; } while (u);
            while (n) buf[len++] = t[--n];
            break; }
        case 'x': case 'X': case 'p': {
            uint64_t u = (*fmt == 'p') ? (uint64_t)va_arg(ap, void *) : lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            const char *dig = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char t[24]; int n = 0; do { t[n++] = dig[u & 15]; u >>= 4; } while (u);
            while (n) buf[len++] = t[--n];
            break; }
        default: buf[0] = '%'; buf[1] = *fmt; len = 2; break;
        }
        int pad = width - len - (neg ? 1 : 0);
        if (neg && zero) pos += put(out, cap, pos, '-');
        while (pad-- > 0) pos += put(out, cap, pos, zero ? '0' : ' ');
        if (neg && !zero) pos += put(out, cap, pos, '-');
        for (int i = 0; i < len; i++) pos += put(out, cap, pos, s[i]);
    }
    if (cap) out[pos < cap ? pos : cap - 1] = 0;
    return (int)pos;
}
int snfmt(char *out, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vsnfmt(out, cap, fmt, ap); va_end(ap); return n;
}

uint32_t crc32(const void *data, size_t n) {
    const uint8_t *p = data; uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

#if __SIZEOF_POINTER__ == 4
/* 64-bit division helpers the compiler emits calls to on 32-bit targets. */
uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem) {
    if (d == 0) { if (rem) *rem = 0; return 0; }
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    if (rem) *rem = r;
    return q;
}
uint64_t __udivdi3(uint64_t n, uint64_t d) { return __udivmoddi4(n, d, 0); }
uint64_t __umoddi3(uint64_t n, uint64_t d) { uint64_t r; __udivmoddi4(n, d, &r); return r; }
int64_t __divdi3(int64_t n, int64_t d) {
    bool neg = (n < 0) != (d < 0);
    uint64_t q = __udivmoddi4(n < 0 ? -(uint64_t)n : (uint64_t)n, d < 0 ? -(uint64_t)d : (uint64_t)d, 0);
    return neg ? -(int64_t)q : (int64_t)q;
}
int64_t __moddi3(int64_t n, int64_t d) {
    uint64_t r; __udivmoddi4(n < 0 ? -(uint64_t)n : (uint64_t)n, d < 0 ? -(uint64_t)d : (uint64_t)d, &r);
    return n < 0 ? -(int64_t)r : (int64_t)r;
}
#endif
