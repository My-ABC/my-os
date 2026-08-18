#include "serial.h"
#include "io.h"

void serial_init(void) {
    outb(SERIAL_COM1 + 1, 0x00);  // 关闭中断
    outb(SERIAL_COM1 + 3, 0x80);  // 打开波特率除数锁存
    outb(SERIAL_COM1 + 0, 0x03);  // 除数低字节: 38400 baud
    outb(SERIAL_COM1 + 1, 0x00);  // 除数高字节
    outb(SERIAL_COM1 + 3, 0x03);  // 8 位数据, 无校验, 1 位停止位
    outb(SERIAL_COM1 + 2, 0xC7);  // 启用 FIFO, 清空, 14 字节阈值
    outb(SERIAL_COM1 + 4, 0x0B);  // IRQ 使能, RTS/DSR 置位
}

static int serial_is_transmit_empty(void) {
    return inb(SERIAL_COM1 + 5) & 0x20;
}

void serial_putchar(char c) {
    if (c == '\n') {
        serial_putchar('\r');
    }
    while (!serial_is_transmit_empty()) {
    }
    outb(SERIAL_COM1, (uint8_t)c);
}

void serial_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial_putchar(str[i]);
    }
}

void serial_print_dec(uint32_t num) {
    if (num == 0) {
        serial_putchar('0');
        return;
    }

    char buffer[12];
    int i = 0;
    while (num > 0) {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    }

    while (i > 0) {
        serial_putchar(buffer[--i]);
    }
}
