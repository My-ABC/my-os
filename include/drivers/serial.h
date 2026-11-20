#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define SERIAL_COM1 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_print(const char *str);
void serial_print_hex(uint32_t num);
void serial_print_dec(uint32_t num);

int serial_getchar(void);
int serial_wait_char(void);
void serial_gets(char *buffer, int size);

#endif