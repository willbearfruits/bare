#include "mem.h"
#include "io.h"
#include "log.h"

static uint64_t free_base, free_end;

void pmm_set_region(uint64_t base, uint64_t end) {
    free_base = (base + 0xFFF) & ~0xFFFull; free_end = end & ~0xFFFull;
    logf("pmm: %lx..%lx (%lu MiB)", free_base, free_end, (free_end - free_base) >> 20);
}
/* The boot memory map, for putting device registers where nothing is. Reserved ranges of 256 KiB or more are left out:
   some firmware marks the whole PCI window reserved (Linux ignores those too, efi_remove_e820_mmio). */
#define MAPS 256
static struct { uint64_t base, len; bool ram; } maps[MAPS];
static int nmaps;
void pmm_note(uint64_t base, uint64_t len, int kind) {
    if (!len || (kind == MAP_RESERVED && len >= (256u << 10))) return;
    bool ram = kind == MAP_RAM;
    if (nmaps && maps[nmaps - 1].ram == ram && maps[nmaps - 1].base + maps[nmaps - 1].len == base) { maps[nmaps - 1].len += len; return; }
    if (nmaps < MAPS) { maps[nmaps].base = base; maps[nmaps].len = len; maps[nmaps].ram = ram; nmaps++; }
    else logf("pmm: memory map too long, %lx +%lx not kept", base, len);
}
bool pmm_claimed(uint64_t base, uint64_t len, uint64_t *start) {
    for (int i = 0; i < nmaps; i++)
        if (maps[i].base < base + len && base < maps[i].base + maps[i].len) { *start = maps[i].base; return true; }
    return false;
}
uint64_t pmm_ram_top32(void) {
    uint64_t top = 0;
    for (int i = 0; i < nmaps; i++) {
        uint64_t e = maps[i].base + maps[i].len;
        if (maps[i].ram && maps[i].base < 0x100000000ull) top = MAX(top, MIN(e, 0x100000000ull));
    }
    return top;
}
uint64_t pmm_avail(void) { return free_end > free_base ? free_end - free_base : 0; }
uint64_t pmm_alloc_pages(size_t n) {
    uint64_t p = free_base;
    if (p + n * 4096 > free_end) { logf("pmm: out of memory"); for (;;) hlt(); }
    free_base += n * 4096;
    memset(phys_to_virt(p), 0, n * 4096);
    return p;
}
void *dma_alloc(size_t bytes, uint64_t *phys_out) {
    uint64_t p = pmm_alloc_pages((bytes + 4095) / 4096);
    if (phys_out) *phys_out = p;
    return phys_to_virt(p);
}
