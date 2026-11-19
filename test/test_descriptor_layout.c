#include "test_support.h"

/* Include the real implementations so their private descriptor tables can be
 * checked without executing LGDT/LIDT or any other privileged instruction. */
#include "../src/gdt.c"
#include "../src/idt.c"

int main(void) {
    gdt_set_entry(2, 0x12345678U, 0xABCDEU, 0x9AU, 0xC0U);
    check(gdt[2].base_low == 0x5678U &&
          gdt[2].base_middle == 0x34U && gdt[2].base_high == 0x12U,
          "GDT: setter splits 32-bit base into descriptor fields");
    check(gdt[2].limit_low == 0xBCDEU &&
          gdt[2].granularity == 0xCAU && gdt[2].access == 0x9AU,
          "GDT: setter combines limit high nibble and granularity flags");

    idt_set_gate(0x31, 0x89ABCDEFU, 0x28U, 0x8EU);
    check(idt[0x31].base_low == 0xCDEFU &&
          idt[0x31].base_high == 0x89ABU,
          "IDT: gate splits handler address into low/high words");
    check(idt[0x31].sel == 0x28U && idt[0x31].always0 == 0 &&
          idt[0x31].flags == 0x8EU,
          "IDT: gate stores selector, reserved byte, and attributes");
    idt_set_task_gate(8, GDT_DF_TSS_SELECTOR);
    check(idt[8].base_low == 0 && idt[8].base_high == 0 &&
          idt[8].sel == GDT_DF_TSS_SELECTOR && idt[8].flags == 0x85U,
          "IDT: task gate uses zero offset and present task-gate attributes");

    return finish_test_suite("Descriptor layout tests");
}
