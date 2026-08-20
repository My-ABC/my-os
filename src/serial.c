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

void serial_print_hex(uint32_t num) {
    serial_print("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_putchar("0123456789ABCDEF"[(num >> i) & 0xF]);
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

// 检查是否有数据可读
static int serial_is_data_available(void) {
    return inb(SERIAL_COM1 + 5) & 0x01;  // 检查 LSR 的 bit 0（数据就绪位）
}

// 从串口读取一个字符（非阻塞）
int serial_getchar(void) {
    if (!serial_is_data_available()) {
        return -1;  // 无数据可读
    }
    return inb(SERIAL_COM1);  // 读取数据端口
}

// 从串口读取一个字符（阻塞）
int serial_wait_char(void) {
    while (!serial_is_data_available()) {
        __asm__ volatile ("pause");  // 等待数据就绪
    }
    return inb(SERIAL_COM1);
}

// 从串口读取一行字符串（阻塞）
void serial_gets(char *buffer, int size) {
    int i = 0;
    while (i < size - 1) {
        char c = serial_wait_char();
        if (c == '\r' || c == '\n') {
            buffer[i] = '\0';
            serial_putchar('\n');  // 回显换行
            break;
        } else if (c == '\b' || c == 0x7F) {  // 退格
            if (i > 0) {
                i--;
                serial_putchar('\b');
                serial_putchar(' ');
                serial_putchar('\b');
            }
        } else {
            buffer[i++] = c;
            serial_putchar(c);  // 回显
        }
    }
    buffer[i] = '\0';
}