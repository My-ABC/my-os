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

ISR_ENTRY 32
ISR_ENTRY 33

; 系统调用暂时禁用
; global isr80
; isr80:
;     pusha
;     call syscall_handler
;     mov [esp+28], eax
;     popa
;     iret

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