#pragma once
/* What the portable core needs from a platform (arch/<x>/ implements these). */
#include <stdint.h>
#include <stdbool.h>

struct fb_info {
    void *addr;
    uint32_t width, height, pitch;   /* pitch in bytes */
    uint8_t bpp;
    uint8_t r_shift, r_size, g_shift, g_size, b_shift, b_size;
};

struct key_event { uint8_t code; uint8_t down; };
/* Pointer (mouse / trackpad). Relative devices fill dx/dy; absolute ones (vmmouse) set abs=1 with ax/ay in 0..32767.
   A touchpad in absolute mode sets touch=1 instead: tx/ty is where a finger is on the pad (0..32767, 0,0 top left),
   z its pressure (0 = lifted, 30 and up = down; 60 where the pad can't tell), finger which one (0..4, kept while it
   stays down) and size how big its contact is (0..255, 0 = unknown); the core decides whether that moves the pointer or
   plays (strum plate, pen, the touch page). */
struct pointer_event { int16_t dx, dy; uint16_t ax, ay; uint8_t abs, buttons; uint8_t touch, z; uint16_t tx, ty; uint8_t finger, size; };

uint64_t plat_ms(void);                       /* milliseconds since boot */
uint64_t plat_us(void);                       /* microseconds since boot (for syncing with other machines) */
bool     plat_key_poll(struct key_event *ev); /* dequeue one key event */
bool     plat_pointer_poll(struct pointer_event *ev);
/* Read n bytes of machine memory for the ROM oscillator (source ids in core/wave.h). Must be safe for any offset. */
void     plat_rom_read(int source, uint32_t offset, uint8_t *dst, int n);
void     plat_putc(char c);                   /* debug log sink */
void     plat_idle(void);                     /* sleep until next interrupt */
uint32_t plat_irq_save(void);                 /* disable interrupts, return old state */
void     plat_irq_restore(uint32_t st);

/* Block storage (BIOS int 13h on a BIOS boot; USB sticks through our own USB stack otherwise). Drive 0 is the boot drive
   when known. */
int      plat_blk_drives(void);
void     plat_blk_rescan(void);
uint64_t plat_blk_sectors(int drive);
bool     plat_blk_read(int drive, uint64_t lba, uint32_t n, void *dst);
bool     plat_blk_write(int drive, uint64_t lba, uint32_t n, const void *src);
void     plat_reboot(void);
const char *plat_blk_name(int drive);         /* for people: "USB: Generic Flash Disk", "NVMe …: model", "BIOS disk 81h" */
uint32_t plat_boot_disk_id(void);             /* the MBR signature of the disk the machine started from; 0 = not known */

/* Network: an Ethernet port, when the machine has one the drivers know (it may come and go: USB adapters). Frames in and
   out whole (no FCS), polled from the main loop. */
bool plat_net_present(uint8_t mac[6]);
bool plat_net_link(void);                     /* a cable, and the other end answering */
bool plat_net_send(const void *frame, int len);
int  plat_net_recv(void *frame, int cap);     /* one frame, 0 when none is waiting */
void plat_net_multicast(bool all);            /* take in every multicast frame (the groups are sorted out above) */

/* Machine personality: sensors and lights (any may be unavailable). */
int      plat_cpu_temp(void);                 /* °C or -1 */
int      plat_fan_rpm(void);                  /* or -1 */
const char *plat_machine(void);               /* DMI name or "" */
void     plat_led(int led, int mode);         /* 0 power LED, 10 lid logo; 0 off 1 on 2 blink (no-op if unsupported) */
void     plat_set_burn(bool burn);            /* idle: spin (heat the CPU) instead of halting */

/* Wall clock (CMOS RTC on x86). False if unavailable. */
struct rtc_time { uint16_t year; uint8_t month, day, hour, min, sec; };
bool     plat_rtc(struct rtc_time *t);

/* Large zeroed allocation that is never freed (tape buffers). NULL if the machine hasn't got the memory. */
void    *plat_alloc(uint32_t bytes);
uint32_t plat_alloc_avail(void);              /* what plat_alloc can still hand out, in bytes */

/* Audio backend status, for display and the 1-bit toggle. */
void     plat_audio_poll(void);               /* slow housekeeping: jack detection etc. — call a few times a second */
const char *plat_audio_name(void);            /* e.g. "HDA 48k" or "PC speaker" */
bool     plat_audio_onebit(void);             /* true when the 1-bit speaker is the active output */
void     plat_audio_toggle_onebit(void);
void     plat_audio_set_onebit(bool on);      /* where there is a sound chip to choose it over */
bool     plat_audio_onebit_chosen(void);      /* the choice, not the fallback */
int      plat_audio_load(void);               /* % of CPU time spent rendering audio in the interrupt (-1 unknown) */
uint32_t plat_audio_latency(void);            /* frames between rendering sound and hearing it (the tape records that much back) */
void     plat_audio_hold(bool on);            /* silence out, and the engine left alone: someone else is rendering (an export) */
void     plat_audio_status(char *out, int cap);  /* one line about the output path, for the log view; call often */
void     plat_audio_test_tone(bool on);       /* 440 Hz straight into the output buffer, bypassing the engine */
void     plat_audio_all_outputs(bool on);     /* every output on, headphone detection ignored */
/* Audio input (line in, microphones): the platform lists what it has and one is on at a time; its frames arrive
   through audio_input_push (core/audio.h), from the audio interrupt, at the output's rate. */
int      plat_audio_inputs(const char **names, int max);   /* 0 = none */
bool     plat_audio_input(int index);                      /* -1 = off; false if it could not */
bool     plat_audio_input_jack(int index);                 /* something is plugged in there, where the jack can tell */
/* MIDI ports (serial, MPU-401, USB): bytes in and out, collected by the platform so none are lost */
int      plat_midi_ports(const char **names, int max);
/* a camera (USB video: a laptop's webcam, or one plugged in). on: stream (its light comes on) or stop; the newest
   picture as 8-bit brightness, w x h, and its number (0: none yet) */
bool     plat_camera(char *name, int cap);
bool     plat_camera_on(bool on);
uint32_t plat_camera_frame(const uint8_t **luma, int *w, int *h);
bool     plat_midi_open(int index);                        /* -1 = close */
int      plat_midi_read(uint8_t *buf, int max);            /* what has come in: bytes, 0 = nothing */
int      plat_midi_write(const uint8_t *buf, int n);       /* bytes the port took now; the rest another time */
/* A hardware report, for someone looking at a machine from afar: the platform's files (a PCI listing, the ACPI
   tables), which the FILE page's log view writes to the stick with the log. A file is its pieces, in order. */
bool     plat_report_file(int index, char *name, int cap, uint32_t *size);          /* false past the last */
bool     plat_report_piece(int index, int piece, const uint8_t **data, uint32_t *len);   /* false past the last */
/* USB: 0 = nothing our own USB stack can drive, 1 = the firmware's (a BIOS boot: taking it over brings USB MIDI, and the
   stick then goes through our driver too, until the next boot), 2 = our own stack runs */
int      plat_usb(void);
bool     plat_usb_takeover(void);                          /* takes a second or two; then rescan the drives */
