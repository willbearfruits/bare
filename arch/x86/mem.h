#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* Physical memory + device mapping. Each CPU layer implements the mapping functions; the bump allocator is shared. */
void      pmm_set_region(uint64_t base, uint64_t end);   /* one usable RAM range, 4K aligned, below 4 GiB */
uint64_t  pmm_alloc_pages(size_t n);                     /* physical, zeroed, 4K aligned */
uint64_t  pmm_avail(void);                               /* bytes left in the region */
enum { MAP_RAM, MAP_FRAMEBUFFER, MAP_RESERVED };
void      pmm_note(uint64_t base, uint64_t len, int kind);   /* each entry of the boot memory map */
bool      pmm_claimed(uint64_t base, uint64_t len, uint64_t *start);   /* overlaps one (start: where that one begins) */
uint64_t  pmm_ram_top32(void);                           /* where its RAM below 4 GiB ends */
void     *dma_alloc(size_t bytes, uint64_t *phys_out);
void     *phys_to_virt(uint64_t phys);
uint64_t  virt_to_phys(const void *v);
void     *mmio_map(uint64_t phys, size_t len);           /* uncached mapping of device registers */
void     *phys_map(uint64_t phys, size_t len);           /* cached mapping of memory outside the allocator (ACPI tables) */
