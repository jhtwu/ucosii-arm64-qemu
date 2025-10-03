#include "bsp_os.h"
#include "timer.h"
#include "uart.h"
#include <ucos_ii.h>

/*
 * Arch timer definitions from armv8 project
 */
enum arch_timer_reg {
    ARCH_TIMER_REG_CTRL,
    ARCH_TIMER_REG_TVAL,
};

#define ARCH_TIMER_PHYS_ACCESS      0
#define ARCH_TIMER_VIRT_ACCESS      1

#define ARCH_TIMER_CTRL_ENABLE      (1 << 0)
#define ARCH_TIMER_CTRL_IT_MASK     (1 << 1)  /* Interrupt mask bit */
#define ARCH_TIMER_CTRL_IT_STAT     (1 << 2)

/* Make sure timer is enabled with interrupt unmasked */
#define ARCH_TIMER_CTRL_ENABLED_UNMASKED  (ARCH_TIMER_CTRL_ENABLE)

#ifndef BSP_OS_TMR_PRESCALE
#define BSP_OS_TMR_PRESCALE         10u   /* Default prescale (tick_rate / 10) */
#endif

static uint32_t BSP_OS_TmrReload;

/*
 * Arch timer register write function
 */
static inline void arch_timer_reg_write_cp15(int access, enum arch_timer_reg reg, uint32_t val)
{
    if (access == ARCH_TIMER_VIRT_ACCESS) {
        switch (reg) {
        case ARCH_TIMER_REG_CTRL:
            uart_puts("[ARCH_TIMER] Writing CNTV_CTL_EL0\n");
            __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(val));
            break;
        case ARCH_TIMER_REG_TVAL:
            __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(val));
            break;
        }
    }
}

/*
 * Virtual timer reload function - ensure timer stays enabled and unmasked
 */
static inline void BSP_OS_VirtTimerReload(void)
{
    uint32_t reload = BSP_OS_TmrReload;
    if (reload != 0u) {
        /* Set timer value */
        __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(reload));
        
        /* Ensure timer stays enabled and unmasked */
        __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(ARCH_TIMER_CTRL_ENABLED_UNMASKED));
    }
}

/*
 * BSP OS timer tick handler - called when timer interrupt occurs
 */
void BSP_OS_TmrTickHandler(uint32_t cpu_id)
{
    (void)cpu_id;
    
    uart_puts("[TIMER] Entry\n");
    
    /* CRITICAL: Reload timer FIRST */
    BSP_OS_VirtTimerReload();
    uart_puts("[TIMER] Reloaded\n");
    
    /* Drive µC/OS-II scheduler - this may cause context switch */
    OSTimeTick();
    uart_puts("[TIMER] OSTimeTick done\n");
    
    uart_puts("[TIMER] Exit\n");
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
    
    uart_puts("[BSP_OS] Enabling virtual timer with unmasked interrupts\n");
    
    /* Enable timer with interrupt unmasked - critical for continuous operation */
    arch_timer_reg_write_cp15(ARCH_TIMER_VIRT_ACCESS, ARCH_TIMER_REG_CTRL, ARCH_TIMER_CTRL_ENABLED_UNMASKED);
    BSP_OS_VirtTimerReload();
    
    uart_puts("[BSP_OS] Timer initialized and running\n");
}