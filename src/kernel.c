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

extern struct multiboot_info* multiboot_info;

// 测试进程（执行一次就返回）
void test_process1(void) {
    static int count = 0;
    printf("[P1] Running: %d\n", count++);
}

void test_process2(void) {
    static int count = 0;
    printf("[P2] Running: %d\n", count++);
}

void test_process3(void) {
    static int count = 0;
    printf("[P3] Running: %d\n", count++);
    if (++count >= 3) {
        process_exit(0);
    }
}

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
    
    // ===== 进程测试 =====
    print_info("\n=== Process Manager Test ===\n");
    process_init();
    
    process_create(test_process1, "test1");
    process_create(test_process2, "test2");
    process_create(test_process3, "test3");
    
    process_list();
    
    print_info("\n=== Scheduling (10 rounds) ===\n");
    for (int i = 0; i < 10; i++) {
        printf("\n--- Round %d ---\n", i);
        process_schedule();
    }
    
    print_info("\n=== Final Process List ===\n");
    process_list();
    
    print_info("\nWelcome to My OS!\n");
    print_info("Type 'help' for available commands.\n");
    print_info("Type 'ps' to see running processes.\n");
    
    shell_run();
}