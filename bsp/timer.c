#include "timer.h"

static uint64_t g_timer_reload = 0u;

/*
 * Chinese: 讀取計時器頻率（cntfrq_el0）。
 * English: Reads the timer frequency (cntfrq_el0).
 */
static inline uint64_t cntfrq_read(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

/*
 * Chinese: 讀取計時器計數（cntpct_el0）。
 * English: Reads the timer count (cntpct_el0).
 */
static inline uint64_t cntpct_read(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

/*
 * Chinese: 寫入虛擬計時器比較值（cntv_tval_el0）。
 * English: Writes the virtual timer compare value (cntv_tval_el0).
 */
static inline void cntv_tval_write(uint64_t value)
{
    __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(value));
}

/*
 * Chinese: 寫入虛擬計時器控制暫存器（cntv_ctl_el0）。
 * English: Writes the virtual timer control register (cntv_ctl_el0).
 */
static inline void cntv_ctl_write(uint64_t value)
{
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(value));
}

/*
 * Chinese: 初始化系統計時器，設定指定的滴答頻率（Hz）。
 * English: Initializes the system timer with the specified tick frequency in Hz.
 */
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

/*
 * Chinese: 確認計時器中斷，並為下一次中斷重新載入計時器。
 * English: Acknowledges the timer interrupt and reloads the timer for the next interrupt.
 */
void timer_ack(void)
{
    cntv_tval_write(g_timer_reload);
}

/*
 * Chinese: 執行一個基於計時器的毫秒級延遲。
 * English: Performs a timer-based delay in milliseconds.
 */
void timer_delay_ms(uint32_t ms)
{
    uint64_t freq = cntfrq_read();
    uint64_t ticks = (freq / 1000u) * (uint64_t)ms;
    uint64_t start = cntpct_read();
    while ((cntpct_read() - start) < ticks) {
    }
}

/*
 * Chinese: 讀取計時器的頻率（CNTFREQ_EL0）。
 * English: Reads the timer's frequency (CNTFREQ_EL0).
 */
uint64_t timer_cntfrq(void)
{
    return cntfrq_read();
}