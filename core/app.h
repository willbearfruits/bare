#pragma once
#include "platform.h"
#define BARE_RELEASE "1.0"                     /* the release; the build number (date and time) is app_version */
#define BARE_STAGE   " beta"                   /* what the release still is, shown after it; "" once it is final */
void app_init(const struct fb_info *fb, uint32_t sample_rate);
void app_step(uint64_t now_ms);             /* one pass of the main loop (the host test harness drives this) */
void app_background(uint64_t now_ms);       /* its input and background work alone (Doom's waits run it) */
void app_run(void) __attribute__((noreturn));
int  app_page(void);                                  /* the page showing (PAGE_*) */
int  app_key(void);                                   /* the key whose tab is lit: the one that opened it (0 = F1) */
void app_goto_key(int k, uint64_t now);               /* key k pressed: what it opens; pressed again, the page's next */
