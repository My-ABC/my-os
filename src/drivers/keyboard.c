#include "keyboard.h"
#include "io.h"
#include "stddef.h"
#include "ring3.h"
#include "signal.h"
#include "vt.h"

/* Host-side driver tests link keyboard.c without the process/signal
 * or vt objects; weak stubs keep Ctrl+C/F-keys inert there. The
 * kernel's strong definitions override these. */
__attribute__((weak)) uint32_t ring3_foreground_pid_vt(int vt) {
    (void)vt;
    return 0U;
}
__attribute__((weak)) int vt_active_index(void) {
    return 0;
}
__attribute__((weak)) void vt_request_switch(int vt) {
    (void)vt;
}
__attribute__((weak)) int ring3_signal_send(uint32_t pid, uint32_t sig) {
    (void)pid;
    (void)sig;
    return -1;
}

#define BUFFER_SIZE 256

// 当前使用的扫描码集 (1 或 2)
static int current_scancode_set = 1;

// 修饰键状态
static int shift_pressed = 0;
static int alt_pressed = 0;
static int ctrl_pressed = 0;
static int caps_lock_on = 0;
static int num_lock_on = 0;

// 扫描码集 1 (make code) 到 ASCII, 0 表示不可打印/未映射
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

// Shift 符号映射表
static const char shift_symbols[128] = {
    ['1'] = '!', ['2'] = '@', ['3'] = '#', ['4'] = '$',
    ['5'] = '%', ['6'] = '^', ['7'] = '&', ['8'] = '*',
    ['9'] = '(', ['0'] = ')', ['-'] = '_', ['='] = '+',
    ['['] = '{', [']'] = '}', ['\\'] = '|', [';'] = ':',
    ['\''] = '"', [','] = '<', ['.'] = '>', ['/'] = '?',
    ['`'] = '~'
};

/* One ring buffer per virtual terminal: keystrokes always land in the
 * visible terminal's queue, so an inactive shell cannot steal input
 * meant for the terminal on screen. */
static volatile char buffer[VT_COUNT][BUFFER_SIZE];
static volatile uint32_t head[VT_COUNT];
static volatile uint32_t tail[VT_COUNT];

// 转换字符（处理 Shift、Caps Lock）
static char apply_modifiers(char c, int shift, int caps) {
    if (c >= 'a' && c <= 'z') {
        if (shift || caps) {
            return c - 'a' + 'A';
        }
        return c;
    }
    if (shift && c >= '1' && c <= '9') {
        return shift_symbols[(int)c];
    }
    if (shift) {
        // 处理其他符号
        switch (c) {
            case '0': return ')';
            case '-': return '_';
            case '=': return '+';
            case '[': return '{';
            case ']': return '}';
            case '\\': return '|';
            case ';': return ':';
            case '\'': return '"';
            case ',': return '<';
            case '.': return '>';
            case '/': return '?';
            case '`': return '~';
        }
    }
    return c;
}

void keyboard_init(void) {
    for (int i = 0; i < VT_COUNT; ++i) {
        head[i] = 0;
        tail[i] = 0;
    }
    shift_pressed = 0;
    alt_pressed = 0;
    ctrl_pressed = 0;
    caps_lock_on = 0;
    num_lock_on = 0;
    current_scancode_set = 1;

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
        return;
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

    if (scancode == 0xE0) {  // 扩展码前缀
        extended = 1;
        return;
    }

    if (current_scancode_set == 2 && scancode == 0xF0) {  // 扫描集2的断码前缀
        release = 1;
        return;
    }

    uint8_t was_extended = extended;
    extended = 0;

    // 处理修饰键（扫描集1和2通用）
    // 左 Shift: 0x2A, 右 Shift: 0x36
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }

    // Caps Lock: 0x3A (按下), 0xBA (释放)
    if (scancode == 0x3A) {
        caps_lock_on = !caps_lock_on;
        return;
    }

    // Num Lock: 0x45 (按下), 0xC5 (释放)
    if (scancode == 0x45) {
        num_lock_on = !num_lock_on;
        return;
    }

    // Ctrl: 扫描集1 0x1D/0x9D, 扫描集2 0x14 (F0 0x14 为释放)
    if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return;
    }
    if (scancode == 0x9D) {
        ctrl_pressed = 0;
        return;
    }
    if (current_scancode_set == 2 && scancode == 0x14) {
        if (release) {
            ctrl_pressed = 0;
            release = 0;
        } else {
            ctrl_pressed = 1;
        }
        return;
    }

    if (current_scancode_set == 1 && (scancode & 0x80)) {  // 扫描集1的断码
        // 检查是否是修饰键释放（已在上面处理）
        return;
    }

    if (current_scancode_set == 2 && release) {  // 扫描集2的断码
        release = 0;
        return;
    }

    /* F1/F2 (scan set 1 make codes) switch virtual terminals. The
     * switch itself is deferred to vt_service() because it copies
     * framebuffer-sized backing stores and must not run inside IRQ1. */
    if (!was_extended && current_scancode_set == 1) {
        if (scancode == 0x3BU) {  // F1
            vt_request_switch(0);
            return;
        }
        if (scancode == 0x3CU) {  // F2
            vt_request_switch(1);
            return;
        }
    }

    char c;
    if (was_extended) {
        // 扩展码（小键盘 Enter, /, 方向键）
        if (scancode == 0x1C) {
            c = '\n';
        } else if (scancode == 0x35) {
            c = '/';
        } else if (scancode == 0x48) {
            c = KEYBOARD_KEY_UP;
        } else if (scancode == 0x50) {
            c = KEYBOARD_KEY_DOWN;
        } else if (scancode == 0x4B) {
            c = KEYBOARD_KEY_LEFT;
        } else if (scancode == 0x4D) {
            c = KEYBOARD_KEY_RIGHT;
        } else {
            c = 0;
        }
    } else {
        const char *scancode_ascii = (current_scancode_set == 1) ? 
                                      scancode_ascii_set1 : scancode_ascii_set2;
        c = scancode_ascii[scancode & 0x7F];
    }
    if (c == 0) {
        return;
    }

    // 应用 Shift 和 Caps Lock 转换
    c = apply_modifiers(c, shift_pressed, caps_lock_on);

    // Ctrl+C 产生 SIGINT 控制字节而不是字符 'c'
    if (ctrl_pressed && (c == 'c' || c == 'C')) {
        c = KEYBOARD_KEY_SIGINT;
        /* A foreground Ring 3 process owns the console, so the kernel
         * shell is halted and may never read this byte (the child is
         * not required to read keys). Deliver SIGINT to the console
         * owner immediately; the byte stays buffered for whichever
         * reader consumes it next. */
        uint32_t fg = ring3_foreground_pid_vt(vt_active_index());
        if (fg != 0U) {
            ring3_signal_send(fg, SIGINT);
        }
    }

    int vt = vt_active_index();
    uint32_t next = (head[vt] + 1) % BUFFER_SIZE;
    if (next != tail[vt]) {
        buffer[vt][head[vt]] = c;
        head[vt] = next;
    }
}

char keyboard_getchar_vt(int vt) {
    if (vt < 0 || vt >= VT_COUNT) vt = 0;
    if (head[vt] == tail[vt]) {
        return 0;
    }
    char c = buffer[vt][tail[vt]];
    tail[vt] = (tail[vt] + 1) % BUFFER_SIZE;
    return c;
}

char keyboard_getchar(void) {
    return keyboard_getchar_vt(vt_active_index());
}

int keyboard_hit_vt(int vt) {
    if (vt < 0 || vt >= VT_COUNT) vt = 0;
    return head[vt] != tail[vt];
}

int keyboard_hit(void) {
    return keyboard_hit_vt(vt_active_index());
}

char keyboard_wait_key(void) {
    while (1) {
        char c = keyboard_getchar();
        if (c != 0) {
            return c;
        }
        __asm__ volatile ("hlt");
    }
}