#include "stdio.h"
#include "keyboard.h"
#include "vga.h"
#include "stdarg.h"

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

// 简单整数转字符串
static void itoa(int num, char* str) {
    int i = 0;
    int sign = 0;
    if (num < 0) {
        sign = 1;
        num = -num;
    }
    do {
        str[i++] = '0' + (num % 10);
        num /= 10;
    } while (num);
    if (sign) str[i++] = '-';
    str[i] = '\0';
    // 反转
    for (int j = 0; j < i / 2; j++) {
        char tmp = str[j];
        str[j] = str[i - 1 - j];
        str[i - 1 - j] = tmp;
    }
}

// 简易 printf（支持 %-10s 左对齐）
void printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    for (int i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] == '%') {
            i++;
            
            // 解析标志：是否左对齐
            int left_align = 0;
            int width = 0;
            while (fmt[i] == '-') {
                left_align = 1;
                i++;
            }
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                width = width * 10 + (fmt[i] - '0');
                i++;
            }
            
            switch (fmt[i]) {
                case 's': {
                    const char* s = va_arg(args, const char*);
                    int len = 0;
                    while (s[len]) len++;
                    
                    // 如果指定了宽度，补空格
                    if (width > len) {
                        int padding = width - len;
                        if (left_align) {
                            // 左对齐：先打印内容，再补空格
                            print_info(s);
                            for (int j = 0; j < padding; j++) putchar(' ');
                        } else {
                            // 右对齐（默认）：先补空格，再打印内容
                            for (int j = 0; j < padding; j++) putchar(' ');
                            print_info(s);
                        }
                    } else {
                        print_info(s);
                    }
                    break;
                }
                case 'd': {
                    int n = va_arg(args, int);
                    char buf[16];
                    itoa(n, buf);
                    print_info(buf);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    putchar(c);
                    break;
                }
                case 'x': {
                    unsigned int n = va_arg(args, unsigned int);
                    char buf[16];
                    int j = 0;
                    if (n == 0) {
                        buf[j++] = '0';
                    } else {
                        const char* hex = "0123456789ABCDEF";
                        while (n) {
                            buf[j++] = hex[n & 0xF];
                            n >>= 4;
                        }
                    }
                    buf[j] = '\0';
                    for (int k = 0; k < j / 2; k++) {
                        char tmp = buf[k];
                        buf[k] = buf[j - 1 - k];
                        buf[j - 1 - k] = tmp;
                    }
                    print_info(buf);
                    break;
                }
                case '%':
                    putchar('%');
                    break;
                default:
                    putchar('%');
                    putchar(fmt[i]);
                    break;
            }
        } else {
            putchar(fmt[i]);
        }
    }
    
    va_end(args);
}

// ========== scanf 简化版 ==========

static char input_buffer[256];
static int input_pos = 0;

// 从键盘读取一行到缓冲区
static void read_line(void) {
    int i = 0;
    while (i < sizeof(input_buffer) - 1) {
        char c = getchar();
        if (c == '\n') {
            input_buffer[i] = '\0';
            break;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c >= 0x20 && c < 0x7F) {
            input_buffer[i++] = c;
            putchar(c);
        }
    }
    input_pos = 0;
}

// 跳过空白
static void skip_whitespace_input(void) {
    while (input_buffer[input_pos] == ' ' || 
           input_buffer[input_pos] == '\t' ||
           input_buffer[input_pos] == '\n') {
        input_pos++;
    }
}

// 读取一个整数
static int read_int_from_buffer(void) {
    skip_whitespace_input();
    int sign = 1;
    int num = 0;
    
    if (input_buffer[input_pos] == '-') {
        sign = -1;
        input_pos++;
    }
    
    while (input_buffer[input_pos] >= '0' && input_buffer[input_pos] <= '9') {
        num = num * 10 + (input_buffer[input_pos] - '0');
        input_pos++;
    }
    
    return sign * num;
}

// 读取一个字符串
static void read_string_from_buffer(char* buffer, int max_len) {
    skip_whitespace_input();
    int i = 0;
    while (input_buffer[input_pos] != '\0' &&
           input_buffer[input_pos] != ' ' &&
           input_buffer[input_pos] != '\t' &&
           input_buffer[input_pos] != '\n' &&
           i < max_len - 1) {
        buffer[i++] = input_buffer[input_pos++];
    }
    buffer[i] = '\0';
}

// 读取一个字符
static char read_char_from_buffer(void) {
    skip_whitespace_input();
    return input_buffer[input_pos++];
}

int vscanf(const char* fmt, va_list args) {
    // 先读取一行输入
    read_line();
    
    int count = 0;
    const char* p = fmt;
    
    while (*p) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 'd': {
                    int* ptr = va_arg(args, int*);
                    *ptr = read_int_from_buffer();
                    count++;
                    break;
                }
                case 's': {
                    char* ptr = va_arg(args, char*);
                    read_string_from_buffer(ptr, 256);
                    count++;
                    break;
                }
                case 'c': {
                    char* ptr = va_arg(args, char*);
                    *ptr = read_char_from_buffer();
                    count++;
                    break;
                }
                default:
                    break;
            }
            p++;
        } else if (*p == ' ' || *p == '\t' || *p == '\n') {
            p++;
        } else {
            // 匹配字面字符
            if (input_buffer[input_pos] == *p) {
                input_pos++;
                p++;
            } else {
                break;
            }
        }
    }
    
    return count;
}

// scanf - 可变参数版本
int scanf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vscanf(fmt, args);
    va_end(args);
    return result;
}

// sscanf - 从字符串解析（简化版）
int sscanf(const char* str, const char* fmt, ...) {
    // 这个实现较为复杂，暂不实现
    // 可以用类似 vscanf 的方式，从字符串读取而不是键盘
    return 0;
}
