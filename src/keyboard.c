#include "keyboard.h"
#include "stdio.h"

static int shift_pressed = 0;
static int caps_lock = 0;

static char scan_normal[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,   'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static char scan_shift[128] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,   0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,   'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

char scan_to_ascii(uint8_t scancode) {
    if (scancode >= 128) return 0;
    if (scancode == 0x0E) return '\b';
    
    // 小键盘
    if (scancode == 0x47) return '7';
    if (scancode == 0x48) return '8';
    if (scancode == 0x49) return '9';
    if (scancode == 0x4A) return '-';
    if (scancode == 0x4B) return '4';
    if (scancode == 0x4C) return '5';
    if (scancode == 0x4D) return '6';
    if (scancode == 0x4E) return '+';
    if (scancode == 0x4F) return '1';
    if (scancode == 0x50) return '2';
    if (scancode == 0x51) return '3';
    if (scancode == 0x52) return '0';
    if (scancode == 0x53) return '.';
    if (scancode == 0x37) return '*';
    if (scancode == 0x35) return '/';
    
    if (shift_pressed) {
        char c = scan_shift[scancode];
        if (c) return c;
    }
    if (caps_lock && scancode >= 0x10 && scancode <= 0x2C) {
        char c = scan_shift[scancode];
        if (c >= 'A' && c <= 'Z') return c;
    }
    return scan_normal[scancode];
}

void keyboard_init(void) {
    shift_pressed = 0;
    caps_lock = 0;
}

int keyboard_has_key(void) {
    uint8_t status;
    __asm__ volatile ("inb $0x64, %0" : "=a"(status));
    return status & 0x01;
}

char keyboard_getchar(void) {
    while (1) {
        uint8_t status;
        __asm__ volatile ("inb $0x64, %0" : "=a"(status));
        if (status & 0x01) {
            uint8_t scancode;
            __asm__ volatile ("inb $0x60, %0" : "=a"(scancode));
            
            if (scancode == 0x2A || scancode == 0x36) shift_pressed = 1;
            if (scancode == 0xAA || scancode == 0xB6) shift_pressed = 0;
            if (scancode == 0x3A) caps_lock = !caps_lock;
            
            if (scancode < 128) {
                char c = scan_to_ascii(scancode);
                if (c) return c;
            }
        }
    }
}