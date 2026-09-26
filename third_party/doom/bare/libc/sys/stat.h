#pragma once
/* see stdio.h */
#include <sys/types.h>
#define mkdir doom_mkdir
int mkdir(const char *path, mode_t mode);
