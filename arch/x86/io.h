#pragma once
#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint8_t  inb(uint16_t p) { uint8_t v;  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }

static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }
#if __SIZEOF_POINTER__ == 8
static inline uint32_t read_flags(void) { uint64_t f; __asm__ volatile("pushfq; popq %0" : "=r"(f)); return (uint32_t)f; }
#else
static inline uint32_t read_flags(void) { uint32_t f; __asm__ volatile("pushfl; popl %0" : "=r"(f)); return f; }
#endif

static inline uint32_t mmio_r32(volatile void *a) { return *(volatile uint32_t *)a; }
static inline uint16_t mmio_r16(volatile void *a) { return *(volatile uint16_t *)a; }
static inline uint8_t  mmio_r8(volatile void *a)  { return *(volatile uint8_t *)a; }
static inline void mmio_w32(volatile void *a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void mmio_w16(volatile void *a, uint16_t v) { *(volatile uint16_t *)a = v; }
static inline void mmio_w8(volatile void *a, uint8_t v)   { *(volatile uint8_t *)a = v; }
static inline void barrier(void) { __asm__ volatile("" ::: "memory"); }
static inline uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
