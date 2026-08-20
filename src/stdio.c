#include "stdio.h"
#include "stddef.h"
#include "vga.h"
#include "serial.h"
#include "stdarg.h"
#include "keyboard.h"

// Helper function to convert integer to string
static int int_to_str(char* str, int value, int base) {
    char* p = str;
    char* p1, *p2;
    unsigned int ud = value;
    int divisor = 10;
    
    // Handle negative numbers
    if (base == 10 && value < 0) {
        *p++ = '-';
        str++;
        ud = -value;
    } else if (base == 16) {
        divisor = 16;
    }
    
    // Convert to string
    do {
        int remainder = ud % divisor;
        *p++ = (remainder < 10) ? (remainder + '0') : (remainder - 10 + 'A');
        ud /= divisor;
    } while (ud);
    
    // Terminate string
    *p = '\0';
    
    // Reverse the string
    p1 = str;
    p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1 = *p2;
        *p2 = tmp;
        p1++;
        p2--;
    }
    
    return p - str;
}

// Helper function to convert integer to string with zero padding
static int int_to_str_padded(char* str, int value, int base, int width, char pad_char) {
    char temp[32];
    char* p = temp;
    char* p1, *p2;
    unsigned int ud = value;
    int divisor = 10;
    int is_negative = 0;
    
    // Handle negative numbers
    if (base == 10 && value < 0) {
        is_negative = 1;
        ud = -value;
    } else if (base == 16) {
        divisor = 16;
    }
    
    // Convert to string (reversed)
    do {
        int remainder = ud % divisor;
        *p++ = (remainder < 10) ? (remainder + '0') : (remainder - 10 + 'A');
        ud /= divisor;
    } while (ud);
    
    int num_length = p - temp;
    
    // Reverse the number to get correct order
    p1 = temp;
    p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1 = *p2;
        *p2 = tmp;
        p1++;
        p2--;
    }
    
    // Calculate padding needed
    int total_length = num_length;
    if (is_negative) {
        total_length++; // For the minus sign
    }
    
    int padding = width - total_length;
    if (padding < 0) padding = 0;
    
    // Build final string
    char* dest = str;
    
    // Add negative sign if needed
    if (is_negative) {
        *dest++ = '-';
    }
    
    // Add padding
    for (int i = 0; i < padding; i++) {
        *dest++ = pad_char;
    }
    
    // Add the number
    for (int i = 0; i < num_length; i++) {
        *dest++ = temp[i];
    }
    
    *dest = '\0';
    
    return dest - str;
}

// Output function pointer type
typedef void (*output_func)(char);

// Generic vsprintf function
static int vsprintf_helper(char* str, const char* format, va_list args, output_func output) {
    char buffer[32];
    int count = 0;
    
    while (*format) {
        if (*format == '%') {
            format++;
            
            // Check for width specifier (e.g., %04d)
            int width = 0;
            char pad_char = ' ';
            if (*format == '0') {
                pad_char = '0';
                format++;
            }
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
            
            switch (*format) {
                case 'd': {
                    int value = va_arg(args, int);
                    int len;
                    if (width > 0) {
                        len = int_to_str_padded(buffer, value, 10, width, pad_char);
                    } else {
                        len = int_to_str(buffer, value, 10);
                    }
                    if (str) {
                        for (int i = 0; i < len; i++) {
                            *str++ = buffer[i];
                        }
                    } else if (output) {
                        for (int i = 0; i < len; i++) {
                            output(buffer[i]);
                        }
                    }
                    count += len;
                    break;
                }
                case 'x': {
                    unsigned int value = va_arg(args, unsigned int);
                    int len;
                    if (width > 0) {
                        len = int_to_str_padded(buffer, value, 16, width, pad_char);
                    } else {
                        len = int_to_str(buffer, value, 16);
                    }
                    if (str) {
                        for (int i = 0; i < len; i++) {
                            *str++ = buffer[i];
                        }
                    } else if (output) {
                        for (int i = 0; i < len; i++) {
                            output(buffer[i]);
                        }
                    }
                    count += len;
                    break;
                }
                case 's': {
                    char* s = va_arg(args, char*);
                    if (s) {
                        int len = 0;
                        while (s[len]) {
                            if (str) {
                                *str++ = s[len];
                            } else if (output) {
                                output(s[len]);
                            }
                            len++;
                        }
                        count += len;
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    if (str) {
                        *str++ = c;
                    } else if (output) {
                        output(c);
                    }
                    count++;
                    break;
                }
                case '%': {
                    if (str) {
                        *str++ = '%';
                    } else if (output) {
                        output('%');
                    }
                    count++;
                    break;
                }
                default:
                    // Unknown format specifier, just print the character
                    if (str) {
                        *str++ = *format;
                    } else if (output) {
                        output(*format);
                    }
                    count++;
                    break;
            }
            format++;
        } else {
            if (str) {
                *str++ = *format;
            } else if (output) {
                output(*format);
            }
            count++;
            format++;
        }
    }
    
    if (str) {
        *str = '\0';
    }
    
    return count;
}

// VGA output function
static void vga_output(char c) {
    vga_putchar(c);
}

// Serial output function
static void serial_output(char c) {
    serial_putchar(c);
}

int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int count = vsprintf_helper(NULL, format, args, vga_output);
    va_end(args);
    return count;
}

int sprintf(char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int count = vsprintf_helper(str, format, args, NULL);
    va_end(args);
    return count;
}

int serial_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int count = vsprintf_helper(NULL, format, args, serial_output);
    va_end(args);
    return count;
}

int putchar(char c) {
    vga_putchar(c);
    return c;
}

int puts(const char* str) {
    vga_print(str);
    vga_putchar('\n');
    return 0;
}

int getchar(void) {
    return keyboard_wait_key();
}

char *gets(char *buf, int size) {
    int i = 0;
    while (i < size - 1) {
        char c = getchar();
        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            putchar('\n');
            return buf;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else {
            buf[i++] = c;
            putchar(c);
        }
    }
    buf[i] = '\0';
    return buf;
}