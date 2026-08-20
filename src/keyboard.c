#include "keyboard.h"
#include "io.h"
#include "stddef.h"

#define BUFFER_SIZE 32

// 当前使用的扫描码集 (1 或 2)
static int current_scancode_set = 1;

// 扫描码集 1 (make code) 到 ASCII, 0 表示不可打印/未映射
// 0x47-0x53 是小键盘, 按 Num Lock 打开时的字符映射
static const char scancode_ascii_set1[128] = {
    0,    27,  '1', '2', '3', '4',  '5', '6',
    '7',  '8', '9', '0', '-', '=',  '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y',  'u', 'i',
    'o',  'p', '[', ']', '\n', 0,   'a', 's',
    'd',  'f', 'g', 'h', 'j', 'k',  'l', ';',
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b',  'n', 'm', ',', '.', '/',  0,   '*',
    0,    ' ', 0,   0,   0,   0,    0,   0,
    0,    0,   0,   0,   0,   0,    0,   '7',
    '8',  '9', '-', '4', '5', '6',  '+', '1',
    '2',  '3', '0', '.',
};

// 扫描码集 2 (make code) 到 ASCII, 0 表示不可打印/未映射
static const char scancode_ascii_set2[128] = {
    0,    0,   '1', '2', '3', '4',  '5', '6',
    '7',  '8', '9', '0', '-', '=',  '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y',  'u', 'i',
    'o',  'p', '[', ']', '\n', 0,   'a', 's',
    'd',  'f', 'g', 'h', 'j', 'k',  'l', ';',
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b',  'n', 'm', ',', '.', '/',  0,   '*',
    0,    ' ', 0,   0,   0,   0,    0,   0,
    0,    0,   0,   0,   0,   0,    0,   '7',
    '8',  '9', '-', '4', '5', '6',  '+', '1',
    '2',  '3', '0', '.',
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

// 等待键盘控制器准备好接收数据
static void keyboard_wait_write(void) {
    while (inb(KEYBOARD_COMMAND_PORT) & 0x02) {
        __asm__ volatile ("nop");
    }
}

// 等待键盘数据准备好读取
static void keyboard_wait_read(void) {
    while (!(inb(KEYBOARD_COMMAND_PORT) & 0x01)) {
        __asm__ volatile ("nop");
    }
}

// 向键盘控制器发送命令
static void keyboard_send_controller_command(uint8_t cmd) {
    keyboard_wait_write();
    outb(KEYBOARD_COMMAND_PORT, cmd);
}

// 向键盘发送数据
static void keyboard_send_data(uint8_t data) {
    keyboard_wait_write();
    outb(KEYBOARD_DATA_PORT, data);
}

// 向键盘发送命令并读取响应
static uint8_t keyboard_send_command_with_response(uint8_t cmd) {
    keyboard_send_data(cmd);
    keyboard_wait_read();
    return inb(KEYBOARD_DATA_PORT);
}

void keyboard_set_scancode_set(int set) {
    if (set < 1 || set > 2) {
        return;  // 只支持扫描码集1和2
    }

    // 禁用键盘
    keyboard_send_controller_command(0xAD);
    
    // 发送设置扫描码集命令 (0xF0)
    keyboard_send_data(0xF0);
    
    // 发送要设置的扫描码集
    keyboard_send_data(set);
    
    // 读取响应
    keyboard_wait_read();
    inb(KEYBOARD_DATA_PORT);  // 丢弃ACK
    
    // 启用键盘
    keyboard_send_controller_command(0xAE);
    
    current_scancode_set = set;
}

int keyboard_get_scancode_set(void) {
    return current_scancode_set;
}

void keyboard_irq(void) {
    static uint8_t extended = 0;
    static uint8_t release = 0;  // 扫描集2的释放标志
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode == 0xE0) {  // 扩展码前缀, 真正的键在下一个字节
        extended = 1;
        return;
    }

    if (current_scancode_set == 2 && scancode == 0xF0) {  // 扫描集2的断码前缀
        release = 1;
        return;
    }

    uint8_t was_extended = extended;
    extended = 0;

    if (current_scancode_set == 1 && (scancode & 0x80)) {  // 扫描集1的断码: 松开按键
        return;
    }

    if (current_scancode_set == 2 && release) {  // 扫描集2的断码
        release = 0;
        return;
    }

    char c;
    if (was_extended) {
        // 扩展码里只有小键盘的 Enter 和 / 是可打印的
        c = scancode == 0x1C ? '\n' : (scancode == 0x35 ? '/' : 0);
    } else {
        // 根据当前扫描码集选择映射表
        const char *scancode_ascii = (current_scancode_set == 1) ? 
                                      scancode_ascii_set1 : scancode_ascii_set2;
        c = scancode_ascii[scancode & 0x7F];
    }
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
