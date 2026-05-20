#include "bsp_os.h"
#include "timer.h"
#include "uart.h"
#include <ucos_ii.h>

#define ARCH_TIMER_CTRL_ENABLE      (1u << 0)
#define ARCH_TIMER_CTRL_IT_MASK     (1u << 1)
#define ARCH_TIMER_CTRL_IT_STAT     (1u << 2)

#ifndef BSP_OS_TMR_PRESCALE
#define BSP_OS_TMR_PRESCALE         10u
#endif

static uint32_t BSP_OS_TmrReload;

/*
 * 3-step virtual timer reload (required for level-triggered timer on KVM):
 *   1. CTL=7: ENABLE=1, IMASK=1 — mask interrupt to clear pending level
 *   2. TVAL = reload count — set next deadline
 *   3. CTL=5: ENABLE=1, IMASK=0, ISTAT=1 — re-enable with interrupt unmasked
 */
static inline void BSP_OS_VirtTimerReload(void)
{
    uint32_t reload = BSP_OS_TmrReload;
    uint32_t ctrl;

    if (reload == 0u) {
        return;
    }

    /* Step 1: mask interrupt */
    ctrl = ARCH_TIMER_CTRL_ENABLE | ARCH_TIMER_CTRL_IT_MASK;
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"((uint64_t)ctrl));

    /* Step 2: set new countdown value */
    __asm__ volatile("msr cntv_tval_el0, %0" :: "r"((uint64_t)reload));

    /* Step 3: re-enable with interrupt unmasked (ENABLE=1, IMASK=0) */
    ctrl = ARCH_TIMER_CTRL_ENABLE;
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"((uint64_t)ctrl));
}

/*
 * BSP OS timer tick handler - called when timer interrupt occurs
 */
void BSP_OS_TmrTickHandler(uint32_t cpu_id)
{
    static uint32_t tick_count = 0;
    uint32_t ctrl;
    (void)cpu_id;

    /* Drive µC/OS-II scheduler first */
    OSTimeTick();

    tick_count++;
    if ((tick_count % 1000) == 0) {
        uart_puts("[TICK] ");
        uart_write_dec(tick_count / 1000);
        uart_puts("s\n");
    }

    /* Reload timer unconditionally — handler was entered because the timer fired */
    BSP_OS_VirtTimerReload();
}

/*
 * Initialize timer tick system like armv8 project
 */
void BSP_OS_TmrTickInit(uint32_t tick_rate)
{
    uart_puts("[BSP_OS] BSP_OS_TmrTickInit\n");
    
    if (tick_rate == 0u) {
        tick_rate = 1000u;  /* Default 1000 Hz */
    }
    
    uint32_t cnt_freq = timer_cntfrq();
    uart_puts("[BSP_OS] Counter frequency = ");
    uart_write_dec(cnt_freq);
    uart_putc('\n');
    
    /* Use a more reasonable prescale for testing */
    uint32_t eff_rate = tick_rate;  /* No prescale for now */
    uint32_t reload = cnt_freq / eff_rate;
    if (reload == 0u) {
        reload = 1u;
    }
    
    BSP_OS_TmrReload = reload;
    
    uart_puts("[BSP_OS] Effective rate = ");
    uart_write_dec(eff_rate);
    uart_puts(" Hz, reload = ");
    uart_write_dec(reload);
    uart_putc('\n');
    
    uart_puts("[BSP_OS] Enabling virtual timer\n");

    /* Initial 3-step load: mask → set TVAL → enable unmasked */
    BSP_OS_VirtTimerReload();

    uart_puts("[BSP_OS] Timer initialized and running\n");
}
