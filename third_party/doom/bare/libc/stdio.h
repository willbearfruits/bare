#pragma once
/* BARE!'s C library for Doom's engine (bare/libc.c): what the engine uses, no more. Every function is renamed doom_*
   so it can't meet another C library's (the host checks link Doom into a Linux program); memcpy, memmove, memset,
   memcmp and strlen are BARE!'s own (core/libc.c). Printing goes to BARE!'s log; files are on the stick (core/doom.c). */
#include <stddef.h>
#include <stdarg.h>
typedef struct doom_file FILE;
#define stdin  doom_stdin
#define stdout doom_stdout
#define stderr doom_stderr
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 512
#define FILENAME_MAX 256
#define printf    doom_printf
#define fprintf   doom_fprintf
#define vprintf   doom_vprintf
#define vfprintf  doom_vfprintf
#define sprintf   doom_sprintf
#define snprintf  doom_snprintf
#define vsnprintf doom_vsnprintf
#define vsprintf  doom_vsprintf
#define sscanf    doom_sscanf
#define puts      doom_puts
#define putchar   doom_putchar
#define fputs     doom_fputs
#define fputc     doom_fputc
#define putc      doom_fputc
#define fopen     doom_fopen
#define fclose    doom_fclose
#define fread     doom_fread
#define fwrite    doom_fwrite
#define fseek     doom_fseek
#define ftell     doom_ftell
#define fflush    doom_fflush
#define feof      doom_feof
#define ferror    doom_ferror
#define fgetc     doom_fgetc
#define getc      doom_fgetc
#define fgets     doom_fgets
#define remove    doom_remove
#define rename    doom_rename
#define setvbuf   doom_setvbuf
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int sprintf(char *out, const char *fmt, ...);
int snprintf(char *out, size_t cap, const char *fmt, ...);
int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap);
int vsprintf(char *out, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int fputs(const char *s, FILE *f);
int fputc(int c, FILE *f);
FILE *fopen(const char *name, const char *mode);
int fclose(FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int fflush(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fgetc(FILE *f);
char *fgets(char *s, int n, FILE *f);
int remove(const char *name);
int rename(const char *from, const char *to);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
#define _IONBF 2
#define _IOLBF 1
#define _IOFBF 0
