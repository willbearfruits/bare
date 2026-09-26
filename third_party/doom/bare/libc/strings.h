#pragma once
/* see stdio.h */
#include <stddef.h>
#define strcasecmp  doom_strcasecmp
#define strncasecmp doom_strncasecmp
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
