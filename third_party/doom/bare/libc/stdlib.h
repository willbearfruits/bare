#pragma once
/* see stdio.h */
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7FFF
#define malloc  doom_malloc
#define calloc  doom_calloc
#define realloc doom_realloc
#define free    doom_free
#define exit    doom_exit
#define abort   doom_abort
#define atoi    doom_atoi
#define atol    doom_atol
#define strtol  doom_strtol
#define strtoul doom_strtoul
#define getenv  doom_getenv
#define system  doom_system
#define qsort   doom_qsort
#define rand    doom_rand
#define srand   doom_srand
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int code) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
char *getenv(const char *name);
int system(const char *cmd);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
int rand(void);
void srand(unsigned seed);
static inline int abs(int x) { return x < 0 ? -x : x; }
static inline long labs(long x) { return x < 0 ? -x : x; }
