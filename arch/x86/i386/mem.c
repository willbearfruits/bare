/* i386: no paging — everything is identity mapped; MMIO caching is governed by the firmware's MTRRs. */
#include "mem.h"
void *phys_to_virt(uint64_t p) { return (void *)(uintptr_t)p; }
uint64_t virt_to_phys(const void *v) { return (uintptr_t)v; }
void *mmio_map(uint64_t phys, size_t len) { (void)len; return (void *)(uintptr_t)phys; }
void *phys_map(uint64_t phys, size_t len) { (void)len; return (void *)(uintptr_t)phys; }
