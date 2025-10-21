#include "cache.h"

#include <stdint.h>

#define CACHE_LINE_SIZE 64u

static inline uintptr_t align_down(uintptr_t addr)
{
    return addr & ~(uintptr_t)(CACHE_LINE_SIZE - 1u);
}

static inline uintptr_t align_up(uintptr_t addr)
{
    return (addr + (CACHE_LINE_SIZE - 1u)) & ~(uintptr_t)(CACHE_LINE_SIZE - 1u);
}

static void cache_op_range(uintptr_t start, uintptr_t end, void (*op)(uintptr_t))
{
    for (uintptr_t addr = start; addr < end; addr += CACHE_LINE_SIZE) {
        op(addr);
    }
}

static void dc_cvac(uintptr_t addr)
{
    __asm__ volatile("dc cvac, %0" :: "r"(addr) : "memory");
}

static void dc_ivac(uintptr_t addr)
{
    __asm__ volatile("dc ivac, %0" :: "r"(addr) : "memory");
}

static void dc_civac(uintptr_t addr)
{
    __asm__ volatile("dc civac, %0" :: "r"(addr) : "memory");
}

void cache_clean_range(const void *addr, size_t size)
{
    if (size == 0u) {
        return;
    }

    uintptr_t start = align_down((uintptr_t)addr);
    uintptr_t end = align_up((uintptr_t)addr + size);

    cache_op_range(start, end, dc_cvac);

    __asm__ volatile("dsb ish" ::: "memory");
}

void cache_invalidate_range(void *addr, size_t size)
{
    if (size == 0u) {
        return;
    }

    uintptr_t start = align_down((uintptr_t)addr);
    uintptr_t end = align_up((uintptr_t)addr + size);

    cache_op_range(start, end, dc_ivac);

    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

void cache_clean_invalidate_range(void *addr, size_t size)
{
    if (size == 0u) {
        return;
    }

    uintptr_t start = align_down((uintptr_t)addr);
    uintptr_t end = align_up((uintptr_t)addr + size);

    cache_op_range(start, end, dc_civac);

    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}
