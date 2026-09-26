#pragma once
/* Wave bank (drawn / additive), plus the two "data-bent" tables: SCAN (the screen) and ROM (the firmware). */
#include <stdint.h>
#include <stdbool.h>

#define WAVE_SLOTS 8
#define WAVE_LEN   256
#define WAVE_HARMS 32

struct wave_slot { int16_t tab[WAVE_LEN]; uint8_t harm[WAVE_HARMS]; /* 0..100 */ };

extern struct wave_slot wave_bank[WAVE_SLOTS];
extern int16_t wave_scan_tab[WAVE_LEN];
extern int16_t wave_rom_tab[WAVE_LEN];

void wave_init(void);
void wave_from_harmonics(int slot);
void wave_smooth(int slot);
void wave_normalize(int slot);
void wave_invert(int slot);
void wave_random(int slot);
void wave_clear(int slot);
void wave_copy(int from, int to);
/* set one sample of the drawn wave, interpolating from the previous pen position */
void wave_draw(int slot, int x, int16_t v, int prev_x, int16_t prev_v);

/* ROM oscillator: which memory and where in it */
enum { ROM_BIOS = 0, ROM_FLASH, ROM_KERNEL, ROM_LOWMEM, ROM_SOURCES };
extern const char *const wave_rom_names[ROM_SOURCES];
extern int      wave_rom_src;
extern uint32_t wave_rom_off;
void wave_rom_refresh(void);       /* rebuild wave_rom_tab from the current source/offset */
