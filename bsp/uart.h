#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_write_hex(unsigned long value);
void uart_write_dec(uint32_t value);

#endif
