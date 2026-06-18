#include "stdio.h"
#include "stdlib.h"
#include "keyboard.h"
#include "idt.h"
#include "isr.h"
#include "trap.h"
#include "memory.h"
#include "string.h"
#include "paging.h"
#include "shell/shell.h"
#include "syscalls.h"

extern struct multiboot_info* multiboot_info;

void kmain(struct multiboot_info* info) {
    vga_clear();
    print_info("=== My OS ===\n\n");
    
    pic_init();
    idt_init();
    trap_init();
    pmm_init(info);
    paging_init();
    
    keyboard_init();
    __asm__ volatile ("sti");
    
    // ===== 测试系统调用 =====
    print_info("\n=== Testing syscall ===\n");
    
    // 使用内联汇编调用 int 0x80
    int result;
    __asm__ volatile (
        "movl $1, %%eax\n"      // SYS_PRINT
        "movl $0xC0000000, %%ebx\n"  // 字符串地址
        "int $0x80\n"
        "movl %%eax, %0\n"
        : "=r"(result)
        : : "eax", "ebx"
    );
    
    printf("syscall returned: %d\n", result);
    
    print_info("\nWelcome to My OS!\n");
    print_info("Type 'help' for available commands.\n");
    
    shell_run();
}