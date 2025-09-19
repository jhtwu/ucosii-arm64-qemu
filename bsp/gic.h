#ifndef BSP_GIC_H
#define BSP_GIC_H

#include <stdint.h>

void gic_init(void);
uint32_t gic_acknowledge(void);
void gic_end_interrupt(uint32_t int_id);

#endif
