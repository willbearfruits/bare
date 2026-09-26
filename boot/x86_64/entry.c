#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "io.h"
#include "cpu.h"
#include "pit.h"
#include "serial.h"
#include "mem.h"
#include "arch.h"
#include "ps2.h"
#include "drivers/sound.h"
#include "drivers/usb.h"
#include "drivers/i2c.h"
#include "drivers/blkdev.h"
#include "drivers/netdev.h"
#include "acpi.h"
#include "sensors.h"
#include "textmode.h"
#include "earlycon.h"
#include "log.h"
#include "app.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(3);
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_req = { .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0 };
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_req = { .id = LIMINE_HHDM_REQUEST_ID, .revision = 0 };
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request mm_req = { .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0 };
__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_req = { .id = LIMINE_RSDP_REQUEST_ID, .revision = 0 };
__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_file_request file_req = { .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID, .revision = 0 };
extern uint32_t boot_disk_id;
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

void platform_audio_init(void);

static void halt(void) { for (;;) { cli(); hlt(); } }

void kmain(void) {
    serial_init();
    logf("\nBARE!: boot (limine base revision %s)", LIMINE_BASE_REVISION_SUPPORTED(base_revision) ? "ok" : "UNSUPPORTED");
    if (!hhdm_req.response || !mm_req.response) { logf("no hhdm/memmap"); halt(); }

    cpu_init();
    mem_init_x86_64(mm_req.response, hhdm_req.response->offset);   /* before the timer: the HPET fallback maps registers */
    timer_start(1000);

    struct fb_info fb = { 0 };
    if (fb_req.response && fb_req.response->framebuffer_count > 0) {
        struct limine_framebuffer *lfb = fb_req.response->framebuffers[0];
        fb = (struct fb_info){
            .addr = lfb->address, .width = (uint32_t)lfb->width, .height = (uint32_t)lfb->height, .pitch = (uint32_t)lfb->pitch,
            .bpp = (uint8_t)lfb->bpp,
            .r_shift = lfb->red_mask_shift, .r_size = lfb->red_mask_size,
            .g_shift = lfb->green_mask_shift, .g_size = lfb->green_mask_size,
            .b_shift = lfb->blue_mask_shift, .b_size = lfb->blue_mask_size,
        };
        logf("framebuffer %ux%u pitch %u bpp %u", fb.width, fb.height, fb.pitch, fb.bpp);
        /* what the screen says it is (EDID's first detailed timing): if it is bigger than the framebuffer, the firmware
           gave the bootloader no way to know, and the picture is being stretched */
        const uint8_t *ed = lfb->edid;
        if (lfb->edid_size >= 128 && ed && ed[0] == 0 && ed[1] == 0xFF) {
            uint32_t w = ed[56] | (ed[58] & 0xF0u) << 4, h = ed[59] | (ed[61] & 0xF0u) << 4;
            logf("screen: %ux%u by its EDID%s", w, h, w > fb.width || h > fb.height ? ": bigger than the framebuffer" : "");
        } else logf("screen: no EDID from the firmware (the bootloader took the mode it was in)");
        if (fb.bpp != 15 && fb.bpp != 16 && fb.bpp != 24 && fb.bpp != 32) { logf("unsupported bpp"); fb.addr = 0; }   /* core/gfx.c writes these four */
        if (fb.addr) earlycon_start(&fb);            /* the boot log on screen until the instrument starts */
    }
    if (!fb.addr) {
        textmode_notice(0xB8000, 80, 25);            /* where a BIOS boot leaves the text screen; harmless elsewhere */
        fb = fb_in_ram();
    }

    ps2_init();
    sensors_init();
    if (file_req.response && file_req.response->executable_file) {   /* the disk this kernel was read from: its MBR signature */
        boot_disk_id = file_req.response->executable_file->mbr_disk_id;
        logf("boot disk: signature %08x", boot_disk_id);
    }
    usb_init();                                      /* no BIOS here: sticks, keyboards and MIDI through our own USB stack */
    internal_disks_init();                           /* and the internal disks through our own SATA and NVMe drivers */
    net_ports_init();                                /* Ethernet */
    if (rsdp_req.response) {                         /* physical from base revision 3; older Limine gave an HHDM address */
        uint64_t a = (uint64_t)rsdp_req.response->address, off = hhdm_req.response->offset;
        acpi_set_rsdp_phys(a >= off ? a - off : a);
    }
    if (!ps2_has_touchpad()) i2chid_init();          /* a touchpad on I2C, as most laptops since about 2015 have */
    const struct sound *snd = sound_init();
    platform_audio_init();
    logf("audio: %s", snd ? snd->long_name : "PC speaker fallback");

    earlycon_stop();                                 /* the instrument paints from here; its first frame covers the log */
    app_init(&fb, snd ? snd->rate() : 48000);
    logf("running");
    app_run();
}
