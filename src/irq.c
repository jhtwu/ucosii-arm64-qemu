#include <stdint.h>

#include <ucos_ii.h>

#include "gic.h"
#include "timer.h"
#include "uart.h"
#include "bsp_int.h"

#define TIMER_INTERRUPT_ID  27u
#define GIC_SPURIOUS_BASE   1020u

/*
 * 中文：IRQ 分派函數 - 由組語 IRQ handler 調用
 *       注意：不在這裡調用 OSIntEnter/OSIntExit，這些在組語層面處理
 * English: IRQ dispatch function - called from assembly IRQ handler
 *          Note: Do NOT call OSIntEnter/OSIntExit here, they're handled in assembly
 */
void irq_dispatch(void)
{
    uint32_t raw_id = gic_acknowledge();
    uint32_t int_id = raw_id & 0x3FFu;

    if (int_id >= GIC_SPURIOUS_BASE) {
        uart_puts("[IRQ] spurious interrupt, ignoring\n");
        return;
    }

    /* Use BSP interrupt handler to dispatch */
    BSP_IntHandler(int_id);

    gic_end_interrupt(raw_id);
}
