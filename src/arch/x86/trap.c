#include "trap.h"
#include "panic.h"
#include "serial.h"
#include "paging.h"
#include "pmm.h"

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
        uint32_t flags = PAGE_PRESENT | PAGE_WRITABLE;
        void *physical_page = pmm_alloc_page();

        if (physical_page == 0) {
            panic_blue_screen("Out of physical memory", regs);
            return;
        }

        if (paging_is_user_address(fault_addr)) {
            flags |= PAGE_USER;
        }

        paging_map_page(fault_addr, (uint32_t)physical_page, flags);
        serial_print("[PAGE FAULT] Allocated and mapped physical page\n");
        return;
    }

    panic_blue_screen("Page fault", regs);
}
