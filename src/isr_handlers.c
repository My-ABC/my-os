#include "stdint.h"
#include "vga.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"

void isr_handler(uint32_t int_no) {
    vga_print_error("A isr Error");
}

void irq_handler(uint32_t int_no) {
    if (int_no == 32) {
        pit_tick();

        uint32_t ticks = pit_ticks();
        if (ticks % PIT_FREQUENCY == 0) {
            serial_print_dec(ticks / PIT_FREQUENCY);
            serial_putchar('\n');
        }
    }

    pic_send_eoi(int_no - 32);
}
