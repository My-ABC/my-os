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
#include "serial.h"
#include "process.h"
#include "elf.h"

extern struct multiboot_info* multiboot_info;

// 测试 ELF 数据（一个简单的程序）
static uint8_t test_elf[] = {
    // 这里需要放一个真正的 ELF 文件
};

void kmain(struct multiboot_info* info) {
    vga_clear();
    print_info("=== My OS ===\n\n");
    
    pic_init();
    idt_init();
    trap_init();
    pmm_init(info);
    paging_init();
    serial_init(COM1);
    
    keyboard_init();
    __asm__ volatile ("sti");
    
    print_info("\nWelcome to My OS!\n");
    print_info("Type 'help' for available commands.\n");
    
    shell_run();
}