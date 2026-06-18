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
static int num_lock = 1;  // 默认开启
static int extended_key = 0;

static char scan_to_ascii(uint8_t scancode, int is_extended) {
    if (scancode >= 128) return 0;
    
    // ===== 方向键（带 0xE0 前缀）→ 过滤 =====
    if (is_extended) {
        if (scancode == 0x48 || scancode == 0x50 ||
            scancode == 0x4B || scancode == 0x4D) {
            return 0;
        }
        if (scancode == 0x47 || scancode == 0x4F || scancode == 0x49 ||
            scancode == 0x51 || scancode == 0x52 || scancode == 0x53) {
            return 0;
        }
    }
    
    // Backspace
    if (scancode == 0x0E) return '\b';
    if (scancode == 0x1C) return '\n';
    if (scancode == 0x0F) return 4;  // Tab → 4 个空格
    if (scancode == 0x39) return ' ';
    if (scancode == 0x01) return 0;  // ESC
    
    // ===== 小键盘（NumLock 控制） =====
    if (!is_extended) {
        if (num_lock) {
            if (scancode == 0x47) return '7';
            if (scancode == 0x48) return '8';
            if (scancode == 0x49) return '9';
            if (scancode == 0x4B) return '4';
            if (scancode == 0x4C) return '5';
            if (scancode == 0x4D) return '6';
            if (scancode == 0x4F) return '1';
            if (scancode == 0x50) return '2';
            if (scancode == 0x51) return '3';
            if (scancode == 0x52) return '0';
            if (scancode == 0x53) return '.';
            if (scancode == 0x37) return '*';
            if (scancode == 0x35) return '/';
            if (scancode == 0x4A) return '-';
            if (scancode == 0x4E) return '+';
        } else {
            if (scancode == 0x47 || scancode == 0x48 || scancode == 0x49 ||
                scancode == 0x4B || scancode == 0x4C || scancode == 0x4D ||
                scancode == 0x4F || scancode == 0x50 || scancode == 0x51 ||
                scancode == 0x52 || scancode == 0x53) {
                return 0;
            }
            if (scancode == 0x37) return '*';
            if (scancode == 0x35) return '/';
            if (scancode == 0x4A) return '-';
            if (scancode == 0x4E) return '+';
        }
    }
    
    static char normal_map[128] = {
        0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,  'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    };
    
    static char shift_map[128] = {
        0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,   0,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,  'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    };
    
    char c;
    
    if (caps_lock && scancode >= 0x10 && scancode <= 0x2C) {
        c = shift_map[scancode];
        if (c >= 'A' && c <= 'Z') {
            if (!shift_pressed) {
                return c;
            }
        }
    }
    
    if (shift_pressed) {
        c = shift_map[scancode];
        if (c) return c;
    }
    
    return normal_map[scancode];
}

void keyboard_handler(void) {
    uint8_t scancode;
    __asm__ volatile ("inb $0x60, %0" : "=a"(scancode));
    
    if (scancode == 0xE0) {
        extended_key = 1;
        return;
    }
    
    if (scancode == 0x2A || scancode == 0x36) shift_pressed = 1;
    if (scancode == 0xAA || scancode == 0xB6) shift_pressed = 0;
    if (scancode == 0x3A) caps_lock = !caps_lock;
    if (scancode == 0x45) num_lock = !num_lock;
    
    if (scancode < 128) {
        char c = scan_to_ascii(scancode, extended_key);
        extended_key = 0;
        
        if (c == 4) {  // Tab → 4 个空格
            for (int i = 0; i < 4; i++) {
                int next = (kb_head + 1) % BUFFER_SIZE;
                if (next != kb_tail) {
                    kb_buffer[kb_head] = ' ';
                    kb_head = next;
                }
            }
        } else if (c) {
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
    num_lock = 1;
    extended_key = 0;
    
    while (1) {
        uint8_t status;
        __asm__ volatile ("inb $0x64, %0" : "=a"(status));
        if (!(status & 0x01)) break;
        uint8_t dummy;
        __asm__ volatile ("inb $0x60, %0" : "=a"(dummy));
    }
    
    print_info("Keyboard driver initialized\n");
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