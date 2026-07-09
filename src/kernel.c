#include "stdint.h"

void kmain() {
    __asm__ volatile ("cli");
    while(1) 
        __asm__ volatile ("hlt");
}