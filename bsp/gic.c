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
#define ICC_CTLR_EL1_EOImode_split      (1U << 1)   /* Split EOI: EOIR=priority-drop, DIR=deactivate */
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

    val = gic_read_sre();
    if (val & ICC_SRE_EL1_SRE) {
        return 1;
    }

    val |= ICC_SRE_EL1_SRE;
    gic_write_sre(val);
    val = gic_read_sre();

    return !!(val & ICC_SRE_EL1_SRE);
}

static void gic_cpu_sys_reg_init(void)
{
    /* Enable system register interface */
    if (!gic_enable_sre()) {
        uart_puts("[GIC] ERROR: Unable to set SRE (disabled at EL2)\n");
        return;
    }

    /* Set priority mask register */
    gic_write_pmr(DEFAULT_PMR_VALUE);

    /* Set binary point register */
    gic_write_bpr1(0);

    /* Set control register - EOImode=0: EOIR does full priority drop + deactivation */
    gic_write_ctlr(0u);

    /* Enable Group 1 interrupts */
    gic_write_grpen1(1);
}

void gic_init(void)
{
    /* Phase 1: Distributor initialization */
    mmio_write32(GICD_CTLR, 0u);
    mmio_write32(GICD_CTLR, GICD_CTLR_ENABLE);

    /* Phase 2: Redistributor initialization */

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

    /* Phase 3: CPU interface system register initialization */
    gic_cpu_sys_reg_init();
}

uint32_t gic_acknowledge(void)
{
    uint64_t int_id;
    __asm__ volatile("mrs %0, S3_0_c12_c12_0" : "=r"(int_id));  /* ICC_IAR1_EL1 */
    return (uint32_t)int_id;
}

void gic_end_interrupt(uint32_t int_id)
{
    /* EOImode=0: EOIR does full priority drop + deactivation — no DIR write */
    __asm__ volatile("msr S3_0_c12_c12_1, %x0" :: "rZ"((uint64_t)int_id));  /* ICC_EOIR1_EL1 */
    __asm__ volatile("isb");
}

void gic_enable_spi_interrupt(uint32_t int_id)
{
    /* SPI interrupts are ID 32 and above */
    if (int_id < 32u) {
        uart_puts("[GIC] Error: Invalid SPI interrupt ID\n");
        return;
    }

    /* Calculate register offset for GICD_ISENABLER */
    uint32_t reg_offset = (int_id / 32u) * 4u;
    uint32_t bit_offset = int_id % 32u;
    uint32_t reg_addr = GICD_BASE + 0x100u + reg_offset; /* GICD_ISENABLER base */

    /* Enable the interrupt */
    mmio_write32(reg_addr, (1u << bit_offset));

    /* Set interrupt priority (lower value = higher priority) */
    uint32_t prio_reg_offset = int_id * 1u; /* 8-bit per interrupt */
    uint32_t prio_reg_addr = GICD_BASE + 0x400u + prio_reg_offset; /* GICD_IPRIORITYR base */
    mmio_write8(prio_reg_addr, 0x80u); /* Medium priority */

    /* GICv3: Route interrupt to CPU 0 using GICD_IROUTER (not GICD_ITARGETSR) */
    /* GICD_IROUTER is 64-bit per interrupt, starting from ID 32 */
    if (int_id >= 32u) {
        uint32_t router_offset = (int_id - 32u) * 8u; /* 8 bytes per interrupt */
        uint32_t router_addr_low = GICD_BASE + 0x6000u + router_offset;  /* GICD_IROUTER base */
        uint32_t router_addr_high = router_addr_low + 4u;

        /* Set to CPU 0 (MPIDR = 0) */
        mmio_write32(router_addr_low, 0u);   /* Lower 32 bits: Aff0, Aff1, Aff2 */
        mmio_write32(router_addr_high, 0u);  /* Upper 32 bits: Aff3, IRM */
    }

    /* Set interrupt to Group 1 */
    uint32_t group_reg_offset = (int_id / 32u) * 4u;
    uint32_t group_bit_offset = int_id % 32u;
    uint32_t group_reg_addr = GICD_BASE + 0x80u + group_reg_offset; /* GICD_IGROUPR base */
    uint32_t group_val = mmio_read32(group_reg_addr);
    group_val |= (1u << group_bit_offset);
    mmio_write32(group_reg_addr, group_val);
}