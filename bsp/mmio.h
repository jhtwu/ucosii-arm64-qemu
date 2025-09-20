#ifndef BSP_MMIO_H
#define BSP_MMIO_H

#include <stdint.h>

/*
 * Chinese: 向指定的記憶體映射 I/O 位址寫入一個 32 位元的值。
 * English: Writes a 32-bit value to the specified memory-mapped I/O address.
 */
static inline void mmio_write32(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

/*
 * Chinese: 從指定的記憶體映射 I/O 位址讀取一個 32 位元的值。
 * English: Reads a 32-bit value from the specified memory-mapped I/O address.
 */
static inline uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

/*
 * Chinese: 向指定的記憶體映射 I/O 位址寫入一個 64 位元的值。
 * English: Writes a 64-bit value to the specified memory-mapped I/O address.
 */
static inline void mmio_write64(uintptr_t addr, uint64_t value)
{
    *(volatile uint64_t *)addr = value;
}

/*
 * Chinese: 從指定的記憶體映射 I/O 位址讀取一個 64 位元的值。
 * English: Reads a 64-bit value from the specified memory-mapped I/O address.
 */
static inline uint64_t mmio_read64(uintptr_t addr)
{
    return *(volatile uint64_t *)addr;
}

#endif