#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdint.h>

void timer_init(uint32_t tick_hz);
void timer_ack(void);
void timer_delay_ms(uint32_t ms);

uint64_t timer_cntfrq(void);

#endif
