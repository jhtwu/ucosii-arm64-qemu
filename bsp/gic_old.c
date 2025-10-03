#include "gic.h"
#include "uart.h"
#include "mmio.h"

#define TIMER_INTERRUPT_ID  27u

/* GICv3 Distributor */
#define GICD_BASE           0x08000000u
#define GICD_CTLR           (GICD_BASE + 0x0000u)
#define GICD_CTLR_ENABLE    0x37u

/* GICv3 Redistributor */
#define GICR_BASE           0x080A0000u
#define GICR_CTLR           (GICR_BASE + 0x0000u)
#define GICR_WAKER          (GICR_BASE + 0x0014u)

/* GICv3 SGI base (redistributor SGI frame) */
#define GICR_SGI_BASE       0x080B0000u
#define GICR_IGROUPR0       (GICR_SGI_BASE + 0x0080u)
#define GICR_IGRPMODR0      (GICR_SGI_BASE + 0x0D00u)
#define GICR_ISENABLER0     (GICR_SGI_BASE + 0x0100u)
#define GICR_ICENABLER0     (GICR_SGI_BASE + 0x0180u)
#define GICR_ISPENDR0       (GICR_SGI_BASE + 0x0200u)
#define GICR_ICFGR0         (GICR_SGI_BASE + 0x0C00u)

#define GICR_IPRIORITYR(n)  (GICR_SGI_BASE + 0x0400u + ((n) * 4u))

void gic_init(void)
{
    uart_puts("[GIC] Starting clean GIC initialization\n");
    
    /* Test basic MMIO access */
    uart_puts("[GIC] Testing distributor access\n");
    uint32_t gicd_ctrl = mmio_read32(GICD_CTLR);
    uart_puts("[GIC] GICD_CTLR = ");
    uart_write_hex(gicd_ctrl);
    uart_putc('\n');
    
    /* Disable distributor */
    mmio_write32(GICD_CTLR, 0u);
    
    /* Skip redistributor CTLR test for now */
    uart_puts("[GIC] Skipping redistributor CTLR test - using direct SGI access\n");
    
    /* Test SGI base access directly */
    uart_puts("[GIC] Testing SGI base access\n");
    group = mmio_read32(GICR_IGROUPR0);
    uart_puts("[GIC] GICR_IGROUPR0 = ");
    uart_write_hex(group);
    uart_putc('\n');

    uart_puts("[GIC] Configuring timer interrupt (ID 27)\n");
    
    /* Configure interrupt 27 priority */
    uint32_t priority = mmio_read32(GICR_IPRIORITYR(TIMER_INTERRUPT_ID / 4u));
    uint32_t shift = (TIMER_INTERRUPT_ID % 4u) * 8u;
    priority &= ~(0xFFu << shift);
    priority |= (0x80u << shift);  /* Medium priority */
    mmio_write32(GICR_IPRIORITYR(TIMER_INTERRUPT_ID / 4u), priority);

    /* Configure as Group 1 */
    uint32_t group = mmio_read32(GICR_IGROUPR0);
    group |= (1u << TIMER_INTERRUPT_ID);
    mmio_write32(GICR_IGROUPR0, group);

    /* Clear group modifier */
    uint32_t group_mod = mmio_read32(GICR_IGRPMODR0);
    group_mod &= ~(1u << TIMER_INTERRUPT_ID);
    mmio_write32(GICR_IGRPMODR0, group_mod);

    /* Enable the interrupt */
    mmio_write32(GICR_ICENABLER0, (1u << TIMER_INTERRUPT_ID));  /* First clear */
    mmio_write32(GICR_ISENABLER0, (1u << TIMER_INTERRUPT_ID));  /* Then enable */

    /* Configure as edge-triggered for timer interrupt 27 */
    uint32_t cfg = mmio_read32(GICR_ICFGR0 + 4);  /* ICFGR1 for interrupts 16-31 */
    uint32_t cfg_shift = ((TIMER_INTERRUPT_ID - 16) * 2);
    cfg &= ~(0x3u << cfg_shift);
    cfg |= (0x2u << cfg_shift);  /* Edge-triggered */
    mmio_write32(GICR_ICFGR0 + 4, cfg);
    
    uart_puts("[GIC] Enabling distributor\n");
    mmio_write32(GICD_CTLR, GICD_CTLR_ENABLE);
    
    uart_puts("[GIC] GIC initialization completed\n");
}

uint32_t gic_acknowledge(void)
{
    uint64_t int_id;
    __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(int_id));
    return (uint32_t)int_id;
}

void gic_end_interrupt(uint32_t int_id)
{
    uint64_t value = (uint64_t)int_id;
    __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"(value));
    __asm__ volatile("msr ICC_DIR_EL1, %0" :: "r"(value));
}