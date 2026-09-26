#pragma once
/* When the loader leaves the machine in text mode (no VESA linear framebuffer, or one we can't draw), the instrument
   runs without a picture: a notice goes on the text screen and the pixels go to a framebuffer in RAM. */
#include "platform.h"

void           textmode_notice(uint64_t phys, int cols, int rows);   /* phys: the text buffer, 0xB8000 on a PC */
struct fb_info fb_in_ram(void);
