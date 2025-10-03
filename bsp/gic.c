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

/* GICv3 SGI base (redistributor SGI frame) */
#define GICR_SGI_BASE       0x080B0000u
#define GICR_IGROUPR0       (GICR_SGI_BASE + 0x0080u)
#define GICR_IGRPMODR0      (GICR_SGI_BASE + 0x0D00u)
#define GICR_ISENABLER0     (GICR_SGI_BASE + 0x0100u)
#define GICR_ICENABLER0     (GICR_SGI_BASE + 0x0180u)
#define GICR_ICFGR0         (GICR_SGI_BASE + 0x0C00u)
#define GICR_IPRIORITYR(n)  (GICR_SGI_BASE + 0x0400u + ((n) * 4u))

void gic_init(void)
{
    uart_puts("[GIC] Starting armv8-style GIC initialization\n");
    
    /* Test distributor access */
    uint32_t gicd_ctrl = mmio_read32(GICD_CTLR);
    uart_puts("[GIC] GICD_CTLR = ");
    uart_write_hex(gicd_ctrl);
    uart_putc('\n');
    
    /* Follow armv8 project sequence: gic_dist_init() then gic_cpu_init() */
    uart_puts("[GIC] Phase 1: Distributor initialization\n");
    
    /* Disable distributor first */
    mmio_write32(GICD_CTLR, 0u);
    
    /* Enable distributor with proper flags like armv8 */
    mmio_write32(GICD_CTLR, 0x37u);  /* ARE_S | ARE_NS | Enable_G1S | Enable_G1NS | Enable_G0 */
    
    uart_puts("[GIC] Phase 2: CPU interface initialization\n");
    
    /* Configure redistributor like armv8 */
    uart_puts("[GIC] Configuring redistributor for timer interrupt\n");
    
    /* Set all PPIs/SGIs to Group 1 like armv8 */
    mmio_write32(GICR_SGI_BASE + 0x0080u, 0xFFFFFFFFu);  /* GICR_IGROUPR0 */
    
    /* Configure timer interrupt 27 specifically */
    uint32_t timer_id = 27u;
    
    /* Set priority for interrupt 27 */
    uint32_t priority_reg = GICR_SGI_BASE + 0x0400u + (timer_id / 4u) * 4u;
    uint32_t priority = mmio_read32(priority_reg);
    uint32_t shift = (timer_id % 4u) * 8u;
    priority &= ~(0xFFu << shift);
    priority |= (0x80u << shift);  /* Medium priority */
    mmio_write32(priority_reg, priority);
    
    /* Enable interrupt 27 */
    mmio_write32(GICR_SGI_BASE + 0x0100u, (1u << timer_id));  /* GICR_ISENABLER0 */
    
    /* Configure as edge-triggered */
    uint32_t cfg_reg = GICR_SGI_BASE + 0x0C00u + 4u;  /* GICR_ICFGR1 for interrupts 16-31 */
    uint32_t cfg = mmio_read32(cfg_reg);
    uint32_t cfg_shift = ((timer_id - 16u) * 2u);
    cfg |= (0x2u << cfg_shift);  /* Edge-triggered */
    mmio_write32(cfg_reg, cfg);
    
    uart_puts("[GIC] Timer interrupt 27 configured: priority, enabled, edge-triggered\n");
    
    uart_puts("[GIC] armv8-style GIC initialization completed\n");
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