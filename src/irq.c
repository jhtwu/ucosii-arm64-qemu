#include <stdint.h>

#include <ucos_ii.h>

#include "gic.h"
#include "timer.h"
#include "uart.h"

#define TIMER_INTERRUPT_ID  30u
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
        return;
    }

    OSIntEnter();

    if (int_id == TIMER_INTERRUPT_ID) {
        static uint32_t seconds = 0u;
        timer_ack();
        ++seconds;
        uart_puts("[TIMER] one second tick: ");
        uart_write_dec(seconds);
        uart_putc('\n');
        OSTimeTick();
    } else {
        uart_puts("[IRQ] unexpected source: ");
        uart_write_dec(int_id);
        uart_putc('\n');
    }

    gic_end_interrupt(raw_id);
    OSIntExit();
}
