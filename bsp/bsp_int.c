#include "bsp_int.h"
#include "uart.h"
#include <stddef.h>

#define MAX_INTERRUPTS 256u

/*
 * Global interrupt vector table
 */
static BSP_INT_FNCT_PTR bsp_int_vect_tbl[MAX_INTERRUPTS];

/*
 * Register interrupt service routine
 */
void BSP_IntVectSet(uint32_t int_id, uint32_t int_prio, uint32_t int_target, BSP_INT_FNCT_PTR int_fnct)
{
    (void)int_prio;    /* Unused in this simple implementation */
    (void)int_target;  /* Unused in this simple implementation */
    
    if (int_id < MAX_INTERRUPTS) {
        bsp_int_vect_tbl[int_id] = int_fnct;
        uart_puts("[BSP] Registered ISR for interrupt ");
        uart_write_dec(int_id);
        uart_putc('\n');
    }
}

/*
 * Enable interrupt source (placeholder - actual enable done in GIC)
 */
void BSP_IntSrcEn(uint32_t int_id)
{
    uart_puts("[BSP] Enabled interrupt ");
    uart_write_dec(int_id);
    uart_putc('\n');
    /* Note: Actual interrupt enable is handled by GIC in gic.c */
}

/*
 * Disable interrupt source (placeholder)
 */
void BSP_IntSrcDis(uint32_t int_id)
{
    uart_puts("[BSP] Disabled interrupt ");
    uart_write_dec(int_id);
    uart_putc('\n');
}

/*
 * BSP interrupt handler - called from irq_dispatch
 */
void BSP_IntHandler(uint32_t int_id)
{
    if (int_id < MAX_INTERRUPTS && bsp_int_vect_tbl[int_id] != NULL) {
        uart_puts("[BSP] Dispatching ISR for interrupt ");
        uart_write_dec(int_id);
        uart_putc('\n');
        bsp_int_vect_tbl[int_id](int_id);
    } else {
        uart_puts("[BSP] No ISR registered for interrupt ");
        uart_write_dec(int_id);
        uart_putc('\n');
    }
}