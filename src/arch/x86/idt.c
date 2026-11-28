#include "idt.h"
#include "io.h"
#include "vga.h"
#include "gdt.h"
#include "panic.h"
#include "string.h"

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void default_handler(void) {
    /* Entered directly by the CPU: [esp]=eip, [esp+4]=cs,
     * [esp+8]=eflags, plus an error code on vectors that push one —
     * we cannot tell which, so report it as unknown. Panic instead of
     * the old silent cli/hlt hang so stray exceptions/IRQs are
     * diagnosable; panic_blue_screen never returns. */
    struct registers regs;
    memset(&regs, 0, sizeof(regs));
    regs.int_no = 0xFFFFU;
    regs.err_code = 0xFFFFFFFFU;
    uint32_t sp;
    __asm__ volatile ("mov %%esp, %0" : "=r"(sp));
    volatile uint32_t *frame = (volatile uint32_t *)sp;
    regs.eip = frame[0];
    regs.cs = frame[1];
    regs.eflags = frame[2];
    panic_blue_screen("Unhandled CPU exception or interrupt", &regs);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;

    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void idt_set_entry(int index, uint32_t base, uint16_t selector, uint8_t flags) {
    idt[index].base_low = base & 0xFFFF;
    idt[index].sel = selector;
    idt[index].always0 = 0;        // 必须为 0
    idt[index].flags = flags;
    idt[index].base_high = (base >> 16) & 0xFFFF;
}

void idt_set_task_gate(uint8_t num, uint16_t sel) {
    idt[num].base_low = 0;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = 0x85;
    idt[num].base_high = 0;
}

void isr_handler(uint32_t int_no);
void irq_handler(uint32_t int_no);

void idt_init(void) {
    for (int i = 0;i < 256;i++) {
        idt_set_gate(i, (uint32_t)default_handler, 0x08, 0x8E);
    }

    extern void isr3(void);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);

    extern void isr14(void);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);

    idt_set_task_gate(8, GDT_DF_TSS_SELECTOR);

    extern void irq0(void);
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);

    extern void irq1(void);
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);

    extern void syscall(void);
    // Trap gate (0xEF) keeps IF unchanged so blocking syscalls can be
    // woken by the keyboard IRQ; interrupt gate (0xEE) would deadlock them.
    idt_set_entry(0x80, (uint32_t)syscall, 0x08, 0xEF);

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;
    
    __asm__ volatile ("lidt (%0)" :: "r" (&idtp));
}