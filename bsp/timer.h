#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdint.h>

/*
 * Chinese: 初始化系統計時器，設定指定的滴答頻率（Hz）。
 * English: Initializes the system timer with the specified tick frequency in Hz.
 */
void timer_init(uint32_t tick_hz);

/*
 * Chinese: 確認計時器中斷，並為下一次中斷重新載入計時器。
 * English: Acknowledges the timer interrupt and reloads the timer for the next interrupt.
 */
void timer_ack(void);

/*
 * Chinese: 執行一個基於計時器的毫秒級延遲。
 * English: Performs a timer-based delay in milliseconds.
 */
void timer_delay_ms(uint32_t ms);

/*
 * Chinese: 讀取計時器的頻率（CNTFREQ_EL0）。
 * English: Reads the timer's frequency (CNTFREQ_EL0).
 */
uint64_t timer_cntfrq(void);

#endif