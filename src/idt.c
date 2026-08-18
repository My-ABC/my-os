#include "idt.h"
#include "io.h"
#include "vga.h"

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void default_handler(void) {
    vga_print_error("Error: this is a NULL isr");
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    };
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;

    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void isr_handler(uint32_t int_no);
void irq_handler(uint32_t int_no);

void idt_init(void) {
    for (int i = 0;i < 256;i++) {
        idt_set_gate(i, (uint32_t)default_handler, 0x08, 0x8E);
    }

    extern void isr3(void);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);

    extern void irq0(void);
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;
    
    __asm__ volatile ("lidt (%0)" :: "r" (&idtp));
}