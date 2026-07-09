#include "vga.h"

void isr3_handler(void) {
    vga_print_error("[INT3] This is an big Error");
}