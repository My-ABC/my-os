#include "trap.h"
#include "stdio.h"
#include "vga.h"
#include "idt.h"
#include "keyboard.h"

// 数字转十六进制字符串
static void hex_to_str(uint32_t num, char* buf) {
    const char* hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[num & 0xF];
        num >>= 4;
    }
    buf[8] = '\0';
}

// 拼接两个寄存器值
#define CONCAT_REG(line_buf, label1, val1, label2, val2) \
    do { \
        char _buf1[16], _buf2[16]; \
        hex_to_str(val1, _buf1); \
        hex_to_str(val2, _buf2); \
        int _idx = 0; \
        const char* _p = label1; \
        while (*_p) line_buf[_idx++] = *_p++; \
        _p = _buf1; \
        while (*_p) line_buf[_idx++] = *_p++; \
        _p = label2; \
        while (*_p) line_buf[_idx++] = *_p++; \
        _p = _buf2; \
        while (*_p) line_buf[_idx++] = *_p++; \
        line_buf[_idx] = '\0'; \
    } while(0)

static void center_text(const char* str, int y, uint8_t color) {
    int len = 0;
    while (str[len]) len++;
    int x = (VGA_WIDTH - len) / 2;
    if (x < 0) x = 0;
    
    for (int i = 0; str[i]; i++) {
        vga_putchar_at(str[i], color, x + i, y);
    }
}

void kernel_panic(const char* message, struct registers* regs) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        uint16_t* vga = (uint16_t*)0xB8000;
        vga[i] = (uint16_t)' ' | ((uint16_t)0x1F << 8);
    }
    
    int line = 1;
    center_text("========================================", line++, 0x1F);
    center_text("        KERNEL PANIC - SYSTEM CRASH", line++, 0x1F);
    center_text("========================================", line++, 0x1F);
    line++;
    
    center_text("Error:", line++, 0x1F);
    center_text(message, line++, 0x4F);
    line++;
    
    if (regs) {
        center_text("=== CPU REGISTERS ===", line++, 0x1F);
        line++;
        
        char line_buf[64];
        
        CONCAT_REG(line_buf, "EAX: 0x", regs->eax, "     EBX: 0x", regs->ebx);
        center_text(line_buf, line++, 0x1F);
        
        CONCAT_REG(line_buf, "ECX: 0x", regs->ecx, "     EDX: 0x", regs->edx);
        center_text(line_buf, line++, 0x1F);
        
        CONCAT_REG(line_buf, "ESI: 0x", regs->esi, "     EDI: 0x", regs->edi);
        center_text(line_buf, line++, 0x1F);
        
        CONCAT_REG(line_buf, "EBP: 0x", regs->ebp, "     ESP: 0x", regs->esp);
        center_text(line_buf, line++, 0x1F);
        
        CONCAT_REG(line_buf, "EIP: 0x", regs->eip, "     EFLAGS: 0x", regs->eflags);
        center_text(line_buf, line++, 0x1F);
        
        CONCAT_REG(line_buf, "CS: 0x", regs->cs, "        SS: 0x", regs->ss);
        center_text(line_buf, line++, 0x1F);
    }
    
    line++;
    center_text("========================================", line++, 0x1F);
    center_text("System will reboot ...", line++, 0x1F);

    __asm__ volatile (
        "cli\n"
        "movl $0x0000FFF0, %eax\n"
        "jmp *%eax\n"
    );
}

void int3_handler(void) {
    struct registers regs;
    __asm__ volatile (
        "movl %%edi, %0\n"
        "movl %%esi, %1\n"
        "movl %%ebp, %2\n"
        "movl %%esp, %3\n"
        "movl %%ebx, %4\n"
        "movl %%edx, %5\n"
        "movl %%ecx, %6\n"
        "movl %%eax, %7\n"
        : "=m"(regs.edi), "=m"(regs.esi), "=m"(regs.ebp), "=m"(regs.esp),
          "=m"(regs.ebx), "=m"(regs.edx), "=m"(regs.ecx), "=m"(regs.eax)
    );

    for (int i = 0; i < 10; i++);
    
    __asm__ volatile (
        "movl 4(%%ebp), %0\n"
        "movl 8(%%ebp), %1\n"
        "movl 12(%%ebp), %2\n"
        : "=r"(regs.eip), "=r"(regs.cs), "=r"(regs.eflags)
    );
    
    regs.ss = 0;
    regs.ds = 0;
    regs.es = 0;
    regs.fs = 0;
    regs.gs = 0;
    
    kernel_panic("INT3 - Breakpoint Exception", &regs);
}

void trap_init(void) {
    extern void isr3(void);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    print_info("Trap handlers installed\n");
}

void int3_breakpoint(void) {
    __asm__ volatile ("int $0x03");
}