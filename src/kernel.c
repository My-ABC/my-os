#include "stdio.h"
#include "keyboard.h"
#include "idt.h"
#include "isr.h"
#include "trap.h"

void kernel_main() {
    vga_clear();
    print_info("=== My OS ===\n\n");
    
    print_info("Initializing PIC...\n");
    pic_init();
    
    print_info("Initializing IDT...\n");
    idt_init();
    
    print_info("Initializing traps...\n");
    trap_init();
    
    print_info("Enabling interrupts...\n");
    __asm__ volatile ("sti");
    print_info("Interrupts enabled!\n");
    
    print_info("\nTesting sleep(1000 ms)...\n");
    sleep(1000);
    print_info("1 second passed!\n");
    sleep(1000);
    print_info("2 seconds passed!\n");
    sleep(1000);
    print_info("3 seconds passed!\n");
    
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