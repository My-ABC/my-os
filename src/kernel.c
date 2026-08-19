#include "stdint.h"
#include "stdio.h"
#include "vga.h"
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "serial.h"
#include "rtc.h"

void kmain() {
    // 初始化 VGA
    vga_init();
    serial_init();
    serial_printf("MyOS v0.0.1\n");
    
    printf("MyOS v0.1\n");
    printf("==========\n\n");
    
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

    vga_print_info("Initializing keyboard (IRQ1)...");
    keyboard_init();
    
#ifdef SCANCODE_SET
    keyboard_set_scancode_set(SCANCODE_SET);
    serial_printf("Scancode set: %d\n", SCANCODE_SET);
#endif
    
    pic_clear_mask(1);
    vga_print_success("Keyboard initialized");

    vga_print_info("Enabling interrupts...");
    __asm__ volatile ("sti");
    vga_print_success("Interrupts enabled");

    vga_print_info("Initializing RTC...");
    rtc_init();
    vga_print_success("RTC initialized");

    vga_print_info("Kernel loaded at 0x100000");
    vga_print_warning("Low memory detected");
    vga_print_error("Network card not found");
    vga_print_success("VGA driver loaded");
    vga_print("\n");
    
    // 测试数字
    printf("Numbers: %d (dec), %x (hex)\n", 42, 0xDEADBEEF);

    // 显示RTC时间
    vga_print("Current time (UTC): ");
    rtc_print_datetime();
    
    // 显示北京时间
    vga_print("Beijing time (UTC+8): ");
    {
        rtc_time_t time;
        rtc_read_time(&time);
        rtc_to_beijing_time(&time);
        
        // 打印北京时间
        printf("%04d-%02d-%02d %02d:%02d:%02d\n", 
               time.year, time.month, time.day, time.hour, time.minute, time.second);
        
        // 同样输出到串口
        serial_printf("Beijing time (UTC+8): %04d-%02d-%02d %02d:%02d:%02d\n", 
                      time.year, time.month, time.day, time.hour, time.minute, time.second);
    }
    // 显示Unix时间戳
    rtc_print_unix_timestamp();

#ifdef PANIC_DEMO
    vga_print("Triggering INT3 (panic demo)\n");
    __asm__ volatile ("int $0x03");
#endif

    printf("Timer ticking, output on COM1\n");
    printf("Press 'b' for a blue screen, any other key to halt\n");
    serial_printf("Press 'b' for a blue screen, any other key to halt\n");

    char key = keyboard_wait_key();
    printf("Key pressed: %c\n", key);
    serial_printf("Key pressed: %c\n", key);

    if (key == 'b' || key == 'B') {
        __asm__ volatile ("int $0x03");
    }

    serial_printf("Halted\n");
    printf("Halted\n");
    while (1);
}