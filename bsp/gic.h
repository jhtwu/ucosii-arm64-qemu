#ifndef BSP_GIC_H
#define BSP_GIC_H

#include <stdint.h>

/*
 * Chinese: 初始化通用中斷控制器 (GIC)。
 * English: Initializes the Generic Interrupt Controller (GIC).
 */
void gic_init(void);

/*
 * Chinese: 回應一個中斷請求，並回傳中斷 ID。
 * English: Acknowledges an interrupt request and returns the interrupt ID.
 */
uint32_t gic_acknowledge(void);

/*
 * Chinese: 通知 GIC 中斷處理已完成。
 * English: Notifies the GIC that interrupt processing is complete.
 */
void gic_end_interrupt(uint32_t int_id);

/*
 * Chinese: 啟用 SPI 中斷。
 * English: Enables an SPI (Shared Peripheral Interrupt).
 */
void gic_enable_spi_interrupt(uint32_t int_id);

#endif