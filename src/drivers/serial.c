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

    outb(SERIAL_COM3 + 1, 0x00);
    outb(SERIAL_COM3 + 3, 0x80);
    outb(SERIAL_COM3 + 0, 0x03);
    outb(SERIAL_COM3 + 1, 0x00);
    outb(SERIAL_COM3 + 3, 0x03);
    outb(SERIAL_COM3 + 2, 0xC7);
    outb(SERIAL_COM3 + 4, 0x0B);
}

/* Bounded LSR wait: a missing UART must not hang the kernel. ~1M port
 * reads is far longer than any FIFO drain at 38400 baud. */
#define SERIAL_TX_TIMEOUT 0x100000U

static int serial3_tx_timeout_warned;

/* Returns 1 when the THR is ready, 0 on timeout (caller drops). */
static int serial_tx_wait(uint16_t lsr_port) {
    for (uint32_t i = 0; i < SERIAL_TX_TIMEOUT; ++i) {
        if (inb(lsr_port) & 0x20) return 1;
    }
    return 0;
}

void serial_putchar(char c) {
    if (c == '\n') {
        serial_putchar('\r');
    }
    if (!serial_tx_wait(SERIAL_COM1 + 5)) return;
    outb(SERIAL_COM1, (uint8_t)c);
}

void serial3_putchar(char c) {
    if (c == '\n') {
        c = '\r';
        if (!serial_tx_wait(SERIAL_COM3 + 5)) goto dropped;
        outb(SERIAL_COM3, (uint8_t)c);
        c = '\n';
    }
    if (!serial_tx_wait(SERIAL_COM3 + 5)) goto dropped;
    outb(SERIAL_COM3, (uint8_t)c);
    return;
dropped:
    if (!serial3_tx_timeout_warned) {
        serial3_tx_timeout_warned = 1;
        serial_print("[SER] COM3 TX timeout, output dropped\n");
    }
}

void serial3_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial3_putchar(str[i]);
    }
}

void serial_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial_putchar(str[i]);
    }
}

void serial_flush(void) {
    /* LSR bit 6 (TEMT): transmitter holding register and FIFO empty —
     * everything queued has actually left the UART. */
    for (uint32_t i = 0; i < SERIAL_TX_TIMEOUT; ++i) {
        if (inb(SERIAL_COM1 + 5) & 0x40) return;
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
