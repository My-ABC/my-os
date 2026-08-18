#include "trap.h"
#include "panic.h"

void isr3_handler(struct registers* regs) {
    panic_blue_screen("Breakpoint exception (INT3)", regs);
}
