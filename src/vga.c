#include <stdint.h>
#include <stddef.h>
#include "vga.h"

// VGA 参数
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

// 全局变量
static uint16_t cursor_row = 0;
static uint16_t cursor_col = 0;
static uint8_t current_color = 0x0F;  // 默认白字黑底

// ========== 端口 I/O 函数（放在最前面） ==========
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// ========== 内部函数 ==========
// 内部函数：创建 VGA 条目
static inline uint16_t vga_entry(unsigned char ch, uint8_t color) {
    return (uint16_t)(ch | (color << 8));
}

// 内部函数：获取颜色字节
static inline uint8_t vga_color(enum vga_color fg, enum vga_color bg) {
    return (uint8_t)(fg | (bg << 4));
}

// 内部函数：更新硬件光标
static void vga_update_cursor(void) {
    uint16_t pos = cursor_row * VGA_WIDTH + cursor_col;
    
    // 写入光标位置到 VGA 寄存器
    // 0x3D4 是索引寄存器，0x3D5 是数据寄存器
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

// 内部函数：滚动屏幕
static void vga_scroll(void) {
    // 将第 1 行到第 24 行上移
    for (uint16_t row = 1; row < VGA_HEIGHT; row++) {
        for (uint16_t col = 0; col < VGA_WIDTH; col++) {
            uint16_t index = row * VGA_WIDTH + col;
            uint16_t prev_index = (row - 1) * VGA_WIDTH + col;
            VGA_MEMORY[prev_index] = VGA_MEMORY[index];
        }
    }
    
    // 清空最后一行
    uint16_t last_row = VGA_HEIGHT - 1;
    for (uint16_t col = 0; col < VGA_WIDTH; col++) {
        uint16_t index = last_row * VGA_WIDTH + col;
        VGA_MEMORY[index] = vga_entry(' ', current_color);
    }
    
    // 光标移到最后一行的开始
    cursor_row = VGA_HEIGHT - 1;
    cursor_col = 0;
}

// ========== 公共函数 ==========
// 初始化 VGA
void vga_init(void) {
    current_color = vga_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();
}

// 清屏
void vga_clear(void) {
    for (uint16_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = vga_entry(' ', current_color);
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_update_cursor();
}

// 设置颜色
void vga_set_color(enum vga_color fg, enum vga_color bg) {
    current_color = vga_color(fg, bg);
}

// 打印字符
void vga_putchar(char c) {
    if (c == '\n') {
        // 换行
        cursor_row++;
        cursor_col = 0;
    } else if (c == '\r') {
        // 回车
        cursor_col = 0;
    } else if (c == '\t') {
        // 制表符（4个空格）
        do {
            vga_putchar(' ');
        } while (cursor_col % 4 != 0);
        return;
    } else if (c == '\b') {
        // 退格
        if (cursor_col > 0) {
            cursor_col--;
            uint16_t index = cursor_row * VGA_WIDTH + cursor_col;
            VGA_MEMORY[index] = vga_entry(' ', current_color);
        }
    } else {
        // 普通字符
        uint16_t index = cursor_row * VGA_WIDTH + cursor_col;
        VGA_MEMORY[index] = vga_entry((unsigned char)c, current_color);
        cursor_col++;
    }
    
    // 检查是否需要换行
    if (cursor_col >= VGA_WIDTH) {
        cursor_row++;
        cursor_col = 0;
    }
    
    // 检查是否需要滚动
    if (cursor_row >= VGA_HEIGHT) {
        vga_scroll();
    }
    
    // 更新硬件光标
    vga_update_cursor();
}

// 打印字符串
void vga_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i]);
    }
}

// 打印带颜色的字符串
void vga_print_color(const char* str, enum vga_color fg, enum vga_color bg) {
    uint8_t old_color = current_color;
    vga_set_color(fg, bg);
    vga_print(str);
    vga_set_color(old_color & 0x0F, (old_color >> 4) & 0x0F);
}

// 打印信息 (白色)
void vga_print_info(const char* str) {
    vga_print_color("[INFO] ", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_print_color(str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

// 打印警告 (黄色)
void vga_print_warning(const char* str) {
    vga_print_color("[WARN] ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_print_color(str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

// 打印错误 (红色)
void vga_print_error(const char* str) {
    vga_print_color("[ERROR] ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    vga_print_color(str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

// 打印成功 (绿色)
void vga_print_success(const char* str) {
    vga_print_color("[SUCCESS] ", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    vga_print_color(str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

// 打印十六进制数
void vga_print_hex(uint32_t num) {
    char hex_chars[] = "0123456789ABCDEF";
    vga_print("0x");
    
    // 从最高位开始打印
    int started = 0;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (num >> i) & 0xF;
        if (nibble != 0 || started || i == 0) {
            vga_putchar(hex_chars[nibble]);
            started = 1;
        }
    }
}

// 打印十进制数
void vga_print_dec(uint32_t num) {
    if (num == 0) {
        vga_putchar('0');
        return;
    }
    
    char buffer[12];
    int i = 0;
    while (num > 0) {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    while (i > 0) {
        vga_putchar(buffer[--i]);
    }
}

// 获取当前光标位置
void vga_get_cursor(uint16_t* row, uint16_t* col) {
    *row = cursor_row;
    *col = cursor_col;
}

// 设置光标位置
void vga_set_cursor(uint16_t row, uint16_t col) {
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        cursor_row = row;
        cursor_col = col;
        vga_update_cursor();
    }
}