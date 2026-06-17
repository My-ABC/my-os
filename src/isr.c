#include "idt.h"
#include "stdio.h"
#include "isr.h"

volatile uint32_t system_ticks = 0;
volatile uint32_t system_seconds = 0;
volatile uint32_t irq_count = 0;

uint32_t get_ticks(void) {
    return system_ticks;
}

uint32_t get_seconds(void) {
    return system_seconds;
}

// sleep 参数改为秒（不是毫秒）
void sleep_seconds(uint32_t sec) {
    uint32_t target = system_ticks + sec * 18;  // 18 ticks ≈ 1 秒
    while (system_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

void sleep(uint32_t ms) {
    // 转换为 ticks（约 18.2 Hz）
    uint32_t ticks = (ms * 18) / 1000;
    if (ticks == 0) ticks = 1;
    uint32_t target = system_ticks + ticks;
    while (system_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

void isr_dispatcher(uint8_t irq) {
    if (irq >= 32) {
        __asm__ volatile ("movb $0x20, %%al; outb %%al, $0x20" : : : "al");
        if (irq >= 40) {
            __asm__ volatile ("movb $0x20, %%al; outb %%al, $0xA0" : : : "al");
        }
    }
    
    switch (irq) {
        case 32:
            system_ticks++;
            irq_count++;
            if (system_ticks % 18 == 0) {
                system_seconds++;
            }
            break;
        case 33:
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
    
    __asm__ volatile ("movb $0xFE, %%al; outb %%al, $0x21" : : : "al");
    __asm__ volatile ("movb $0xFF, %%al; outb %%al, $0xA1" : : : "al");
    
    __asm__ volatile ("sti");
    
    print_info("PIC initialized\n");
}