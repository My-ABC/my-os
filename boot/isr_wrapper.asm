section .text

extern isr_dispatcher
extern int3_handler

%macro ISR_ENTRY 1
global isr%1
isr%1:
    pusha
    push %1
    call isr_dispatcher
    add esp, 4
    popa
    iret
%endmacro

; 时钟中断 (IRQ0)
ISR_ENTRY 32

; 键盘中断 (IRQ1)
ISR_ENTRY 33

; INT3 异常处理
global isr3
isr3:
    pusha
    call int3_handler
    popa
    iret

global idt_load
idt_load:
    lidt [esp + 4]
    ret