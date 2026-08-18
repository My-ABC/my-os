#include "stdint.h"
#include "vga.h"
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"

void kmain() {
    // 初始化 VGA
    vga_init();
    serial_init();
    serial_print("MyOS v0.1 serial console\n");
    
    vga_print("MyOS v0.1\n");
    vga_print("==========\n\n");
    
    // 测试日志输出
    vga_print_info("System initialized successfully");
    
    vga_print_info("Loading IDT...");
    idt_init();
    vga_print_success("IDT loaded");

    vga_print_info("Remapping PIC...");
    pic_remap();
    vga_print_success("PIC remapped");

    vga_print_info("Initializing PIT at 100Hz...");
    pit_init(PIT_FREQUENCY);
    pic_clear_mask(0);
    vga_print_success("PIT initialized");

    vga_print_info("Enabling interrupts...");
    __asm__ volatile ("sti");
    vga_print_success("Interrupts enabled");

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

#ifdef PANIC_DEMO
    vga_print("Triggering INT3 (panic demo)\n");
    __asm__ volatile ("int $0x03");
#endif

    vga_print("Timer ticking, output on COM1\n");
    while(1) {
        __asm__ volatile ("hlt");
    }
}