#include "trap.h"
#include "panic.h"
#include "serial.h"
#include "paging.h"

void isr3_handler(struct registers* regs) {
    panic_blue_screen("Breakpoint exception (INT3)", regs);
}

void isr14_handler(struct registers* regs) {
    uint32_t fault_addr = 0;

    __asm__ volatile ("mov %%cr2, %0" : "=r" (fault_addr));

    serial_print("[PAGE FAULT] CR2=");
    serial_print_hex(fault_addr);
    serial_print(" error=");
    serial_print_hex(regs->err_code);
    serial_print("\n");

    if ((regs->err_code & 0x1U) == 0U) {
        paging_map_page(fault_addr, fault_addr, PAGE_PRESENT | PAGE_WRITABLE);
        serial_print("[PAGE FAULT] Lazy mapped missing page\n");
        return;
    }

    panic_blue_screen("Page fault", regs);
}
