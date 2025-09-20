#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

/*
 * Chinese: 初始化 UART，設定鮑率、資料位、停止位等參數。
 * English: Initializes the UART, setting parameters like baud rate, data bits, stop bits, etc.
 */
void uart_init(void);

/*
 * Chinese: 透過 UART 傳送一個字元。
 * English: Transmits a single character via UART.
 */
void uart_putc(char c);

/*
 * Chinese: 透過 UART 傳送一個以 null 結尾的字串。
 * English: Transmits a null-terminated string via UART.
 */
void uart_puts(const char *s);

/*
 * Chinese: 將一個無符號長整數以十六進位格式透過 UART 傳送。
 * English: Transmits an unsigned long integer in hexadecimal format via UART.
 */
void uart_write_hex(unsigned long value);

/*
 * Chinese: 將一個 32 位元無符號整數以十進位格式透過 UART 傳送。
 * English: Transmits a 32-bit unsigned integer in decimal format via UART.
 */
void uart_write_dec(uint32_t value);

#endif