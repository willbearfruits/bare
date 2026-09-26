#pragma once
#include "platform.h"
#define BARE_RELEASE "2.5"                     /* the release; the build number (date and time) is app_version */
void app_init(const struct fb_info *fb, uint32_t sample_rate);
void app_step(uint64_t now_ms);             /* one pass of the main loop (the host test harness drives this) */
void app_background(uint64_t now_ms);       /* its input and background work alone (Doom's waits run it) */
void app_run(void) __attribute__((noreturn));
int  app_page(void);                                  /* the page showing (PAGE_*) */
