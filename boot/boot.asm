section .multiboot
align 4
    dd 0x1BADB002
    dd 0x03
    dd -(0x1BADB002 + 0x03)

section .text
global start
extern kmain

start:
    cli
    mov esp, stack_top
    push ebx
    call kmain
    hlt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top: