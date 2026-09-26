#include "wave.h"
#include "libc.h"
#include "platform.h"
#include "tables.h"

struct wave_slot wave_bank[WAVE_SLOTS];
int16_t wave_scan_tab[WAVE_LEN];
int16_t wave_rom_tab[WAVE_LEN];
const char *const wave_rom_names[ROM_SOURCES] = { "BIOS ROM", "FLASH", "KERNEL", "LOW MEM" };
int wave_rom_src = ROM_BIOS;
uint32_t wave_rom_off = 0;

static uint32_t rng = 0x9E3779B9u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

void wave_from_harmonics(int slot) {
    struct wave_slot *w = &wave_bank[slot];
    int32_t acc[WAVE_LEN]; memset(acc, 0, sizeof acc);
    int32_t peak = 1;
    for (int h = 0; h < WAVE_HARMS; h++) {
        if (!w->harm[h]) continue;
        for (int i = 0; i < WAVE_LEN; i++) acc[i] += sine_q15[(i * (h + 1)) & 255] * w->harm[h];
    }
    for (int i = 0; i < WAVE_LEN; i++) { int32_t a = acc[i] < 0 ? -acc[i] : acc[i]; if (a > peak) peak = a; }
    for (int i = 0; i < WAVE_LEN; i++) w->tab[i] = (int16_t)((int64_t)acc[i] * 32000 / peak);
}

void wave_smooth(int slot) {
    int16_t *t = wave_bank[slot].tab, out[WAVE_LEN];
    for (int i = 0; i < WAVE_LEN; i++) out[i] = (int16_t)((t[(i - 1) & 255] + 2 * t[i] + t[(i + 1) & 255]) / 4);
    memcpy(t, out, sizeof out);
}
void wave_normalize(int slot) {
    int16_t *t = wave_bank[slot].tab; int32_t peak = 1;
    for (int i = 0; i < WAVE_LEN; i++) { int32_t a = t[i] < 0 ? -t[i] : t[i]; if (a > peak) peak = a; }
    for (int i = 0; i < WAVE_LEN; i++) t[i] = (int16_t)((int32_t)t[i] * 32000 / peak);
}
void wave_invert(int slot) { int16_t *t = wave_bank[slot].tab; for (int i = 0; i < WAVE_LEN; i++) t[i] = (int16_t)-t[i]; }
void wave_random(int slot) {
    struct wave_slot *w = &wave_bank[slot];
    for (int h = 0; h < WAVE_HARMS; h++) w->harm[h] = (rnd() % 4 == 0) ? (uint8_t)(rnd() % 100) : 0;
    w->harm[0] = 100;
    wave_from_harmonics(slot);
}
void wave_clear(int slot) { memset(&wave_bank[slot], 0, sizeof wave_bank[slot]); }
void wave_copy(int from, int to) { wave_bank[to] = wave_bank[from]; }

void wave_draw(int slot, int x, int16_t v, int prev_x, int16_t prev_v) {
    int16_t *t = wave_bank[slot].tab;
    if (prev_x < 0 || prev_x == x) { t[x & 255] = v; return; }
    int dx = x - prev_x, n = dx < 0 ? -dx : dx;
    for (int i = 0; i <= n; i++) {
        int px = prev_x + (dx < 0 ? -i : i);
        t[px & 255] = (int16_t)(prev_v + ((int32_t)(v - prev_v) * i) / n);
    }
}

void wave_rom_refresh(void) {
    uint8_t buf[WAVE_LEN];
    plat_rom_read(wave_rom_src, wave_rom_off, buf, WAVE_LEN);
    for (int i = 0; i < WAVE_LEN; i++) wave_rom_tab[i] = (int16_t)(((int32_t)buf[i] - 128) * 256);
}

static void preset_harm(int slot, const uint8_t *h, int n) {
    wave_clear(slot);
    for (int i = 0; i < n; i++) wave_bank[slot].harm[i] = h[i];
    wave_from_harmonics(slot);
}

void wave_init(void) {
    static const uint8_t sine[] = { 100 };
    static const uint8_t tri[] = { 100, 0, 11, 0, 4, 0, 2, 0, 1 };
    static const uint8_t saw[] = { 100, 50, 33, 25, 20, 17, 14, 12, 11, 10, 9, 8, 8, 7, 7, 6 };
    static const uint8_t square[] = { 100, 0, 33, 0, 20, 0, 14, 0, 11, 0, 9, 0, 8, 0, 7, 0 };
    static const uint8_t organ[] = { 100, 60, 40, 30, 0, 20, 0, 15 };
    static const uint8_t vox[] = { 60, 100, 30, 10, 40, 5, 0, 8 };
    static const uint8_t bell[] = { 100, 0, 0, 0, 40, 0, 0, 0, 0, 0, 25, 0, 0, 0, 0, 0, 0, 0, 12 };
    preset_harm(0, sine, sizeof sine);
    preset_harm(1, tri, sizeof tri);
    preset_harm(2, saw, sizeof saw);
    preset_harm(3, square, sizeof square);
    preset_harm(4, organ, sizeof organ);
    preset_harm(5, vox, sizeof vox);
    preset_harm(6, bell, sizeof bell);
    wave_random(7);
    memset(wave_scan_tab, 0, sizeof wave_scan_tab);
    wave_rom_refresh();
}
