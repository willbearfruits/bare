#include "mem.h"
#include "arch.h"
#include "io.h"
#include "log.h"

static uint64_t hhdm_off;

uint64_t hhdm(void) { return hhdm_off; }
void *phys_to_virt(uint64_t p) { return (void *)(p + hhdm_off); }
uint64_t virt_to_phys(const void *v) { return (uint64_t)v - hhdm_off; }

void mem_init_x86_64(struct limine_memmap_response *mm, uint64_t off) {
    hhdm_off = off;
    uint64_t best = 0, best_len = 0;
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        int kind = e->type == LIMINE_MEMMAP_FRAMEBUFFER ? MAP_FRAMEBUFFER
                 : e->type == LIMINE_MEMMAP_RESERVED || e->type == LIMINE_MEMMAP_RESERVED_MAPPED ? MAP_RESERVED : MAP_RAM;
        pmm_note(e->base, e->length, kind);
        if (kind != MAP_RAM && e->base >= 0x100000) logf("memmap: %lx +%lx %s", e->base, e->length, kind == MAP_RESERVED ? "reserved" : "framebuffer");
        if (e->type != LIMINE_MEMMAP_USABLE || e->base < 0x100000) continue;
        if (e->base + e->length > 0xFFFFFFFFull) continue;     /* keep DMA buffers below 4 GiB */
        if (e->length > best_len) { best = e->base; best_len = e->length; }
    }
    pmm_set_region(best, best + best_len);
    logf("hhdm %lx", hhdm_off);
}

/* ---- paging: add uncached mappings for MMIO into the HHDM ---- */
#define PTE_P   (1ull << 0)
#define PTE_RW  (1ull << 1)
#define PTE_PWT (1ull << 3)
#define PTE_PCD (1ull << 4)
#define PTE_PS  (1ull << 7)
#define PTE_ADDR 0x000FFFFFFFFFF000ull
#define PTE_FLAGS_KEEP (PTE_P | PTE_RW | (1ull << 2) | PTE_PWT | PTE_PCD | (1ull << 5) | (1ull << 6) | (1ull << 8) | (1ull << 63))

static uint64_t *table_at(uint64_t phys) { return (uint64_t *)phys_to_virt(phys & PTE_ADDR); }

/* Return the next-level table for entry idx, creating it or splitting a huge page (level: 3=PDPT entry,2=PD entry). */
static uint64_t *descend(uint64_t *table, int idx, int level) {
    uint64_t e = table[idx];
    if (!(e & PTE_P)) {
        uint64_t p = pmm_alloc_pages(1);
        table[idx] = p | PTE_P | PTE_RW;
        return table_at(p);
    }
    if ((e & PTE_PS) && level <= 3) {
        uint64_t p = pmm_alloc_pages(1);
        uint64_t *nt = table_at(p);
        uint64_t sub = level == 3 ? (1ull << 21) : (1ull << 12);
        uint64_t flags = e & PTE_FLAGS_KEEP;
        if (level == 3) flags |= PTE_PS;
        for (int i = 0; i < 512; i++) nt[i] = ((e & PTE_ADDR) + i * sub) | flags;
        table[idx] = p | PTE_P | PTE_RW;
        return nt;
    }
    return table_at(e);
}

static void *map_pages(uint64_t phys, size_t len, uint64_t cache) {
    uint64_t start = phys & ~0xFFFull, end = (phys + len + 0xFFF) & ~0xFFFull;
    uint64_t cr3; __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    for (uint64_t p = start; p < end; p += 4096) {
        uint64_t v = p + hhdm_off;
        uint64_t *pml4 = table_at(cr3);
        uint64_t *pdpt = descend(pml4, (v >> 39) & 511, 4);
        uint64_t *pd   = descend(pdpt, (v >> 30) & 511, 3);
        uint64_t *pt   = descend(pd,   (v >> 21) & 511, 2);
        pt[(v >> 12) & 511] = p | PTE_P | PTE_RW | cache;
        __asm__ volatile("invlpg (%0)" :: "r"(v) : "memory");
    }
    return phys_to_virt(phys);
}
void *mmio_map(uint64_t phys, size_t len) { return map_pages(phys, len, PTE_PCD | PTE_PWT); }
void *phys_map(uint64_t phys, size_t len) { return map_pages(phys, len, 0); }
