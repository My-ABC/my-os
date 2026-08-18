section .text

%macro ISR_NOERR 1
isr%1:
    cli
    push byte 0
    push byte %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
isr%1:
    cli
    push byte %1
    jmp isr_common_stub
%endmacro

%macro IRQ 2
irq%1:
    cli
    push byte 0
    push byte %2
    jmp irq_common_stub
%endmacro

extern isr3_handler

global isr3

isr3:
    pusha
    call isr3_handler
    popa
    iret

extern isr_handler
extern irq_handler

global irq0

irq0:
    pusha
    push dword 32
    call irq_handler
    add esp, 4
    popa
    iret

isr_common_stub:
    pusha
    popa
    iret

irq_common_stub:
    pusha
    popa
    iret