#include "timer.h"

static uint64_t g_timer_reload = 0u;

static inline uint64_t cntfrq_read(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

static inline uint64_t cntvct_read(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
}

static inline void cntv_tval_write(uint64_t value)
{
    __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(value));
}

static inline void cntv_ctl_write(uint64_t value)
{
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(value));
}

void timer_init(uint32_t tick_hz)
{
    uint64_t freq = cntfrq_read();
    g_timer_reload = freq / (uint64_t)tick_hz;
    if (g_timer_reload == 0u) {
        g_timer_reload = 1u;
    }

    cntv_ctl_write(0u);
    cntv_tval_write(g_timer_reload);
    cntv_ctl_write(1u);
}

void timer_ack(void)
{
    cntv_tval_write(g_timer_reload);
}

void timer_delay_ms(uint32_t ms)
{
    uint64_t freq = cntfrq_read();
    uint64_t ticks = (freq / 1000u) * (uint64_t)ms;
    uint64_t start = cntvct_read();
    while ((cntvct_read() - start) < ticks) {
    }
}

uint64_t timer_cntfrq(void)
{
    return cntfrq_read();
}
