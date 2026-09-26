/* BARE!'s C library for Doom's engine: memory from the piece BARE! set aside for Doom, printing into BARE!'s log,
   files on the stick through core/doomhost.c. The names are doom_* (the headers in libc/ rename them).
   Files are whole in memory: one read from the stick when first read, and written ones are kept until the tic is over
   (doom_libc_flush) — a save game is written as temp.dsg and then renamed, so only the final name reaches the stick. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include "doomhost.h"

int errno;

/* ---- memory: first fit, neighbours joined as it walks ---- */
struct blk { uint32_t size, used, pad[2]; };               /* 16 bytes: what follows stays 16-aligned */
static uint8_t *heap_at; static uint32_t heap_len;
static struct blk *next_blk(struct blk *b) { return (struct blk *)((uint8_t *)b + b->size); }
static void heap_start(void) {
    heap_at = doom_heap(&heap_len);
    heap_len &= ~15u;
    struct blk *b = (struct blk *)heap_at;
    b->size = heap_len; b->used = 0;
}
void *malloc(size_t n) {
    if (!heap_at) heap_start();
    uint32_t need = (uint32_t)((n + 15) & ~(size_t)15) + sizeof(struct blk);
    uint8_t *end = heap_at + heap_len;
    for (struct blk *b = (struct blk *)heap_at; (uint8_t *)b < end; b = next_blk(b)) {
        if (b->used) continue;
        while ((uint8_t *)next_blk(b) < end && !next_blk(b)->used) b->size += next_blk(b)->size;
        if (b->size < need) continue;
        if (b->size - need >= 2 * sizeof(struct blk)) {
            struct blk *rest = (struct blk *)((uint8_t *)b + need);
            rest->size = b->size - need; rest->used = 0;
            b->size = need;
        }
        b->used = 1;
        return b + 1;
    }
    doom_print("malloc: out of memory\n");
    return NULL;
}
void free(void *p) { if (p) ((struct blk *)p - 1)->used = 0; }
void *calloc(size_t n, size_t size) { void *p = malloc(n * size); if (p) memset(p, 0, n * size); return p; }
void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    struct blk *b = (struct blk *)p - 1;
    size_t have = b->size - sizeof(struct blk);
    if (n <= have) return p;
    void *q = malloc(n);
    if (q) { memcpy(q, p, have); free(p); }
    return q;
}

/* ---- the end ---- */
void exit(int code) { doom_fatal(code ? "the engine gave up" : "the engine ended"); }
void abort(void) { doom_fatal("the engine aborted"); }
char *getenv(const char *name) { return NULL; }
int system(const char *cmd) { return -1; }

/* ---- strings ---- */
void *memchr(const void *s, int c, size_t n) { const uint8_t *p = s; for (; n; n--, p++) if (*p == (uint8_t)c) return (void *)p; return NULL; }
size_t strnlen(const char *s, size_t n) { size_t i = 0; while (i < n && s[i]) i++; return i; }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
char *strncpy(char *d, const char *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n) { char *e = d + strlen(d); size_t i = 0; for (; i < n && s[i]; i++) e[i] = s[i]; e[i] = 0; return d; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (uint8_t)*a - (uint8_t)*b; }
int strncmp(const char *a, const char *b, size_t n) { for (; n; n--, a++, b++) { if (*a != *b || !*a) return (uint8_t)*a - (uint8_t)*b; } return 0; }
int strcasecmp(const char *a, const char *b) { while (*a && tolower(*a) == tolower(*b)) { a++; b++; } return tolower((uint8_t)*a) - tolower((uint8_t)*b); }
int strncasecmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) { int x = tolower((uint8_t)*a), y = tolower((uint8_t)*b); if (x != y || !x) return x - y; }
    return 0;
}
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; } }
char *strrchr(const char *s, int c) { const char *r = NULL; for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; } }
char *strstr(const char *h, const char *n) {
    size_t l = strlen(n);
    for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h;
    return l ? NULL : (char *)h;
}
char *strdup(const char *s) { size_t l = strlen(s) + 1; char *d = malloc(l); if (d) memcpy(d, s, l); return d; }
char *strerror(int e) { return e == ENOENT ? "no such file" : "error"; }

long strtol(const char *s, char **end, int base) {
    while (isspace((uint8_t)*s)) s++;
    bool neg = *s == '-'; if (*s == '-' || *s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') base = 8;
    else if (base == 0) base = 10;
    unsigned long v = 0;
    for (;; s++) {
        int d = isdigit((uint8_t)*s) ? *s - '0' : isalpha((uint8_t)*s) ? tolower((uint8_t)*s) - 'a' + 10 : 99;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
    }
    if (end) *end = (char *)s;
    return neg ? -(long)v : (long)v;
}
unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtol(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

/* ---- formatting: flags, width, precision, h/l/ll/z; no floating point ---- */
struct out { char *p; size_t cap, n; };
static void put(struct out *o, char c) { if (o->n + 1 < o->cap) o->p[o->n] = c; o->n++; }
int vsnprintf(char *buf, size_t cap, const char *f, va_list ap) {
    struct out o = { buf, cap, 0 };
    for (; *f; f++) {
        if (*f != '%') { put(&o, *f); continue; }
        f++;
        bool left = false, zero = false, plus = false, space = false, alt = false;
        for (;; f++) {
            if (*f == '-') left = true; else if (*f == '0') zero = true; else if (*f == '+') plus = true;
            else if (*f == ' ') space = true; else if (*f == '#') alt = true; else break;
        }
        int width = 0, prec = -1;
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = true; width = -width; } f++; }
        else while (isdigit((uint8_t)*f)) width = width * 10 + *f++ - '0';
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; } else while (isdigit((uint8_t)*f)) prec = prec * 10 + *f++ - '0';
        }
        int len = 0;
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 'j' || *f == 't') { if (*f == 'l') len++; f++; }
        char tmp[40]; const char *s = tmp; int n = 0; char sign = 0;
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            unsigned long long v; int base = *f == 'x' || *f == 'X' || *f == 'p' ? 16 : *f == 'o' ? 8 : 10;
            if (*f == 'p') v = (uintptr_t)va_arg(ap, void *), alt = true;
            else if (*f == 'd' || *f == 'i') {
                long long x = len >= 2 ? va_arg(ap, long long) : len == 1 ? va_arg(ap, long) : va_arg(ap, int);
                if (x < 0) { sign = '-'; v = (unsigned long long)-x; } else { v = (unsigned long long)x; sign = plus ? '+' : space ? ' ' : 0; }
            } else v = len >= 2 ? va_arg(ap, unsigned long long) : len == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            const char *dig = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char r[24]; int nd = 0;
            do { r[nd++] = dig[v % (unsigned)base]; v /= (unsigned)base; } while (v);
            char pre[3]; int np = 0;
            if (sign) pre[np++] = sign;
            if (alt && base == 16) { pre[np++] = '0'; pre[np++] = *f == 'X' ? 'X' : 'x'; }
            int zeros = prec > nd ? prec - nd : 0;
            if (zero && !left && prec < 0 && width > np + nd) zeros = width - np - nd;   /* zeros after the sign and 0x */
            if (zeros > 30 - nd) zeros = 30 - nd;
            for (int j = 0; j < np; j++) tmp[n++] = pre[j];
            while (zeros-- > 0) tmp[n++] = '0';
            while (nd) tmp[n++] = r[--nd];
            break; }
        case 'c': tmp[0] = (char)va_arg(ap, int); n = 1; break;
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; n = (int)(prec >= 0 ? strnlen(s, (size_t)prec) : strlen(s)); break;
        case '%': tmp[0] = '%'; n = 1; break;
        case 0: f--; continue;
        default: tmp[0] = '%'; tmp[1] = *f; n = 2; break;
        }
        int padn = width > n ? width - n : 0;
        if (!left) while (padn-- > 0) put(&o, ' ');
        for (int j = 0; j < n; j++) put(&o, s[j]);
        if (left) while (padn-- > 0) put(&o, ' ');
    }
    if (cap) buf[o.n < cap ? o.n : cap - 1] = 0;
    return (int)o.n;
}
int vsprintf(char *out, const char *fmt, va_list ap) { return vsnprintf(out, 1u << 20, fmt, ap); }
int snprintf(char *out, size_t cap, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(out, cap, fmt, ap); va_end(ap); return n; }
int sprintf(char *out, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(out, 1u << 20, fmt, ap); va_end(ap); return n; }

/* sscanf: what the engine asks of it — numbers (%d %i %u %x %o) and literal text */
int sscanf(const char *s, const char *f, ...) {
    va_list ap; va_start(ap, f);
    int got = 0;
    for (; *f; f++) {
        if (isspace((uint8_t)*f)) { while (isspace((uint8_t)*s)) s++; continue; }
        if (*f != '%') { if (*s != *f) break; s++; continue; }
        f++;
        int base = *f == 'x' || *f == 'X' ? 16 : *f == 'o' ? 8 : *f == 'i' ? 0 : 10;
        while (isspace((uint8_t)*s)) s++;
        char *end;
        long v = strtol(s, &end, base);
        if (end == s) break;
        *va_arg(ap, int *) = (int)v;
        s = end; got++;
    }
    va_end(ap);
    return got;
}

/* ---- files ---- */
struct doom_file { char name[64]; char mode; bool console, loaded, eof; uint8_t *buf; uint32_t size, cap, pos; };
static struct doom_file con_out = { "stdout", 'w', true }, con_err = { "stderr", 'w', true };
FILE *stdin = NULL, *stdout = &con_out, *stderr = &con_err;
#define FILES 8
static struct doom_file files[FILES];
/* written and closed, not on the stick yet */
static struct { char name[64]; uint8_t *buf; uint32_t size; } kept[FILES];

static int kept_find(const char *name) { for (int i = 0; i < FILES; i++) if (kept[i].buf && !strcasecmp(kept[i].name, name)) return i; return -1; }
static void kept_drop(int i) { free(kept[i].buf); kept[i].buf = NULL; }

FILE *fopen(const char *name, const char *mode) {
    struct doom_file *f = NULL;
    for (int i = 0; i < FILES && !f; i++) if (!files[i].mode) f = &files[i];
    if (!f || strlen(name) >= sizeof f->name) { errno = EINVAL; return NULL; }
    memset(f, 0, sizeof *f);
    strcpy(f->name, name);
    if (mode[0] == 'r') {
        int k = kept_find(name);
        int32_t size = k >= 0 ? (int32_t)kept[k].size : doom_file_size(name);
        if (size < 0) { errno = ENOENT; return NULL; }
        f->size = (uint32_t)size;
    } else {
        f->cap = 4096; f->buf = malloc(f->cap);
        if (!f->buf) { errno = EIO; return NULL; }
        f->loaded = true;
    }
    f->mode = mode[0] == 'r' ? 'r' : 'w';
    return f;
}
static bool load(struct doom_file *f) {
    if (f->loaded) return true;
    f->buf = malloc(f->size ? f->size : 1);
    if (!f->buf) return false;
    int k = kept_find(f->name);
    if (k >= 0) memcpy(f->buf, kept[k].buf, f->size);
    else if (f->size && !doom_file_read(f->name, 0, f->buf, f->size)) { free(f->buf); f->buf = NULL; return false; }
    f->loaded = true;
    return true;
}
size_t fread(void *p, size_t size, size_t n, FILE *f) {
    if (!f || f->console || !size || !load(f)) return 0;
    size_t want = size * n, have = f->pos < f->size ? f->size - f->pos : 0;
    if (want > have) { want = have - have % size; f->eof = true; }
    memcpy(p, f->buf + f->pos, want);
    f->pos += (uint32_t)want;
    return want / size;
}
size_t fwrite(const void *p, size_t size, size_t n, FILE *f) {
    size_t len = size * n;
    if (!f || !len) return 0;
    if (f->console) { char t[256]; size_t k = len < sizeof t - 1 ? len : sizeof t - 1; memcpy(t, p, k); t[k] = 0; doom_print(t); return n; }
    if (f->mode != 'w') return 0;
    if (f->pos + len > f->cap) {
        uint32_t cap = f->cap; while (f->pos + len > cap) cap *= 2;
        uint8_t *b = realloc(f->buf, cap);
        if (!b) return 0;
        f->buf = b; f->cap = cap;
    }
    memcpy(f->buf + f->pos, p, len);
    f->pos += (uint32_t)len;
    if (f->pos > f->size) f->size = f->pos;
    return n;
}
int fclose(FILE *f) {
    if (!f || f->console) return 0;
    if (f->mode == 'w') {
        int k = kept_find(f->name);
        if (k >= 0) kept_drop(k);
        for (k = 0; k < FILES && kept[k].buf; k++) {}
        if (k < FILES) { strcpy(kept[k].name, f->name); kept[k].buf = f->buf; kept[k].size = f->size; f->buf = NULL; }
    }
    free(f->buf);
    f->mode = 0;
    return 0;
}
int fseek(FILE *f, long off, int whence) {
    if (!f || f->console) return -1;
    long base = whence == SEEK_CUR ? (long)f->pos : whence == SEEK_END ? (long)f->size : 0;
    if (base + off < 0) return -1;
    f->pos = (uint32_t)(base + off); f->eof = false;
    return 0;
}
long ftell(FILE *f) { return f && !f->console ? (long)f->pos : -1; }
int fflush(FILE *f) { return 0; }
int feof(FILE *f) { return f && f->eof; }
int ferror(FILE *f) { return 0; }
int setvbuf(FILE *f, char *buf, int mode, size_t size) { return 0; }
int fgetc(FILE *f) { uint8_t c; return fread(&c, 1, 1, f) == 1 ? c : EOF; }
char *fgets(char *s, int n, FILE *f) {
    int i = 0;
    while (i < n - 1) { int c = fgetc(f); if (c == EOF) break; s[i++] = (char)c; if (c == '\n') break; }
    s[i] = 0;
    return i ? s : NULL;
}
int remove(const char *name) { int k = kept_find(name); if (k >= 0) kept_drop(k); return 0; }   /* the stick's copy is replaced when written */
int rename(const char *from, const char *to) {
    int k = kept_find(from);
    if (k < 0 || strlen(to) >= sizeof kept[k].name) return -1;
    int old = kept_find(to);
    if (old >= 0) kept_drop(old);
    strcpy(kept[k].name, to);
    return 0;
}
int mkdir(const char *path, mode_t mode) { return 0; }

/* what was written goes to the stick: once a tic is over */
void doom_libc_flush(void) {
    for (int k = 0; k < FILES; k++) if (kept[k].buf) { doom_file_write(kept[k].name, kept[k].buf, kept[k].size); kept_drop(k); }
}

/* ---- printing: into BARE!'s log ---- */
int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char t[512];
    int n = vsnprintf(t, sizeof t, fmt, ap);
    if (!f || f->console) doom_print(t); else fwrite(t, 1, strlen(t), f);
    return n;
}
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vfprintf(stdout, fmt, ap); va_end(ap); return n; }
int fprintf(FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vfprintf(f, fmt, ap); va_end(ap); return n; }
int fputs(const char *s, FILE *f) { return (int)fwrite(s, 1, strlen(s), f); }
int fputc(int c, FILE *f) { char t = (char)c; return fwrite(&t, 1, 1, f) ? c : EOF; }
int puts(const char *s) { doom_print(s); doom_print("\n"); return 0; }
int putchar(int c) { char t[2] = { (char)c, 0 }; doom_print(t); return c; }
