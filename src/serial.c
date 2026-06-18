#include "serial.h"
#include "stdio.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(int port) {
    // 禁用中断
    outb(port + 1, 0x00);
    
    // 设置波特率 (115200)
    outb(port + 3, 0x80);  // DLAB = 1
    outb(port + 0, 0x01);  // 低字节
    outb(port + 1, 0x00);  // 高字节
    outb(port + 3, 0x03);  // DLAB = 0, 8N1
    
    // 启用 FIFO
    outb(port + 2, 0xC7);
    
    // 启用 IRQ
    outb(port + 4, 0x0B);
    
    print_info("Serial initialized on port 0x");
    char buf[16];
    int n = port;
    if (n < 10) {
        buf[0] = '0' + n;
        buf[1] = '\0';
    } else if (n < 100) {
        buf[0] = '0' + (n / 10);
        buf[1] = '0' + (n % 10);
        buf[2] = '\0';
    } else if (n < 1000) {
        buf[0] = '0' + (n / 100);
        buf[1] = '0' + ((n / 10) % 10);
        buf[2] = '0' + (n % 10);
        buf[3] = '\0';
    } else {
        buf[0] = '0' + (n / 1000);
        buf[1] = '0' + ((n / 100) % 10);
        buf[2] = '0' + ((n / 10) % 10);
        buf[3] = '0' + (n % 10);
        buf[4] = '\0';
    }
    print_info(buf);
    print_info("\n");
}

int serial_has_data(int port) {
    return inb(port + 5) & 1;
}

char serial_getchar(int port) {
    while (!serial_has_data(port));
    return inb(port);
}

void serial_putchar(int port, char c) {
    // 等待发送缓冲区空
    while ((inb(port + 5) & 0x20) == 0);
    outb(port, c);
}

void serial_write(int port, const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            serial_putchar(port, '\r');
        }
        serial_putchar(port, str[i]);
    }
}