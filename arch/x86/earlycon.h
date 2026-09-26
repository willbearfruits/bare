#pragma once
#include "platform.h"
void earlycon_start(const struct fb_info *fb);    /* draws what was logged so far, then every new line */
void earlycon_putc(char c);
void earlycon_stop(void);                         /* the instrument takes the screen */
