#ifndef BSP_OS_H
#define BSP_OS_H

#include <stdint.h>

/*
 * BSP OS timer tick handler
 */
void BSP_OS_TmrTickHandler(uint32_t cpu_id);

/*
 * BSP OS timer tick initialization
 */
void BSP_OS_TmrTickInit(uint32_t tick_rate);

#endif /* BSP_OS_H */