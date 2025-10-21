#include "mmu.h"

#include <stdint.h>

#define L1_BLOCK_SIZE_SHIFT 30u
#define L1_ENTRY_COUNT      4u

extern uint64_t mmu_table_start[];

static void write_mair(uint64_t value)
{
    __asm__ volatile("msr mair_el1, %0" :: "r"(value));
}

static void write_tcr(uint64_t value)
{
    __asm__ volatile("msr tcr_el1, %0" :: "r"(value));
}

static void write_ttbr0(uint64_t value)
{
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(value));
}

static void enable_mmu_and_caches(void)
{
    uint64_t sctlr;

    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0);   /* MMU enable */
    sctlr |= (1ULL << 2);   /* D-cache enable */
    sctlr |= (1ULL << 12);  /* I-cache enable */
    sctlr &= ~(1ULL << 25); /* Clear EE bit */
    sctlr &= ~(1ULL << 4);  /* Clear SA bit */

    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

void mmu_init(void)
{
    uint64_t *level1_table = (uint64_t *)&mmu_table_start;

    /* Clear table */
    for (uint32_t i = 0; i < 512u; ++i) {
        level1_table[i] = 0u;
    }

    /* Attr0: Normal memory (Write-back, Read/Write allocate) */
    /* Attr1: Device-nGnRnE */
    uint64_t mair = (0xFFULL << 0) | (0x00ULL << 8);
    write_mair(mair);

    for (uint32_t i = 0; i < L1_ENTRY_COUNT; ++i) {
        uint64_t addr = ((uint64_t)i) << L1_BLOCK_SIZE_SHIFT;
        uint64_t attr_idx = (i == 0u) ? 1u : 0u;

        uint64_t sh = (attr_idx == 0u) ? (3ULL << 8) : (0ULL << 8);  /* Inner shareable for normal memory */

        uint64_t block = addr |
                         (1ULL << 0) |        /* Valid */
                         (0ULL << 1) |        /* Block descriptor */
                         (attr_idx << 2) |    /* Attr index */
                         (0ULL << 6) |        /* AP[2:1] = RW EL1 */
                         sh |
                         (1ULL << 10);        /* Access flag */

        level1_table[i] = block;
    }

    /* Configure TCR */
    uint64_t tcr = (32ULL << 0) |  /* T0SZ: 4GB VA */
                   (1ULL << 8) |   /* IRGN0: Write-back */
                   (1ULL << 10) |  /* ORGN0: Write-back */
                   (3ULL << 12) |  /* SH0: Inner shareable */
                   (0ULL << 14) |  /* TG0: 4KB */
                   (1ULL << 32);   /* IPS: 36-bit PA */
    write_tcr(tcr);

    /* Set translation table base */
    uint64_t ttbr0 = (uint64_t)(uintptr_t)level1_table;
    write_ttbr0(ttbr0);

    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    /* Invalidate TLB before enabling MMU */
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    enable_mmu_and_caches();
}
