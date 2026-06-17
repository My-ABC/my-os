#ifndef _IDT_H
#define _IDT_H

#include "stdint.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef void (*isr_handler_t)(void);

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);
void idt_register_handler(uint8_t num, isr_handler_t handler);
extern void idt_load(void);

#endif