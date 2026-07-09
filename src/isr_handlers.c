#include "stdint.h"
#include "vga.h"

void isr_handler(uint32_t int_no) {
    vga_print_error("A isr Error");
}

void irq_handler(uint32_t int_no) {
    vga_print_info("A irq int");
}