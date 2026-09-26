/* The 1 kHz tick behind plat_ms, the drivers' timeouts and the audio pump. IRQ 0 comes from the 8254, or — where
   firmware switched the 8254 off, as many UEFI-only laptops do — from the HPET in legacy-replacement mode, which drives
   the same IRQ 0. With neither interrupting, time comes from the TSC and the main loop runs the tick hook (timer_poll). */
#include "pit.h"
#include "hpet.h"
#include "io.h"
#include "cpu.h"
#include "log.h"
#include "rtc.h"

static volatile uint64_t ticks;
static tick_hook_t hook;
static uint16_t divisor;
static enum { SRC_8254, SRC_HPET, SRC_TSC } src;
static uint64_t tsc0, tsc_per_ms, polled_ms;

static uint64_t tsc_tick, tsc_per_tick;                 /* the cycle counter at the last tick, and per tick (smoothed) */
static void pit_irq(void) {
    uint64_t t = rdtsc();
    if (tsc_tick && t > tsc_tick) { uint64_t d = t - tsc_tick; tsc_per_tick = tsc_per_tick ? tsc_per_tick - (tsc_per_tick >> 4) + (d >> 4) : d; }
    tsc_tick = t; ticks++;
    if (hook) hook();
}

static void program_8254(void) { outb(0x43, 0x34); outb(0x40, divisor & 0xFF); outb(0x40, divisor >> 8); }   /* ch 0, mode 2 */

static bool ticking(void) {                                         /* three ticks within ~0.2 s of cycles */
    uint64_t t = ticks;
    for (uint64_t c0 = rdtsc(); rdtsc() - c0 < 500000000ull;) if (ticks >= t + 3) return true;
    return false;
}

static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

/* TSC counts per ms: from CPUID where the CPU says (Intel since Skylake — the machines that switch the 8254 off),
   else counted over one second of the CMOS clock */
static uint64_t tsc_rate(void) {
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    uint32_t max = a;
    if (max >= 0x15) { cpuid(0x15, &a, &b, &c, &d); if (a && b && c) return (uint64_t)c * b / a / 1000; }
    if (max >= 0x16) { cpuid(0x16, &a, &b, &c, &d); if (a & 0xFFFF) return (uint64_t)(a & 0xFFFF) * 1000; }
    struct rtc_time t; const uint64_t give_up = 20000000000ull;      /* a stuck clock: ~5-10 s, then assume 2 GHz */
    if (!rtc_read(&t)) return 2000000;
    int s0 = t.sec; uint64_t start = rdtsc();
    while (rtc_read(&t) && t.sec == s0 && rdtsc() - start < give_up) ;
    uint64_t c0 = rdtsc(); s0 = t.sec;
    while (rtc_read(&t) && t.sec == s0 && rdtsc() - start < give_up) ;
    uint64_t r = (rdtsc() - c0) / 1000;
    return r > 100000 ? r : 2000000;
}

void timer_start(uint32_t hz) {
    divisor = (uint16_t)(1193182 / hz);
    program_8254();
    irq_install(0, pit_irq);
    sti();
    if (ticking()) { logf("timer: 8254 at %u Hz", hz); return; }
    logf("timer: the 8254 doesn't tick");
    if (hpet_start_legacy(hz) && ticking()) { src = SRC_HPET; logf("timer: HPET on IRQ 0 at %u Hz", hz); return; }
    src = SRC_TSC; tsc_per_ms = tsc_rate(); tsc0 = rdtsc();
    logf("timer: no timer interrupts; time from the TSC (%lu per ms), audio from the main loop", tsc_per_ms);
}

bool timer_polled(void) { return src == SRC_TSC; }
const char *timer_source(void) { return src == SRC_8254 ? "8254" : src == SRC_HPET ? "HPET" : "TSC, polled"; }

void timer_poll(void) {
    static bool busy;                                               /* the hook may ask for the time itself */
    if (src != SRC_TSC || busy) return;
    busy = true;
    uint64_t now = pit_ticks();
    if (now - polled_ms > 100) polled_ms = now - 1;                 /* after a long stall, don't replay every tick */
    while (polled_ms < now) { polled_ms++; if (hook) hook(); }
    busy = false;
}

bool pit_restore(void) {
    if (src != SRC_8254) return false;
    outb(0x43, 0xE2);                                 /* read-back: channel 0's status byte */
    uint8_t mode = (inb(0x40) >> 1) & 3;              /* modes 2 and 6 are the same */
    outb(0x43, 0x00);                                 /* latch channel 0's count */
    uint16_t count = inb(0x40); count |= (uint16_t)(inb(0x40) << 8);
    if (mode == 2 && count <= divisor) return false;
    program_8254();
    return true;
}
uint64_t pit_ticks(void) { return src == SRC_TSC ? (rdtsc() - tsc0) / tsc_per_ms : ticks; }
/* microseconds: the ticks, and how far into the next one the cycle counter says we are */
uint64_t timer_us(void) {
    if (src == SRC_TSC) return (rdtsc() - tsc0) * 1000 / tsc_per_ms;
    uint32_t f = read_flags(); cli();
    uint64_t t = ticks, c = tsc_tick, per = tsc_per_tick, now = rdtsc();
    if (f & 0x200) sti();
    uint64_t frac = per && now > c ? (now - c) * 1000 / per : 0;
    return t * 1000 + (frac < 1000 ? frac : 999);
}
void pit_set_tick_hook(tick_hook_t h) { hook = h; }
void sleep_ms(uint32_t ms) {
    uint64_t end = pit_ticks() + ms;
    while (pit_ticks() < end) { if (src == SRC_TSC) { timer_poll(); __asm__ volatile("pause"); } else hlt(); }
}
