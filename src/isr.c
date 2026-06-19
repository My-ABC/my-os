#include "idt.h"
#include "stdio.h"
#include "isr.h"
#include "keyboard.h"
#include "process.h"

volatile uint32_t system_ticks = 0;
volatile uint32_t system_seconds = 0;

uint32_t get_ticks(void) {
    return system_ticks;
}

uint32_t get_seconds(void) {
    return system_seconds;
}

void sleep(uint32_t ms) {
    // PIT 默认约 18.2Hz，每 tick ≈ 55ms
    // 所以 1000ms ≈ 18 ticks
    uint32_t ticks = ms / 55;
    if (ticks == 0) ticks = 1;
    uint32_t target = system_ticks + ticks;
    while (system_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

void isr_dispatcher(uint8_t irq) {
    // 发送 EOI
    if (irq >= 32) {
        __asm__ volatile ("movb $0x20, %%al; outb %%al, $0x20" : : : "al");
        if (irq >= 40) {
            __asm__ volatile ("movb $0x20, %%al; outb %%al, $0xA0" : : : "al");
        }
    }
    
    switch (irq) {
        case 32:  // 时钟
            system_ticks++;
            if (system_ticks % 18 == 0) {
                system_seconds++;
            }
            if (system_ticks % 10 == 0) {
                process_schedule();
            }
            break;
        case 33:  // 键盘中断
            keyboard_handler();
            break;
        default:
            break;
    }
}

void pic_init(void) {
    __asm__ volatile ("movb $0x11, %%al; outb %%al, $0x20" : : : "al");
    __asm__ volatile ("movb $0x11, %%al; outb %%al, $0xA0" : : : "al");
    
    __asm__ volatile ("movb $0x20, %%al; outb %%al, $0x21" : : : "al");
    __asm__ volatile ("movb $0x28, %%al; outb %%al, $0xA1" : : : "al");
    
    __asm__ volatile ("movb $0x04, %%al; outb %%al, $0x21" : : : "al");
    __asm__ volatile ("movb $0x02, %%al; outb %%al, $0xA1" : : : "al");
    
    __asm__ volatile ("movb $0x01, %%al; outb %%al, $0x21" : : : "al");
    __asm__ volatile ("movb $0x01, %%al; outb %%al, $0xA1" : : : "al");
    
    // 允许时钟(IRQ0)和键盘(IRQ1)
    __asm__ volatile ("movb $0xFC, %%al; outb %%al, $0x21" : : : "al");
    __asm__ volatile ("movb $0xFF, %%al; outb %%al, $0xA1" : : : "al");
    
    __asm__ volatile ("sti");
    
    print_info("PIC initialized\n");
}