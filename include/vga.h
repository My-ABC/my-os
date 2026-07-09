#ifndef _VGA_H
#define _VGA_H

#include <stdint.h>

// VGA 颜色枚举
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GRAY = 7,
    VGA_COLOR_DARK_GRAY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15,
};

void vga_init(void);

void vga_clear(void);

void vga_set_color(enum vga_color fg, enum vga_color bg);

void vga_putchar(char c);

void vga_print(const char* str);

void vga_print_color(const char* str, enum vga_color fg, enum vga_color bg);

void vga_print_info(const char* str);

void vga_print_warning(const char* str);

void vga_print_error(const char* str);

void vga_print_success(const char* str);

void vga_print_hex(uint32_t num);

void vga_print_dec(uint32_t num);

void vga_get_cursor(uint16_t* row, uint16_t* col);

void vga_set_cursor(uint16_t row, uint16_t col);

#endif