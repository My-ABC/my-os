#include "stdio.h"
#include "keyboard.h"
#include "vga.h"

void print(const char* str) {
    vga_write(str, (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE);
}

void print_color(const char* str, uint8_t color) {
    vga_write(str, color);
}

void putchar(char c) {
    vga_putchar(c, (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE);
}

void print_info(const char* str) {
    vga_write(str, (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE);
}

void print_error(const char* str) {
    vga_write(str, (VGA_COLOR_BLACK << 4) | VGA_COLOR_RED);
}

void print_warning(const char* str) {
    vga_write(str, (VGA_COLOR_BLACK << 4) | VGA_COLOR_BROWN);
}

char getchar(void) {
    return keyboard_getchar();
}

int has_key(void) {
    return keyboard_has_key();
}

void scan(char* buffer, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = getchar();
        if (c == '\n') {
            buffer[i] = '\0';
            putchar('\n');
            break;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c >= 0x20 && c < 0x7F) {
            buffer[i++] = c;
            putchar(c);
        }
    }
    if (i >= max_len - 1) {
        buffer[i] = '\0';
    }
}