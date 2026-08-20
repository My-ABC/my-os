#include "ring3.h"

void ring0_to_ring3(uint32_t *ustack_top, void (*func)(void)) {
    __asm__ volatile (
        "cli\n"
        "pushl $0x23\n"
        "pushl %0\n"
        "pushl $0x202\n"
        "pushl $0x1B\n"
        "pushl %1\n"
        "iret\n"
        :
        : "r"(ustack_top), "r"(func)
        : "memory"
    );
}