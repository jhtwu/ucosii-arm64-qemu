#include <stdint.h>

#include "mmio.h"
#include "uart.h"

#define UART0_BASE       0x09000000u
#define UARTDR           (UART0_BASE + 0x00u)
#define UARTFR           (UART0_BASE + 0x18u)
#define UARTIBRD         (UART0_BASE + 0x24u)
#define UARTFBRD         (UART0_BASE + 0x28u)
#define UARTLCRH         (UART0_BASE + 0x2Cu)
#define UARTCR           (UART0_BASE + 0x30u)
#define UARTIMSC         (UART0_BASE + 0x38u)

#define UARTFR_TXFF      (1u << 5)

/*
 * Chinese: 初始化 UART，設定鮑率、資料位、停止位等參數。
 * English: Initializes the UART, setting parameters like baud rate, data bits, stop bits, etc.
 */
void uart_init(void)
{
    mmio_write32(UARTCR, 0u);
    mmio_write32(UARTIMSC, 0u);
    mmio_write32(UARTIBRD, 13u);
    mmio_write32(UARTFBRD, 2u);
    mmio_write32(UARTLCRH, (3u << 5) | (1u << 4));
    mmio_write32(UARTCR, (1u << 9) | (1u << 8) | (1u << 0));
}

/*
 * Chinese: 透過 UART 傳送一個字元。
 * English: Transmits a single character via UART.
 */
void uart_putc(char c)
{
    if (c == '\n') {
        uart_putc('\r');
    }
    while (mmio_read32(UARTFR) & UARTFR_TXFF) {
    }
    mmio_write32(UARTDR, (uint32_t)c);
}

/*
 * Chinese: 透過 UART 傳送一個以 null 結尾的字串。
 * English: Transmits a null-terminated string via UART.
 */
void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

/*
 * Chinese: 將一個無符號長整數以十六進位格式透過 UART 傳送。
 * English: Transmits an unsigned long integer in hexadecimal format via UART.
 */
void uart_write_hex(unsigned long value)
{
    static const char digits[] = "0123456789ABCDEF";
    for (int shift = (int)(sizeof(unsigned long) * 8) - 4; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> shift) & 0xFu]);
    }
}

/*
 * Chinese: 將一個 32 位元無符號整數以十進位格式透過 UART 傳送。
 * English: Transmits a 32-bit unsigned integer in decimal format via UART.
 */
void uart_write_dec(uint32_t value)
{
    char buffer[16];
    int idx = 0;

    if (value == 0u) {
        uart_putc('0');
        return;
    }

    while ((value > 0u) && (idx < (int)sizeof(buffer))) {
        buffer[idx++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (idx > 0) {
        uart_putc(buffer[--idx]);
    }
}