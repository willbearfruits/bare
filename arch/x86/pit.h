#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef void (*tick_hook_t)(void);
void     timer_start(uint32_t hz);  /* 8254, else HPET, else TSC time with the main loop polling (logs which); enables interrupts */
bool     timer_polled(void);        /* no timer interrupts: call timer_poll from the main loop */
void     timer_poll(void);          /* runs the tick hook for every ms that passed (polled mode only) */
const char *timer_source(void);     /* "8254", "HPET" or "TSC, polled" */
bool     pit_restore(void);          /* after firmware ran: put the 8254 back if it was changed; true if it was */
uint64_t pit_ticks(void);           /* ms since boot when hz == 1000 */
uint64_t timer_us(void);            /* µs since boot: the ticks and the cycle counter between them */
void     pit_set_tick_hook(tick_hook_t h);   /* runs in IRQ0 context every tick */
void     sleep_ms(uint32_t ms);
