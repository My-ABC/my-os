#include "idt.h"
#include "stdio.h"

// IDT 作为字节数组，对齐到 16 字节
static uint8_t idt[256 * 8] __attribute__((aligned(16)));

void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags) {
    uint16_t idx = num * 8;
    idt[idx + 0] = base & 0xFF;
    idt[idx + 1] = (base >> 8) & 0xFF;
    idt[idx + 2] = selector & 0xFF;
    idt[idx + 3] = (selector >> 8) & 0xFF;
    idt[idx + 4] = 0;  // zero
    idt[idx + 5] = flags;
    idt[idx + 6] = (base >> 16) & 0xFF;
    idt[idx + 7] = (base >> 24) & 0xFF;
}

void idt_init(void) {
    print_info("Setting up IDT...\n");
    
    // 清空 IDT
    for (int i = 0; i < 256 * 8; i++) {
        idt[i] = 0;
    }
    
    // 设置时钟中断 (IRQ0)
    extern void isr32(void);
    uint32_t isr32_addr = (uint32_t)isr32;
    
    print_info("isr32 addr: 0x");
    char buf[16];
    const char* hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[isr32_addr & 0xF];
        isr32_addr >>= 4;
    }
    buf[8] = '\0';
    print_info(buf);
    print_info("\n");
    
    // 写入 IDT 条目 32
    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E);
    
    // 验证写入
    print_info("idt[32] = ");
    uint16_t idx = 32 * 8;
    for (int i = 0; i < 8; i++) {
        buf[i*2] = hex[(idt[idx + i] >> 4) & 0xF];
        buf[i*2 + 1] = hex[idt[idx + i] & 0xF];
    }
    buf[16] = '\0';
    print_info(buf);
    print_info("\n");
    
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr;
    
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)idt;
    
    __asm__ volatile ("lidt (%0)" : : "r"(&idtr));
    
    print_info("IDT loaded\n");
}