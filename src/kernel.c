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

extern struct multiboot_info* multiboot_info;

void kmain(struct multiboot_info* info) {
    vga_clear();
    print_info("=== My OS ===\n\n");
    
    pic_init();
    idt_init();
    trap_init();
    pmm_init(info);
    paging_init();
    
    // 初始化串口
    serial_init(COM1);
    
    // 串口输出测试
    serial_write(COM1, "Serial output test!\n");
    serial_write(COM1, "My OS serial debug enabled.\n");
    
    keyboard_init();
    __asm__ volatile ("sti");
    
    print_info("\nWelcome to My OS!\n");
    print_info("Type 'help' for available commands.\n");
    
    shell_run();
}