#include "trap.h"
#include "panic.h"
#include "serial.h"
#include "paging.h"
#include "pmm.h"
#include "ring3.h"
#include "syscall.h"
#include "string.h"

void isr3_handler(struct registers* regs) {
    panic_blue_screen("Breakpoint exception (INT3)", regs);
}

static int isr14_from_user(struct registers *regs) {
    return (regs->cs & 3U) == 3U || (regs->err_code & 0x4U) != 0U;
}

static int isr14_demand_map(uint32_t fault_addr) {
    uint32_t page = fault_addr & ~(PAGE_SIZE - 1U);
    uint64_t physical_page = pmm_alloc_page();
    if (physical_page == 0U) return 0;

    paging_map_page(page, physical_page,
                    PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (paging_get_page_physical(page) == 0U) {
        pmm_free_page(physical_page);
        return 0;
    }

    memset((void *)(uintptr_t)page, 0, PAGE_SIZE);
    return 1;
}

static uint32_t isr14_kill_current(struct registers *regs, const char *why) {
    struct irq_frame *kernel = ring3_terminate_current();
    if (kernel != 0) {
        serial_print("[PAGE FAULT] ");
        serial_print(why);
        serial_print("\n");
        return (uint32_t)kernel;
    }
    panic_blue_screen(why, regs);
    return 0;
}

uint32_t isr14_handler(struct registers* regs) {
    uint32_t fault_addr = 0;
    __asm__ volatile ("mov %%cr2, %0" : "=r" (fault_addr));
    int from_user = isr14_from_user(regs);

    serial_print("[PAGE FAULT] CR2=");
    serial_print_hex(fault_addr);
    serial_print(" error=");
    serial_print_hex(regs->err_code);
    serial_print("\n");

    if ((regs->err_code & 0x1U) == 0U) {
        int permitted = paging_is_user_address(fault_addr) &&
            (ring3_user_range_permitted(fault_addr) ||
             sys_user_heap_contains(fault_addr));
        if (permitted && isr14_demand_map(fault_addr)) {
            serial_print("[PAGE FAULT] Demand-mapped user page\n");
            return 0;
        }
        if (from_user) {
            return isr14_kill_current(regs,
                permitted ? "Out of memory for user page" : "Illegal user page fault");
        }
        panic_blue_screen(permitted ? "Out of physical memory" : "Illegal kernel page fault",
                          regs);
        return 0;
    }

    if (from_user) {
        return isr14_kill_current(regs, "User protection violation");
    }
    panic_blue_screen("Kernel protection fault", regs);
    return 0;
}
