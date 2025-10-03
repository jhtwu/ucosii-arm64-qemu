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

/* GIC CPU Interface constants from armv8 project */
#define ICC_SRE_EL1_SRE         (1U << 0)
#define ICC_CTLR_EL1_EOImode_drop_dir   (0U << 1)
#define DEFAULT_PMR_VALUE       0xF0u

/* System register access functions like armv8 */
static inline uint32_t gic_read_sre(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, S3_0_c12_c12_5" : "=r"(val)); 
    return val;
}

static inline void gic_write_sre(uint32_t val)
{
    __asm__ volatile("msr S3_0_c12_c12_5, %x0" :: "rZ"(val)); 
    __asm__ volatile("isb");
}

static inline void gic_write_pmr(uint32_t val)
{
    __asm__ volatile("msr S3_0_c4_c6_0, %x0" :: "rZ"(val)); 
}

static inline void gic_write_ctlr(uint32_t val)
{
    __asm__ volatile("msr S3_0_c12_c12_4, %x0" :: "rZ"(val)); 
    __asm__ volatile("isb");
}

static inline void gic_write_grpen1(uint32_t val)
{
    __asm__ volatile("msr S3_0_c12_c12_7, %x0" :: "rZ"(val)); 
    __asm__ volatile("isb");
}

static inline void gic_write_bpr1(uint32_t val)
{
    __asm__ volatile("msr S3_0_c12_c12_3, %x0" :: "rZ"(val)); 
}

static inline int gic_enable_sre(void)
{
    uint32_t val;

    uart_puts("[GIC] Enabling system register interface\n");

    val = gic_read_sre();
    if (val & ICC_SRE_EL1_SRE) {
        uart_puts("[GIC] SRE already enabled\n");
        return 1;
    }

    uart_puts("[GIC] Setting SRE bit\n");
    val |= ICC_SRE_EL1_SRE;
    gic_write_sre(val);
    val = gic_read_sre();

    uart_puts("[GIC] SRE enable result: ");
    uart_write_hex(val);
    uart_putc('\n');
    return !!(val & ICC_SRE_EL1_SRE);
}

static void gic_cpu_sys_reg_init(void)
{
    uart_puts("[GIC] CPU system register initialization\n");
    
    /* Enable system register interface */
    if (!gic_enable_sre()) {
        uart_puts("[GIC] ERROR: Unable to set SRE (disabled at EL2)\n");
        return;
    }

    /* Set priority mask register */
    uart_puts("[GIC] Setting priority mask\n");
    gic_write_pmr(DEFAULT_PMR_VALUE);

    /* Set binary point register */
    uart_puts("[GIC] Setting binary point register\n");
    gic_write_bpr1(0);

    /* Set control register - EOI deactivates interrupt too */
    uart_puts("[GIC] Setting control register\n");
    gic_write_ctlr(ICC_CTLR_EL1_EOImode_drop_dir);

    /* Enable Group 1 interrupts */
    uart_puts("[GIC] Enabling Group 1 interrupts\n");
    gic_write_grpen1(1);
    
    uart_puts("[GIC] CPU interface system registers configured\n");
}

void gic_init(void)
{
    uart_puts("[GIC] Starting complete armv8-style GIC initialization\n");
    
    /* Test distributor access */
    uint32_t gicd_ctrl = mmio_read32(GICD_CTLR);
    uart_puts("[GIC] GICD_CTLR = ");
    uart_write_hex(gicd_ctrl);
    uart_putc('\n');
    
    /* Phase 1: Distributor initialization */
    uart_puts("[GIC] Phase 1: Distributor initialization\n");
    mmio_write32(GICD_CTLR, 0u);
    mmio_write32(GICD_CTLR, GICD_CTLR_ENABLE);
    
    /* Phase 2: Redistributor initialization */
    uart_puts("[GIC] Phase 2: Redistributor initialization\n");
    
    /* Set all PPIs/SGIs to Group 1 */
    mmio_write32(GICR_IGROUPR0, 0xFFFFFFFFu);
    
    /* Configure timer interrupt 27 */
    uint32_t timer_id = 27u;
    
    /* Set priority */
    uint32_t priority_reg = GICR_IPRIORITYR(timer_id / 4u);
    uint32_t priority = mmio_read32(priority_reg);
    uint32_t shift = (timer_id % 4u) * 8u;
    priority &= ~(0xFFu << shift);
    priority |= (0x80u << shift);
    mmio_write32(priority_reg, priority);
    
    /* Enable interrupt */
    mmio_write32(GICR_ISENABLER0, (1u << timer_id));
    
    /* Configure as edge-triggered */
    uint32_t cfg_reg = GICR_ICFGR0 + 4u;
    uint32_t cfg = mmio_read32(cfg_reg);
    uint32_t cfg_shift = ((timer_id - 16u) * 2u);
    cfg |= (0x2u << cfg_shift);
    mmio_write32(cfg_reg, cfg);
    
    uart_puts("[GIC] Timer interrupt 27 configured\n");
    
    /* Phase 3: CPU interface system register initialization */
    uart_puts("[GIC] Phase 3: CPU interface initialization\n");
    gic_cpu_sys_reg_init();
    
    uart_puts("[GIC] Complete GICv3 initialization finished\n");
}

uint32_t gic_acknowledge(void)
{
    uint64_t int_id;
    __asm__ volatile("mrs %0, S3_0_c12_c12_0" : "=r"(int_id));  /* ICC_IAR1_EL1 */
    return (uint32_t)int_id;
}

void gic_end_interrupt(uint32_t int_id)
{
    __asm__ volatile("msr S3_0_c12_c12_1, %x0" :: "rZ"((uint64_t)int_id));  /* ICC_EOIR1_EL1 */
    __asm__ volatile("isb");
    __asm__ volatile("msr S3_0_c12_c11_1, %x0" :: "rZ"((uint64_t)int_id));  /* ICC_DIR_EL1 */
    __asm__ volatile("isb");
}