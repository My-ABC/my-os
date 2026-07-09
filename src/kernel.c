#include "stdint.h"
#include "vga.h"

void kmain() {
    // 初始化 VGA
    vga_init();
    
    vga_print("MyOS v0.1\n");
    vga_print("==========\n\n");
    
    // 测试日志输出
    vga_print_info("System initialized successfully");
    vga_print_info("Kernel loaded at 0x100000");
    vga_print_warning("Low memory detected");
    vga_print_error("Network card not found");
    vga_print_success("VGA driver loaded");
    vga_print("\n");
    
    // 测试数字
    vga_print("Numbers: ");
    vga_print_dec(42);
    vga_print(" (dec), ");
    vga_print_hex(0xDEADBEEF);
    vga_print(" (hex)\n");
    
    __asm__ volatile ("cli");
    while(1) {
        __asm__ volatile ("hlt");
    }
}