#include "vga.h"

static uint16_t* const vga_buffer = (uint16_t*)0xB8000;
static uint16_t vga_cursor = 0;
static uint8_t vga_color = (VGA_COLOR_WHITE << 4) | VGA_COLOR_BLACK;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_update_hardware_cursor(void) {
    uint16_t pos = vga_cursor;
    __asm__ volatile (
        "movb $0x0F, %%al\n"
        "movw $0x03D4, %%dx\n"
        "outb %%al, %%dx\n"
        "movw $0x03D5, %%dx\n"
        "movb %b0, %%al\n"
        "outb %%al, %%dx\n"
        : : "r"((uint8_t)(pos & 0xFF))
        : "al", "dx"
    );
    __asm__ volatile (
        "movb $0x0E, %%al\n"
        "movw $0x03D4, %%dx\n"
        "outb %%al, %%dx\n"
        "movw $0x03D5, %%dx\n"
        "movb %b0, %%al\n"
        "outb %%al, %%dx\n"
        : : "r"((uint8_t)((pos >> 8) & 0xFF))
        : "al", "dx"
    );
}

void vga_clear(void) {
    uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ', color);
    }
    vga_cursor = 0;
    vga_update_hardware_cursor();
}

void vga_scroll(void) {
    // 上移一行
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }
    // 清空最后一行
    uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE;
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ', color);
    }
    vga_cursor = (VGA_HEIGHT - 1) * VGA_WIDTH;
}

void vga_putchar(char c, uint8_t color) {
    if (c == '\n') {
        vga_cursor = (vga_cursor / VGA_WIDTH + 1) * VGA_WIDTH;
        if (vga_cursor >= VGA_WIDTH * VGA_HEIGHT) {
            vga_scroll();
        }
        vga_update_hardware_cursor();
        return;
    }
    
    if (c == '\b') {
        if (vga_cursor > 0) {
            vga_cursor--;
            vga_buffer[vga_cursor] = vga_entry(' ', color);
            vga_update_hardware_cursor();
        }
        return;
    }
    
    vga_buffer[vga_cursor++] = vga_entry(c, color);
    if (vga_cursor >= VGA_WIDTH * VGA_HEIGHT) {
        vga_scroll();
    }
    vga_update_hardware_cursor();
}

void vga_putchar_at(char c, uint8_t color, uint16_t x, uint16_t y) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    uint16_t pos = y * VGA_WIDTH + x;
    vga_buffer[pos] = vga_entry(c, color);
}

void vga_write(const char* str, uint8_t color) {
    for (int i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i], color);
    }
}

uint16_t vga_get_cursor(void) {
    return vga_cursor;
}

void vga_set_cursor(uint16_t pos) {
    if (pos < VGA_WIDTH * VGA_HEIGHT) {
        vga_cursor = pos;
        vga_update_hardware_cursor();
    }
}