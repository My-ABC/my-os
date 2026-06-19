#include "idt.h"
#include "stdio.h"

static uint8_t idt[256 * 8] __attribute__((aligned(16)));

extern void isr32(void);
extern void isr33(void);
extern void isr80(void);

void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags) {
    uint16_t idx = num * 8;
    idt[idx + 0] = base & 0xFF;
    idt[idx + 1] = (base >> 8) & 0xFF;
    idt[idx + 2] = selector & 0xFF;
    idt[idx + 3] = (selector >> 8) & 0xFF;
    idt[idx + 4] = 0;
    idt[idx + 5] = flags;
    idt[idx + 6] = (base >> 16) & 0xFF;
    idt[idx + 7] = (base >> 24) & 0xFF;
}

void idt_init(void) {
    print_info("Setting up IDT...\n");
    
    for (int i = 0; i < 256 * 8; i++) {
        idt[i] = 0;
    }
    
    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr33, 0x08, 0x8E);
    idt_set_gate(0x80, (uint32_t)isr80, 0x08, 0x8E);
    
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr;
    
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)idt;
    
    __asm__ volatile ("lidt (%0)" : : "r"(&idtr));
    
    print_info("IDT loaded\n");
}