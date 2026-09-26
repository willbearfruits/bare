#include "textmode.h"
#include "mem.h"
#include "io.h"
#include "log.h"

static const char *const notice[] = {
    "BARE!",
    "",
    "This machine has no graphics mode the loader can use, only text.",
    "The instrument runs anyway, without a picture: the keys play and the",
    "sound works.",
    "",
    "  1 - =     chord            Q W E      major, minor, 7th",
    "  A - '     strum            Z - /      strum low",
    "  SPACE     hold             up, down   octave",
};

void textmode_notice(uint64_t phys, int cols, int rows) {
    volatile uint16_t *vram = mmio_map(phys, (size_t)cols * rows * 2);
    for (int i = 0; i < cols * rows; i++) vram[i] = 0x0720;
    for (int l = 0; l < (int)(sizeof notice / sizeof *notice) && 2 + l < rows; l++)
        for (int c = 0; notice[l][c] && 4 + c < cols; c++)
            vram[(2 + l) * cols + 4 + c] = (uint16_t)((l == 0 ? 0x0E00 : 0x0700) | (uint8_t)notice[l][c]);
    outb(0x3D4, 0x0A); outb(0x3D5, 0x20);                         /* hide the cursor */
}

struct fb_info fb_in_ram(void) {
    enum { W = 800, H = 600 };
    uint64_t p = pmm_alloc_pages((W * H * 4 + 4095) / 4096);
    logf("no usable framebuffer: running without a picture (%ux%u in RAM)", W, H);
    return (struct fb_info){ .addr = phys_to_virt(p), .width = W, .height = H, .pitch = W * 4, .bpp = 32,
                             .r_shift = 16, .r_size = 8, .g_shift = 8, .g_size = 8, .b_shift = 0, .b_size = 8 };
}
