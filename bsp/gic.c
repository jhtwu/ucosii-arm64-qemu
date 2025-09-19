#include "gic.h"
#include "mmio.h"

#define GICD_BASE           0x08000000u
#define GICC_BASE           0x08010000u

#define GICD_CTLR           (GICD_BASE + 0x000u)
#define GICD_ISENABLER0     (GICD_BASE + 0x100u)
#define GICD_ICENABLER0     (GICD_BASE + 0x180u)
#define GICD_IPRIORITYR(n)  (GICD_BASE + 0x400u + ((n) * 4u))
#define GICD_IGROUPR0       (GICD_BASE + 0x080u)

#define GICC_CTLR           (GICC_BASE + 0x000u)
#define GICC_PMR            (GICC_BASE + 0x004u)
#define GICC_BPR            (GICC_BASE + 0x008u)
#define GICC_IAR            (GICC_BASE + 0x00Cu)
#define GICC_EOIR           (GICC_BASE + 0x010u)

#define TIMER_INTERRUPT_ID  27u

void gic_init(void)
{
    mmio_write32(GICC_CTLR, 0u);
    mmio_write32(GICD_CTLR, 0u);

    uint32_t priority = mmio_read32(GICD_IPRIORITYR(TIMER_INTERRUPT_ID / 4u));
    uint32_t shift = (TIMER_INTERRUPT_ID % 4u) * 8u;
    priority &= ~(0xFFu << shift);
    priority |= (0x40u << shift);
    mmio_write32(GICD_IPRIORITYR(TIMER_INTERRUPT_ID / 4u), priority);

    uint32_t group = mmio_read32(GICD_IGROUPR0);
    group &= ~(1u << TIMER_INTERRUPT_ID);
    mmio_write32(GICD_IGROUPR0, group);

    mmio_write32(GICD_ICENABLER0, (1u << TIMER_INTERRUPT_ID));
    mmio_write32(GICD_ISENABLER0, (1u << TIMER_INTERRUPT_ID));

    mmio_write32(GICC_PMR, 0xFFu);
    mmio_write32(GICC_BPR, 0u);
    mmio_write32(GICC_CTLR, 1u);
    mmio_write32(GICD_CTLR, 1u);
}

uint32_t gic_acknowledge(void)
{
    return mmio_read32(GICC_IAR) & 0x3FFu;
}

void gic_end_interrupt(uint32_t int_id)
{
    mmio_write32(GICC_EOIR, int_id & 0x3FFu);
}
