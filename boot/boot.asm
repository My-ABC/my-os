bits 32
section .multiboot
align 4
    dd 0x1BADB002
    dd 0x03
    dd -(0x1BADB002 + 0x03)

section .text
global start
extern kernel_main

start:
    cli
    mov esp, stack_top
    call kernel_main
    hlt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global keyboard_handler_wrapper
extern keyboard_handler_c
