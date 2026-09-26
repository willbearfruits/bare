/* Glue between the portable core and the x86-64 hardware layer. */
#include "platform.h"
#include "io.h"
#include "pit.h"
#include "ps2.h"
#include "serial.h"
#include "synth.h"
#include "audio.h"
#include "drivers/sound.h"
#include "drivers/pcspk.h"
#include "mem.h"
#include "wave.h"
#include "libc.h"
#include "sensors.h"
#include "rtc.h"
#include "earlycon.h"
#include "drivers/usb.h"
#include "drivers/i2c.h"

extern char _kernel_start[], _kernel_end[];

static bool onebit;
static int16_t scratch[64 * 2];

uint64_t plat_ms(void) { timer_poll(); return pit_ticks(); }         /* timer_poll: only does anything without timer IRQs */
uint64_t plat_us(void) { return timer_us(); }
bool plat_key_poll(struct key_event *ev) { usb_service(); return ps2_key_poll(ev); }           /* USB keyboards and mice join the PS/2 queues */
bool plat_pointer_poll(struct pointer_event *ev) { usb_service(); i2chid_service(); return ps2_pointer_poll(ev); }

void plat_rom_read(int src, uint32_t off, uint8_t *dst, int n) {
    uint64_t base, size;
    switch (src) {
    case ROM_BIOS:   base = 0xF0000; size = 0x10000; break;
    case ROM_FLASH:  base = 0xFFC00000ull; size = 0x400000; break;
    case ROM_KERNEL: base = 0; size = (uint64_t)(_kernel_end - _kernel_start); break;
    default:         base = 0; size = 0x100000; break;
    }
    if (!size) { memset(dst, 0, n); return; }
    for (int i = 0; i < n; i++) {
        uint64_t o = (off + (uint32_t)i) % size;
        if (src == ROM_KERNEL) { dst[i] = (uint8_t)_kernel_start[o]; continue; }
        volatile uint8_t *p = mmio_map(base + o, 1);
        dst[i] = *p;
    }
}
void plat_putc(char c) { serial_putc(c); earlycon_putc(c); }
static bool burn;
void plat_idle(void) {
    if (timer_polled()) { timer_poll(); __asm__ volatile("pause"); return; }   /* nothing would wake a hlt */
    if (burn) { for (volatile int i = 0; i < 20000; i++) ; } else hlt();
}
void plat_set_burn(bool b) { burn = b; }
int plat_cpu_temp(void) { return sensors_cpu_temp(); }
int plat_fan_rpm(void) { return sensors_fan_rpm(); }
const char *plat_machine(void) { return sensors_machine(); }
void plat_led(int led, int mode) { sensors_led(led, mode); }
uint32_t plat_irq_save(void) { uint32_t f = read_flags(); cli(); return f; }
void plat_irq_restore(uint32_t f) { if (f & 0x200) sti(); }

static uint64_t busy_tsc; static int load_pct = -1;
void plat_audio_poll(void) {
    if (sound && sound->poll_jacks) sound->poll_jacks();
    /* audio CPU load: cycles spent inside the tick hook over cycles elapsed since the last poll */
    static uint64_t last; uint64_t now = rdtsc();
    if (last && now > last) { uint64_t st = plat_irq_save(); load_pct = (int)(busy_tsc * 100 / (now - last)); busy_tsc = 0; plat_irq_restore(st); }
    last = now;
}
int plat_audio_load(void) { return load_pct; }
uint32_t plat_audio_latency(void) { return sound ? sound->latency() : 48; }
static bool held;
void plat_audio_hold(bool on) { held = on; if (sound) sound->hold(on); }
bool plat_rtc(struct rtc_time *t) { return rtc_read(t); }
void *plat_alloc(uint32_t bytes) { if (bytes > pmm_avail()) return 0; return dma_alloc(bytes, 0); }
uint32_t plat_alloc_avail(void) { uint64_t a = pmm_avail(); return a > 0xFFFFF000u ? 0xFFFFF000u : (uint32_t)a; }
const char *plat_audio_name(void) {
    static char name[40];
    if (!sound) return "PC speaker";
    uint32_t r = sound->out_rate;
    const char *hp = !sound->headphones ? "" : sound->headphones() ? " · headphones" : " · speakers";
    if (r % 1000) snfmt(name, sizeof name, "%s %u.%u kHz%s", sound->name, r / 1000, r % 1000 / 100, hp);
    else snfmt(name, sizeof name, "%s %u kHz%s", sound->name, r / 1000, hp);
    return name;
}
bool plat_audio_onebit(void) { return onebit || !sound; }
void plat_audio_toggle_onebit(void) { plat_audio_set_onebit(!onebit); }
void plat_audio_set_onebit(bool on) { if (sound) { onebit = on; audio_set_onebit(on); } }   /* the engine makes the square */
bool plat_audio_onebit_chosen(void) { return onebit; }
void plat_audio_test_tone(bool on) { if (sound) sound->test_tone(on); }
void plat_audio_all_outputs(bool on) { if (sound && sound->all_outputs) sound->all_outputs(on); }
int  plat_audio_inputs(const char **names, int max) { return sound && sound->inputs ? sound->inputs(names, max) : 0; }
bool plat_audio_input(int i) { return sound && sound->input_select ? sound->input_select(i) : i < 0; }
bool plat_audio_input_jack(int i) { return sound && sound->input_jack && sound->input_jack(i); }
int  plat_usb(void) { return usb_running() ? 2 : usb_prepared() ? 1 : 0; }
bool plat_usb_takeover(void) { usb_init(); return usb_running(); }
bool plat_camera(char *name, int cap) { return usb_running() && usb_camera(name, cap); }
bool plat_camera_on(bool on) { return usb_running() && usb_camera_on(on); }
uint32_t plat_camera_frame(const uint8_t **luma, int *w, int *h) { return usb_running() ? usb_camera_frame(luma, w, h) : 0; }

/* Timer interrupts per real second, counted between two ticks of the CMOS clock: shows whether the firmware left our
   1 kHz timer alone. Needs calling often (every frame) to catch the second changing. */
static int timer_rate(void) {                         /* -1 while measuring */
    static int last = -1, secs, rate = -1; static uint64_t t0;
    struct rtc_time t;
    if (!rtc_read(&t)) return -1;
    int s = t.min * 60 + t.sec;
    if (s == last) return rate;
    uint64_t now = pit_ticks();
    if (last >= 0) {
        if (secs == 0) t0 = now;
        else rate = (int)((now - t0) / (uint64_t)secs);
        secs++;
    }
    last = s;
    return rate;
}

void plat_audio_status(char *out, int cap) {
    static char dev[200]; static int calls;
    if (calls++ % 15 == 0) {                             /* the codec is asked a few times a second, not every frame */
        if (sound) sound->status(dev, sizeof dev); else snfmt(dev, sizeof dev, "no sound chip this drives: PC speaker only");
    }
    int r = timer_rate();
    if (r >= 0) snfmt(out, (size_t)cap, "timer %s %d/s · %s%s", timer_source(), r, onebit ? "1-BIT · " : "", dev);
    else snfmt(out, (size_t)cap, "timer %s, measuring · %s%s", timer_source(), onebit ? "1-BIT · " : "", dev);
}

void midi_hw_poll(void);
static void audio_tick(void) {
    uint64_t t0 = rdtsc();
    midi_hw_poll();
    if (sound) sound->pump(false);
    else if (!held) audio_render(scratch, audio_rate() / 1000);   /* no PCM device: keep the sequencer, envelopes and scope moving */
    if (!sound) pcspk_tone(held ? 0 : synth_mono_freq());          /* the real PC speaker, where it is all there is */
    busy_tsc += rdtsc() - t0;
}

void platform_audio_init(void) {
    if (sound && timer_polled()) sound->long_lead();                  /* the main loop pumps: keep more audio ahead */
    pit_set_tick_hook(audio_tick);
}
