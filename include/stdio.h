#ifndef _STDIO_H
#define _STDIO_H

#include "stdarg.h"
#include "stdint.h"

// Output functions
int putchar(char c);
int puts(const char* str);

// Formatted output functions
int printf(const char* format, ...);
int sprintf(char* str, const char* format, ...);

// Output to serial console
int serial_printf(const char* format, ...);

#endif
