#include "stdint.h"
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
    serial_print("MyOS v0.0.1\n");
    
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

    vga_print_info("Initializing keyboard (IRQ1)...");
    keyboard_init();
    
#ifdef SCANCODE_SET
    keyboard_set_scancode_set(SCANCODE_SET);
    serial_print("Scancode set: ");
    serial_print_dec(SCANCODE_SET);
    serial_putchar('\n');
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
    vga_print("Numbers: ");
    vga_print_dec(42);
    vga_print(" (dec), ");
    vga_print_hex(0xDEADBEEF);
    vga_print(" (hex)\n");

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
        vga_print_dec(time.year);
        vga_putchar('-');
        if (time.month < 10) vga_putchar('0');
        vga_print_dec(time.month);
        vga_putchar('-');
        if (time.day < 10) vga_putchar('0');
        vga_print_dec(time.day);
        vga_putchar(' ');
        
        if (time.hour < 10) vga_putchar('0');
        vga_print_dec(time.hour);
        vga_putchar(':');
        if (time.minute < 10) vga_putchar('0');
        vga_print_dec(time.minute);
        vga_putchar(':');
        if (time.second < 10) vga_putchar('0');
        vga_print_dec(time.second);
        vga_putchar('\n');
        
        // 同样输出到串口
        serial_print("Beijing time (UTC+8): ");
        serial_print_dec(time.year);
        serial_putchar('-');
        if (time.month < 10) serial_putchar('0');
        serial_print_dec(time.month);
        serial_putchar('-');
        if (time.day < 10) serial_putchar('0');
        serial_print_dec(time.day);
        serial_putchar(' ');
        
        if (time.hour < 10) serial_putchar('0');
        serial_print_dec(time.hour);
        serial_putchar(':');
        if (time.minute < 10) serial_putchar('0');
        serial_print_dec(time.minute);
        serial_putchar(':');
        if (time.second < 10) serial_putchar('0');
        serial_print_dec(time.second);
        serial_putchar('\n');
    }
    // 显示Unix时间戳
    rtc_print_unix_timestamp();

#ifdef PANIC_DEMO
    vga_print("Triggering INT3 (panic demo)\n");
    __asm__ volatile ("int $0x03");
#endif

    vga_print("Timer ticking, output on COM1\n");
    vga_print("Press 'b' for a blue screen, any other key to halt\n");
    serial_print("Press 'b' for a blue screen, any other key to halt\n");

    char key = keyboard_wait_key();
    vga_print("Key pressed: ");
    vga_putchar(key);
    vga_putchar('\n');
    serial_print("Key pressed: ");
    serial_putchar(key);
    serial_putchar('\n');

    if (key == 'b' || key == 'B') {
        __asm__ volatile ("int $0x03");
    }

    serial_print("Halted\n");
    vga_print("Halted\n");
    while (1);
}