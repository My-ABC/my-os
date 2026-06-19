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
    serial_init(COM1);
    process_init();
    
    keyboard_init();
    __asm__ volatile ("sti");
    
    // 测试缺页异常（访问 128MB，超出已映射区域）
    print_info("\n=== Testing Page Fault ===\n");
    print_info("Reading 0x01000000 (128MB)...\n");
    
    uint32_t* test = (uint32_t*)0x01000000;
    uint32_t val = *test;
    printf("Value: 0x%x\n", val);
    
    print_info("\nWelcome to My OS!\n");
    print_info("Type 'help' for available commands.\n");
    
    shell_run();
}