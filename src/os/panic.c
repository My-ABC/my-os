#include "panic.h"
#include "acpi.h"
#include "pit.h"
#include "serial.h"
#include "stdint.h"
#include "vga.h"

static void print_reg(const char* name, uint32_t value) {
    vga_print(name);
    vga_print_hex(value);
    vga_print("  ");

    serial_print(name);
    serial_print_hex(value);
    serial_putchar('\n');
}

// 倒计时; 依赖 IRQ0, 定时器未跑时退化为固定次数的忙等
static void countdown(uint32_t seconds) {
    for (uint32_t left = seconds; left > 0; left--) {
        vga_print_dec(left);
        vga_print("... ");

        uint32_t start = pit_ticks();
        uint32_t spins = 0;
        __asm__ volatile ("sti");
        while (pit_ticks() - start < PIT_FREQUENCY) {
            __asm__ volatile ("hlt");
            if (++spins > 1000000) {
                break;
            }
        }
        __asm__ volatile ("cli");
    }
    vga_print("\n");
}

void panic_blue_screen(const char* message, struct registers* regs) {
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    vga_clear();

    vga_print("*** STOP: KERNEL PANIC ***\n\n");
    vga_print(message);
    vga_print("\n\n");

    serial_print("\n*** STOP: KERNEL PANIC ***\n");
    serial_print(message);
    serial_putchar('\n');

    vga_print("Interrupt: ");
    vga_print_hex(regs->int_no);
    vga_print("  Error code: ");
    vga_print_hex(regs->err_code);
    vga_print("\n\n");
    print_reg("EAX=", regs->eax);
    print_reg("EBX=", regs->ebx);
    print_reg("ECX=", regs->ecx);
    print_reg("EDX=", regs->edx);
    vga_print("\n");
    print_reg("ESI=", regs->esi);
    print_reg("EDI=", regs->edi);
    print_reg("EBP=", regs->ebp);
    print_reg("ESP=", regs->esp);
    vga_print("\n");
    print_reg("EIP=", regs->eip);
    print_reg("CS =", regs->cs);
    print_reg("DS =", regs->ds);
    print_reg("EFL=", regs->eflags);
    vga_print("\n\n");

    vga_print("Rebooting via ACPI in ");
    serial_print("Rebooting via ACPI\n");
    countdown(PANIC_REBOOT_SECONDS);

    acpi_reboot();
}
