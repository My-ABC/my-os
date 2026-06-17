#include "stdio.h"
#include "keyboard.h"
#include "idt.h"
#include "isr.h"
#include "trap.h"
#include "memory.h"

// Multiboot 信息结构（由 boot.asm 传入）
extern struct multiboot_info* multiboot_info;

void kernel_main(struct multiboot_info* info) {
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
    
    // 测试内存分配
    print_info("\nTesting memory allocation...\n");
    uint32_t p1 = pmm_alloc_page();
    uint32_t p2 = pmm_alloc_page();
    uint32_t p3 = pmm_alloc_page();
    
    print_info("Allocated pages at: ");
    char buf[16];
    // 打印 p1
    uint32_t n = p1;
    const char* hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    buf[8] = '\0';
    print_info(buf);
    print_info(", ");
    // p2
    n = p2;
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    buf[8] = '\0';
    print_info(buf);
    print_info(", ");
    // p3
    n = p3;
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    buf[8] = '\0';
    print_info(buf);
    print_info("\n");
    
    // 释放 p2
    print_info("Freeing page 2...\n");
    pmm_free_page(p2);
    
    // 分配一页（应该重用刚释放的页）
    uint32_t p4 = pmm_alloc_page();
    print_info("Allocated new page at: ");
    n = p4;
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    buf[8] = '\0';
    print_info(buf);
    print_info("\n");
    
    // 显示内存统计
    uint32_t total, free, used;
    pmm_get_stats(&total, &free, &used);
    print_info("\nMemory stats: total=");
    char buf2[16];
    n = total;
    if (n < 10) {
        buf2[0] = '0' + n;
        buf2[1] = '\0';
    } else if (n < 100) {
        buf2[0] = '0' + (n / 10);
        buf2[1] = '0' + (n % 10);
        buf2[2] = '\0';
    } else if (n < 1000) {
        buf2[0] = '0' + (n / 100);
        buf2[1] = '0' + ((n / 10) % 10);
        buf2[2] = '0' + (n % 10);
        buf2[3] = '\0';
    } else {
        buf2[0] = '0' + (n / 1000);
        buf2[1] = '0' + ((n / 100) % 10);
        buf2[2] = '0' + ((n / 10) % 10);
        buf2[3] = '0' + (n % 10);
        buf2[4] = '\0';
    }
    print_info(buf2);
    print_info(" pages, free=");
    n = free;
    if (n < 10) {
        buf2[0] = '0' + n;
        buf2[1] = '\0';
    } else if (n < 100) {
        buf2[0] = '0' + (n / 10);
        buf2[1] = '0' + (n % 10);
        buf2[2] = '\0';
    } else if (n < 1000) {
        buf2[0] = '0' + (n / 100);
        buf2[1] = '0' + ((n / 10) % 10);
        buf2[2] = '0' + (n % 10);
        buf2[3] = '\0';
    } else {
        buf2[0] = '0' + (n / 1000);
        buf2[1] = '0' + ((n / 100) % 10);
        buf2[2] = '0' + ((n / 10) % 10);
        buf2[3] = '0' + (n % 10);
        buf2[4] = '\0';
    }
    print_info(buf2);
    print_info(" pages\n");
    
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