#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "io.h"
#include "cpu.h"
#include "pit.h"
#include "serial.h"
#include "mem.h"
#include "ps2.h"
#include "drivers/sound.h"
#include "drivers/usb.h"
#include "drivers/i2c.h"
#include "drivers/blkdev.h"
#include "drivers/netdev.h"
#include "acpi.h"
#include "sensors.h"
#include "bios.h"
#include "textmode.h"
#include "earlycon.h"

void blkbios_init(int biosdev);
#include "log.h"
#include "app.h"

/* Multiboot2 information tags */
struct mb2_tag { uint32_t type, size; };
struct mb2_mmap_entry { uint64_t base, len; uint32_t type, reserved; };
struct mb2_tag_mmap { uint32_t type, size, entry_size, entry_version; struct mb2_mmap_entry entries[]; };
struct __attribute__((packed)) mb2_tag_fb {
    uint32_t type, size; uint64_t addr; uint32_t pitch, width, height; uint8_t bpp, fb_type; uint16_t reserved;
    uint8_t r_pos, r_size, g_pos, g_size, b_pos, b_size;
};

extern char _kernel_end[];
void platform_audio_init(void);

static void halt(void) { for (;;) { cli(); hlt(); } }

void kmain(uint32_t magic, uint32_t info_addr) {
    serial_init();
    logf("\nBARE!: boot (i386, multiboot2 magic %s)", magic == 0x36d76289 ? "ok" : "BAD");
    if (magic != 0x36d76289) halt();

    cpu_init();
    timer_start(1000);

    struct fb_info fb = { 0 };
    uint64_t best = 0, best_len = 0;
    bool uefi = false; int biosdev = -1;
    uint64_t text_addr = 0; int text_cols = 0, text_rows = 0;
    uint32_t total = *(uint32_t *)info_addr;
    uint32_t reserve_end = ((uint32_t)_kernel_end > info_addr + total ? (uint32_t)_kernel_end : info_addr + total);
    for (uint32_t p = info_addr + 8; p < info_addr + total;) {
        struct mb2_tag *t = (struct mb2_tag *)p;
        if (t->type == 0) break;
        if (t->type == 6) {
            struct mb2_tag_mmap *m = (struct mb2_tag_mmap *)t;
            for (uint32_t off = sizeof *m; off < m->size; off += m->entry_size) {
                struct mb2_mmap_entry *e = (struct mb2_mmap_entry *)((char *)m + off);
                logf("memmap %08lx +%08lx type %u", e->base, e->len, e->type);
                pmm_note(e->base, e->len, e->type == 1 || (e->type >= 3 && e->type <= 5) ? MAP_RAM : MAP_RESERVED);
                if (e->type != 1) continue;
                uint64_t b = e->base, end = e->base + e->len;
                if (end > 0xFFFFF000ull) end = 0xFFFFF000ull;
                if (b < 0x100000) continue;
                if (b < reserve_end) b = (reserve_end + 0xFFF) & ~0xFFFull;
                if (end > b && end - b > best_len) { best = b; best_len = end - b; }
            }
        } else if (t->type == 11 || t->type == 12 || t->type == 19 || t->type == 20) {
            uefi = true;
        } else if (t->type == 5) {
            biosdev = (int)*(uint32_t *)((char *)t + 8);
        } else if (t->type == 14 || t->type == 15) {         /* a copy of the ACPI RSDP (15: the newer, with the XSDT) */
            acpi_set_rsdp((char *)t + 8);
        } else if (t->type == 8) {
            struct mb2_tag_fb *f = (struct mb2_tag_fb *)t;
            if (f->fb_type == 2) { text_addr = f->addr; text_cols = (int)f->width; text_rows = (int)f->height; logf("text mode %ux%u", f->width, f->height); }
            else if (f->fb_type != 1) logf("framebuffer is not RGB (type %u)", f->fb_type);
            else if (f->addr >> 32) logf("framebuffer at %lx: above 4 GiB, out of this 32-bit build's reach (the stick boots the 64-bit one on UEFI)", f->addr);
            else {
                fb.addr = (void *)(uintptr_t)f->addr; fb.width = f->width; fb.height = f->height; fb.pitch = f->pitch; fb.bpp = f->bpp;
                fb.r_shift = f->r_pos; fb.r_size = f->r_size; fb.g_shift = f->g_pos; fb.g_size = f->g_size; fb.b_shift = f->b_pos; fb.b_size = f->b_size;
            }
        }
        p += (t->size + 7) & ~7u;
    }
    pmm_set_region(best, best + best_len);
    if (fb.addr) logf("framebuffer %ux%u pitch %u bpp %u at %p", fb.width, fb.height, fb.pitch, fb.bpp, fb.addr);
    if (fb.addr && fb.bpp != 15 && fb.bpp != 16 && fb.bpp != 24 && fb.bpp != 32) { logf("unsupported bpp"); fb.addr = 0; }   /* core/gfx.c writes these four */
    if (fb.addr) earlycon_start(&fb);                /* the boot log on screen until the instrument starts */
    if (!fb.addr) {
        if (text_addr) textmode_notice(text_addr, text_cols, text_rows);
        fb = fb_in_ram();
    }

    ps2_init();
    sensors_init();
    logf("firmware: %s, bios boot device %x", uefi ? "UEFI" : "BIOS", biosdev);
    bios_init(!uefi);
    blkbios_init(biosdev);
    if (!bios_available()) { usb_init(); internal_disks_init(); }   /* no firmware services: our own USB, SATA and NVMe */
    else usb_prepare();                              /* the BIOS keeps USB; memory set aside in case it's taken over later */
    net_ports_init();                                /* Ethernet (the BIOS has nothing there to conflict with) */
    if (!ps2_has_touchpad()) i2chid_init();          /* a touchpad on I2C, as most laptops since about 2015 have */
    const struct sound *snd = sound_init();
    platform_audio_init();
    logf("audio: %s", snd ? snd->long_name : "PC speaker fallback");

    earlycon_stop();                                 /* the instrument paints from here; its first frame covers the log */
    app_init(&fb, snd ? snd->rate() : 48000);
    logf("running");
    app_run();
}
