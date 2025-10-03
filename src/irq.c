#include <stdint.h>

#include <ucos_ii.h>

#include "gic.h"
#include "timer.h"
#include "uart.h"
#include "bsp_int.h"

#define TIMER_INTERRUPT_ID  27u
#define GIC_SPURIOUS_BASE   1020u

void irq_dispatch(void)
{
    uint32_t raw_id = gic_acknowledge();
    uint32_t int_id = raw_id & 0x3FFu;

    uart_puts("[IRQ] raw id = ");
    uart_write_dec(raw_id);
    uart_puts(" resolved = ");
    uart_write_dec(int_id);
    uart_putc('\n');

    if (int_id >= GIC_SPURIOUS_BASE) {
        uart_puts("[IRQ] spurious interrupt, ignoring\n");
        return;
    }

    OSIntEnter();

    /* Use BSP interrupt handler to dispatch */
    BSP_IntHandler(int_id);

    gic_end_interrupt(raw_id);
    OSIntExit();
}
