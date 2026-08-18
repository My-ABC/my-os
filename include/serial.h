#ifndef _SERIAL_H
#define _SERIAL_H

#include "stdint.h"

#define SERIAL_COM1 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_print(const char* str);
void serial_print_dec(uint32_t num);

#endif
