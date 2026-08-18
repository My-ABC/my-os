#include "keyboard.h"
#include "io.h"
#include "stddef.h"

#define BUFFER_SIZE 32

// 扫描码集 1 (make code) 到 ASCII, 0 表示不可打印/未映射
static const char scancode_ascii[128] = {
    0,    27,  '1', '2', '3', '4',  '5', '6',
    '7',  '8', '9', '0', '-', '=',  '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y',  'u', 'i',
    'o',  'p', '[', ']', '\n', 0,   'a', 's',
    'd',  'f', 'g', 'h', 'j', 'k',  'l', ';',
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b',  'n', 'm', ',', '.', '/',  0,   '*',
    0,    ' ',
};

static volatile char buffer[BUFFER_SIZE];
static volatile uint32_t head = 0;
static volatile uint32_t tail = 0;

void keyboard_init(void) {
    head = 0;
    tail = 0;

    // 丢掉上电后残留在输出缓冲区里的字节, 否则第一次 IRQ1 可能不会到来
    while (inb(0x64) & 0x01) {
        inb(KEYBOARD_DATA_PORT);
    }
}

void keyboard_irq(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode & 0x80) {  // break code: 松开按键
        return;
    }

    char c = scancode_ascii[scancode & 0x7F];
    if (c == 0) {
        return;
    }

    uint32_t next = (head + 1) % BUFFER_SIZE;
    if (next != tail) {  // 满了就丢弃, 不覆盖旧按键
        buffer[head] = c;
        head = next;
    }
}

char keyboard_getchar(void) {
    if (head == tail) {
        return 0;
    }

    char c = buffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    return c;
}

char keyboard_wait_key(void) {
    while (1) {
        char c = keyboard_getchar();
        if (c != 0) {
            return c;
        }
        __asm__ volatile ("hlt");  // 等中断, 不空转烧 CPU
    }
}
