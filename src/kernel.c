#include "stdio.h"
#include "stdlib.h"
#include "keyboard.h"
#include "idt.h"
#include "isr.h"
#include "trap.h"
#include "memory.h"
#include "string.h"

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
    
    print_info("Enabling interrupts...\n");
    __asm__ volatile ("sti");
    print_info("Interrupts enabled!\n");
    
    // ===== 测试 stdlib =====
    print_info("\n=== Testing stdlib ===\n");
    
    // 测试 malloc/free
    print_info("malloc(100): ");
    char* p = (char*)malloc(100);
    if (p) {
        print_info("OK at ");
        char buf[16];
        uint32_t addr = (uint32_t)p;
        const char* hex = "0123456789ABCDEF";
        for (int i = 7; i >= 0; i--) {
            buf[i] = hex[addr & 0xF];
            addr >>= 4;
        }
        buf[8] = '\0';
        print_info(buf);
        print_info("\n");
        
        // 写入数据
        strcpy(p, "Hello from malloc!");
        print_info("  data: ");
        print_info(p);
        print_info("\n");
        
        free(p);
        print_info("  freed\n");
    }
    
    // 测试 atoi
    print_info("atoi(\"1234\"): ");
    char buf[16];
    int val = atoi("1234");
    itoa(val, buf, 10);
    print_info(buf);
    print_info("\n");
    
    // 测试 itoa
    print_info("itoa(255, 16): ");
    itoa(255, buf, 16);
    print_info(buf);
    print_info("\n");
    
    // 测试随机数
    print_info("rand(): ");
    itoa(rand(), buf, 10);
    print_info(buf);
    print_info(", ");
    itoa(rand(), buf, 10);
    print_info(buf);
    print_info(", ");
    itoa(rand(), buf, 10);
    print_info(buf);
    print_info("\n");
    
    print_info("\nSystem running. Press 'b' for blue screen.\n");
    
    while(1) {
        if (keyboard_has_key()) {
            char c = keyboard_getchar();
            if (c == 'b' || c == 'B') {
                int3_breakpoint();
            } else {
                putchar(c);
            }
        }
    }
}