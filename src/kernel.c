#include "stdio.h"
#include "stdlib.h"
#include "keyboard.h"
#include "idt.h"
#include "isr.h"
#include "trap.h"
#include "memory.h"
#include "string.h"
#include "shell/shell.h"

extern struct multiboot_info* multiboot_info;

void kmain(struct multiboot_info* info) {
    vga_clear();
    print_info("=== My OS ===\n\n");
    
    print_info("Initializing PIC...\n");
    pic_init();
    
    print_info("Initializing IDT...\n");
    idt_init();
    
    print_info("Initializing traps...\n");
    trap_init();
    
    print_info("Initializing Physical Memory Manager...\n");
    pmm_init(info);
    
    print_info("Initializing keyboard...\n");
    keyboard_init();
    
    print_info("Enabling interrupts...\n");
    __asm__ volatile ("sti");
    print_info("Interrupts enabled!\n");
    
    print_info("\nWelcome to My OS!\n");
    print_info("Type 'help' for available commands.\n");
    
    // 启动 Shell
    shell_run();
}