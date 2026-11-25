#include "stdint.h"
#include "stddef.h"
#include "vga.h"
#include "vbe.h"
#include "vt.h"

// VGA 参数
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

/* Per-terminal console state. The active vt renders straight into the
 * physical VGA window; an inactive vt's putchar traffic is redirected
 * to its private cell copy by vt_text_target(), so each terminal keeps
 * its own cursor and colors. */
struct vga_ctx {
    uint16_t cursor_row;
    uint16_t cursor_col;
    uint8_t current_color;
};

static struct vga_ctx vga_ctxs[VT_COUNT];
static volatile uint16_t *vga_target;

// ========== 端口 I/O 函数（放在最前面） ==========
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// ========== 内部函数 ==========
// 内部函数：创建 VGA 条目
static inline uint16_t vga_entry(unsigned char ch, uint8_t color) {
    return (uint16_t)(ch | (color << 8));
}

// 内部函数：获取颜色字节
static inline uint8_t vga_color(enum vga_color fg, enum vga_color bg) {
    return (uint8_t)(fg | (bg << 4));
}

/* 内部函数：更新硬件光标 (仅当该 ctx 属于当前可见终端时写寄存器) */
static void vga_update_cursor(struct vga_ctx *ctx) {
    uint16_t pos = ctx->cursor_row * VGA_WIDTH + ctx->cursor_col;

    // 写入光标位置到 VGA 寄存器
    // 0x3D4 是索引寄存器，0x3D5 是数据寄存器
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* 内部函数：滚动屏幕 */
static void vga_scroll(struct vga_ctx *ctx) {
    volatile uint16_t *target = vga_target;
    // 将第 1 行到第 24 行上移
    for (uint16_t row = 1; row < VGA_HEIGHT; row++) {
        for (uint16_t col = 0; col < VGA_WIDTH; col++) {
            uint16_t index = row * VGA_WIDTH + col;
            uint16_t prev_index = (row - 1) * VGA_WIDTH + col;
            target[prev_index] = target[index];
        }
    }

    // 清空最后一行
    uint16_t last_row = VGA_HEIGHT - 1;
    for (uint16_t col = 0; col < VGA_WIDTH; col++) {
        uint16_t index = last_row * VGA_WIDTH + col;
        target[index] = vga_entry(' ', ctx->current_color);
    }

    // 光标移到最后一行的开始
    ctx->cursor_row = VGA_HEIGHT - 1;
    ctx->cursor_col = 0;
}

/* 解析当前上下文所属终端的状态与渲染目标。内核态输出固定落在
 * VT_FALLBACK (vt 0)；Ring 3 进程输出落在它自己的 tty。 */
static int vga_select(int *vt_out, struct vga_ctx **ctx_out) {
    int vt = vt_render_index();
    if (vt_out != 0) *vt_out = vt;
    *ctx_out = &vga_ctxs[vt];
    vga_target = (volatile uint16_t *)vt_text_target(vt);
    return vt;
}

// ========== 公共函数 ==========
// 初始化 VGA
void vga_init(void) {
    for (int i = 0; i < VT_COUNT; ++i) {
        vga_ctxs[i].current_color = vga_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vga_ctxs[i].cursor_row = 0;
        vga_ctxs[i].cursor_col = 0;
    }
    vga_target = VGA_MEMORY;
    vga_clear();
}

// 清屏
void vga_clear(void) {
    int vt;
    struct vga_ctx *ctx;
    vga_select(&vt, &ctx);
    if (vbe_available()) {
        vbe_clear();
        return;
    }
    for (uint16_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_target[i] = vga_entry(' ', ctx->current_color);
    }
    ctx->cursor_row = 0;
    ctx->cursor_col = 0;
    if (vt == vt_active_index()) {
        vga_update_cursor(ctx);
    }
}

// 设置颜色
void vga_set_color(enum vga_color fg, enum vga_color bg) {
    struct vga_ctx *ctx;
    vga_select(0, &ctx);
    ctx->current_color = vga_color(fg, bg);
    if (vbe_available()) {
        vbe_set_color((uint8_t)fg, (uint8_t)bg);
    }
}

// 打印字符
void vga_putchar(char c) {
    int vt;
    struct vga_ctx *ctx;
    vt = vga_select(&vt, &ctx);
    if (vbe_available()) {
        vbe_putchar(c);
        return;
    }
    if (c == '\n') {
        // 换行
        ctx->cursor_row++;
        ctx->cursor_col = 0;
    } else if (c == '\r') {
        // 回车
        ctx->cursor_col = 0;
    } else if (c == '\t') {
        // 制表符（4个空格）
        do {
            vga_putchar(' ');
        } while (ctx->cursor_col % 4 != 0);
        return;
    } else if (c == '\b') {
        // 退格
        if (ctx->cursor_col > 0) {
            ctx->cursor_col--;
            uint16_t index = ctx->cursor_row * VGA_WIDTH + ctx->cursor_col;
            vga_target[index] = vga_entry(' ', ctx->current_color);
        }
    } else {
        // 普通字符
        uint16_t index = ctx->cursor_row * VGA_WIDTH + ctx->cursor_col;
        vga_target[index] = vga_entry((unsigned char)c, ctx->current_color);
        ctx->cursor_col++;
    }

    // 检查是否需要换行
    if (ctx->cursor_col >= VGA_WIDTH) {
        ctx->cursor_row++;
        ctx->cursor_col = 0;
    }

    // 检查是否需要滚动
    if (ctx->cursor_row >= VGA_HEIGHT) {
        vga_scroll(ctx);
    }

    // 更新硬件光标 (仅可见终端)
    if (vt == vt_active_index()) {
        vga_update_cursor(ctx);
    }
}

// 打印字符串
void vga_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i]);
    }
}

// 打印带颜色的字符串
void vga_print_color(const char* str, enum vga_color fg, enum vga_color bg) {
    struct vga_ctx *ctx;
    vga_select(0, &ctx);
    uint8_t old_color = ctx->current_color;
    vga_set_color(fg, bg);
    vga_print(str);
    vga_set_color(old_color & 0x0F, (old_color >> 4) & 0x0F);
}

// 打印信息 (白色)
void vga_print_info(const char* str) {
    vga_print_color("[INFO] ", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_print_color(str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

// 打印警告 (黄色)
void vga_print_warning(const char* str) {
    vga_print_color("[WARN] ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_print_color(str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

// 打印错误 (红色)
void vga_print_error(const char* str) {
    vga_print_color("[ERROR] ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    vga_print_color(str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

// 打印成功 (绿色)
void vga_print_success(const char* str) {
    vga_print_color("[SUCCESS] ", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    vga_print_color(str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_putchar('\n');
}

// 打印十六进制数
void vga_print_hex(uint32_t num) {
    char hex_chars[] = "0123456789ABCDEF";
    vga_print("0x");
    
    // 从最高位开始打印
    int started = 0;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (num >> i) & 0xF;
        if (nibble != 0 || started || i == 0) {
            vga_putchar(hex_chars[nibble]);
            started = 1;
        }
    }
}

// 打印十进制数
void vga_print_dec(uint32_t num) {
    if (num == 0) {
        vga_putchar('0');
        return;
    }
    
    char buffer[12];
    int i = 0;
    while (num > 0) {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    while (i > 0) {
        vga_putchar(buffer[--i]);
    }
}

// 当前控制台的列数 (VBE 按字符单元格, 文本模式为 80)
uint16_t vga_columns(void) {
    if (vbe_available()) {
        uint32_t columns = vbe_columns();
        if (columns != 0 && columns <= 0xFFFFU) {
            return (uint16_t)columns;
        }
    }
    return VGA_WIDTH;
}

// 当前控制台的行数 (VBE 按字符单元格, 文本模式为 25)
uint16_t vga_rows(void) {
    if (vbe_available()) {
        uint32_t rows = vbe_rows();
        if (rows != 0 && rows <= 0xFFFFU) {
            return (uint16_t)rows;
        }
    }
    return VGA_HEIGHT;
}

// 获取当前光标位置 (VBE 模式下返回字符单元格坐标)
void vga_get_cursor(uint16_t* row, uint16_t* col) {
    struct vga_ctx *ctx;
    vga_select(0, &ctx);
    if (vbe_available()) {
        vbe_get_cursor(row, col);
        return;
    }
    *row = ctx->cursor_row;
    *col = ctx->cursor_col;
}

// 设置光标位置 (VBE 模式下按字符单元格定位)
void vga_set_cursor(uint16_t row, uint16_t col) {
    int vt;
    struct vga_ctx *ctx;
    vt = vga_select(&vt, &ctx);
    if (vbe_available()) {
        vbe_set_cursor(row, col);
        return;
    }
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        ctx->cursor_row = row;
        ctx->cursor_col = col;
        if (vt == vt_active_index()) {
            vga_update_cursor(ctx);
        }
    }
}

// 终端切换后由 vt.c 调用：把硬件光标寄存器对准新可见终端的光标。
void vga_console_switched(void) {
    if (vbe_available()) return;
    int active = vt_active_index();
    vga_update_cursor(&vga_ctxs[active]);
}