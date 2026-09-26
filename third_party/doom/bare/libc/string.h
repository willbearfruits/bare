#pragma once
/* see stdio.h: memcpy, memmove, memset, memcmp and strlen are BARE!'s (core/libc.c) */
#include <stddef.h>
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
#define memchr  doom_memchr
#define strnlen doom_strnlen
#define strcpy  doom_strcpy
#define strncpy doom_strncpy
#define strcat  doom_strcat
#define strncat doom_strncat
#define strcmp  doom_strcmp
#define strncmp doom_strncmp
#define strchr  doom_strchr
#define strrchr doom_strrchr
#define strstr  doom_strstr
#define strdup  doom_strdup
#define strerror doom_strerror
void *memchr(const void *s, int c, size_t n);
size_t strnlen(const char *s, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strdup(const char *s);
char *strerror(int e);
