#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define SERIAL_COM1 0x3F8
#define SERIAL_COM3 0x3E8

void serial_init(void);
void serial_putchar(char c);
void serial_print(const char *str);
void serial_print_hex(uint32_t num);
void serial_print_dec(uint32_t num);

/* Wait until the COM1 transmitter (THR + FIFO + shift register) is
 * fully drained, bounded by the same timeout as serial_putchar. Use
 * before any action that can cut power so queued diagnostics are not
 * lost mid-line. */
void serial_flush(void);

/* COM3 output mirror for Ring 3 console traffic; COM1 stays reserved
 * for kernel diagnostics. */
void serial3_putchar(char c);
void serial3_print(const char *str);

int serial_getchar(void);

#endif