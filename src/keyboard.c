#include "keyboard.h"
#include "stdio.h"
#include "isr.h"

#define KEYBOARD_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define BUFFER_SIZE 128

static char kb_buffer[BUFFER_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

static int shift_pressed = 0;
static int caps_lock = 0;

// 小键盘硬编码
static char scan_to_ascii(uint8_t scancode) {
    if (scancode >= 128) return 0;
    
    if (scancode == 0x0E) return '\b';
    if (scancode == 0x1C) return '\n';
    if (scancode == 0x39) return ' ';
    if (scancode == 0x01) return 0;  // ESC
    
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
    
    // 主键盘
    static char normal_map[] = {
        0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,  'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    };
    
    static char shift_map[] = {
        0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,   0,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,  'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    };
    
    if (shift_pressed) {
        return shift_map[scancode];
    }
    if (caps_lock && scancode >= 0x10 && scancode <= 0x2C) {
        char c = shift_map[scancode];
        if (c >= 'A' && c <= 'Z') return c;
    }
    return normal_map[scancode];
}

// 键盘中断处理函数（由 IDT 调用）
void keyboard_handler(void) {
    uint8_t scancode;
    __asm__ volatile ("inb $0x60, %0" : "=a"(scancode));
    
    // 处理 Shift 状态
    if (scancode == 0x2A || scancode == 0x36) shift_pressed = 1;
    if (scancode == 0xAA || scancode == 0xB6) shift_pressed = 0;
    if (scancode == 0x3A) caps_lock = !caps_lock;
    
    // 只处理按下事件
    if (scancode < 128) {
        char c = scan_to_ascii(scancode);
        if (c) {
            int next = (kb_head + 1) % BUFFER_SIZE;
            if (next != kb_tail) {
                kb_buffer[kb_head] = c;
                kb_head = next;
            }
        }
    }
}

void keyboard_init(void) {
    kb_head = 0;
    kb_tail = 0;
    shift_pressed = 0;
    caps_lock = 0;
    
    // 清空键盘缓冲区
    while (1) {
        uint8_t status;
        __asm__ volatile ("inb $0x64, %0" : "=a"(status));
        if (!(status & 0x01)) break;
        uint8_t dummy;
        __asm__ volatile ("inb $0x60, %0" : "=a"(dummy));
    }
    
    print_info("Keyboard driver (interrupt mode) initialized\n");
}

int keyboard_has_key(void) {
    return kb_head != kb_tail;
}

char keyboard_getchar(void) {
    while (kb_head == kb_tail) {
        __asm__ volatile ("hlt");
    }
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % BUFFER_SIZE;
    return c;
}