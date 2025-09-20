#include "gic.h"
#include "mmio.h"
#include "uart.h"

#define GICD_BASE           0x08000000u
#define GICR_BASE           0x080A0000u

#define GICD_CTLR           (GICD_BASE + 0x000u)

#define GICR_CTLR           (GICR_BASE + 0x0000u)
#define GICR_WAKER          (GICR_BASE + 0x0014u)

#define GICR_SGI_BASE       (GICR_BASE + 0x10000u)
#define GICR_IGROUPR0       (GICR_SGI_BASE + 0x0080u)
#define GICR_ISENABLER0     (GICR_SGI_BASE + 0x0100u)
#define GICR_ICENABLER0     (GICR_SGI_BASE + 0x0180u)
#define GICR_ISPENDR0        (GICR_SGI_BASE + 0x0200u)
#define GICR_IPRIORITYR(n)  (GICR_SGI_BASE + 0x0400u + ((n) * 4u))
#define GICR_IGRPMODR0      (GICR_SGI_BASE + 0x0D00u)

#define TIMER_INTERRUPT_ID  30u

/*
 * Chinese: 讀取 ICC_SRE_EL1 系統暫存器。
 * English: Reads the ICC_SRE_EL1 system register.
 */
static inline uint32_t read_icc_sre_el1(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, ICC_SRE_EL1" : "=r"(value));
    return (uint32_t)value;
}

/*
 * Chinese: 寫入 ICC_SRE_EL1 系統暫存器。
 * English: Writes to the ICC_SRE_EL1 system register.
 */
static inline void write_icc_sre_el1(uint32_t value)
{
    __asm__ volatile("msr ICC_SRE_EL1, %0" :: "r"((uint64_t)value));
    __asm__ volatile("isb");
}

/*
 * Chinese: 寫入 ICC_CTLR_EL1 系統暫存器。
 * English: Writes to the ICC_CTLR_EL1 system register.
 */
static inline void write_icc_ctlr_el1(uint32_t value)
{
    __asm__ volatile("msr ICC_CTLR_EL1, %0" :: "r"((uint64_t)value));
}

/*
 * Chinese: 寫入 ICC_PMR_EL1 系統暫存器，設定中斷優先級過濾。
 * English: Writes to the ICC_PMR_EL1 system register to set the interrupt priority mask.
 */
static inline void write_icc_pmr_el1(uint32_t value)
{
    __asm__ volatile("msr ICC_PMR_EL1, %0" :: "r"((uint64_t)value));
}

/*
 * Chinese: 寫入 ICC_BPR1_EL1 系統暫存器，設定二進位點。
 * English: Writes to the ICC_BPR1_EL1 system register to set the binary point.
 */
static inline void write_icc_bpr1_el1(uint32_t value)
{
    __asm__ volatile("msr ICC_BPR1_EL1, %0" :: "r"((uint64_t)value));
}

/*
 * Chinese: 寫入 ICC_IGRPEN1_EL1 系統暫存器，啟用 Group 1 中斷。
 * English: Writes to the ICC_IGRPEN1_EL1 system register to enable Group 1 interrupts.
 */
static inline void write_icc_igrpen1_el1(uint32_t value)
{
    __asm__ volatile("msr ICC_IGRPEN1_EL1, %0" :: "r"((uint64_t)value));
    __asm__ volatile("isb");
}

/*
 * Chinese: 初始化通用中斷控制器 (GIC)。
 * English: Initializes the Generic Interrupt Controller (GIC).
 */
void gic_init(void)
{
    uint32_t sre = read_icc_sre_el1();
    if ((sre & 1u) == 0u) {
        write_icc_sre_el1(sre | 0x7u);
    }

    mmio_write32(GICD_CTLR, 0u);
    mmio_write32(GICR_CTLR, 0u);

    uint32_t waker = mmio_read32(GICR_WAKER);
    waker &= ~(1u << 1);                       /* Clear ProcessorSleep */
    mmio_write32(GICR_WAKER, waker);
    while (mmio_read32(GICR_WAKER) & (1u << 2)) {
    }

    uint32_t priority = mmio_read32(GICR_IPRIORITYR(TIMER_INTERRUPT_ID / 4u));
    uint32_t shift = (TIMER_INTERRUPT_ID % 4u) * 8u;
    priority &= ~(0xFFu << shift);
    priority |= (0x80u << shift);
    mmio_write32(GICR_IPRIORITYR(TIMER_INTERRUPT_ID / 4u), priority);

    uint32_t group = mmio_read32(GICR_IGROUPR0);
    group |= (1u << TIMER_INTERRUPT_ID);
    mmio_write32(GICR_IGROUPR0, group);

    uint32_t group_mod = mmio_read32(GICR_IGRPMODR0);
    group_mod |= (1u << TIMER_INTERRUPT_ID);
    mmio_write32(GICR_IGRPMODR0, group_mod);

    mmio_write32(GICR_ICENABLER0, (1u << TIMER_INTERRUPT_ID));
    mmio_write32(GICR_ISENABLER0, (1u << TIMER_INTERRUPT_ID));

    uart_puts("[GIC] IGROUPR0=");
    uart_write_hex(mmio_read32(GICR_IGROUPR0));
    uart_puts(" ISENABLER0=");
    uart_write_hex(mmio_read32(GICR_ISENABLER0));
    uart_puts(" ISPENDR0=");
    uart_write_hex(mmio_read32(GICR_ISPENDR0));
    uart_putc('\n');

    mmio_write32(GICD_CTLR, (1u << 1) | (1u << 4));

    write_icc_ctlr_el1(0u);
    write_icc_pmr_el1(0xFFu);
    write_icc_bpr1_el1(0u);
    write_icc_igrpen1_el1(1u);
}

/*
 * Chinese: 回應一個中斷請求，並回傳中斷 ID。
 * English: Acknowledges an interrupt request and returns the interrupt ID.
 */
uint32_t gic_acknowledge(void)
{
    uint64_t int_id;
    __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(int_id));
    return (uint32_t)(int_id & 0xFFFFFFu);
}

/*
 * Chinese: 通知 GIC 中斷處理已完成。
 * English: Notifies the GIC that interrupt processing is complete.
 */
void gic_end_interrupt(uint32_t int_id)
{
    uint64_t value = (uint64_t)int_id;
    __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"(value));
    __asm__ volatile("msr ICC_DIR_EL1, %0" :: "r"(value));
}