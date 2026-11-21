section .boot.text progbits alloc exec nowrite

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
extern isr14_handler

global isr3

global isr14
global double_fault_task

isr3:
    cli
    push dword 0        ; 伪错误码
    push dword 3        ; 中断号
    pusha
    xor eax, eax
    mov ax, ds
    push eax
    push esp            ; struct registers*
    call isr3_handler
    add esp, 4
    pop eax
    popa
    add esp, 8          ; 弹掉中断号与错误码
    iret

isr14:
    cli
    push dword 14
    pusha
    xor eax, eax
    mov ax, ds
    push eax
    push esp
    call isr14_handler
    add esp, 4
    test eax, eax
    jnz .isr14_switch
    pop eax
    popa
    add esp, 8
    iret
.isr14_switch:
    ; isr14_handler 返回了要切换到的内核 irq_frame, 复用 IRQ0 的出栈序列
    mov esp, eax
    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popa
    add esp, 8
    iret

; #DF 通过 IDT 任务门切换到独立 TSS 后到达这里。
double_fault_task:
    cli
.halt:
    hlt
    jmp .halt

extern isr_handler
extern irq_handler
extern ring3_timer_switch

global irq0
global irq1

irq0:
    ; A uniform frame lets C select either the interrupted kernel or user task.
    push dword 0
    push dword 32
    pusha
    xor eax, eax
    mov ax, ds
    push eax
    xor eax, eax
    mov ax, es
    push eax
    xor eax, eax
    mov ax, fs
    push eax
    xor eax, eax
    mov ax, gs
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call ring3_timer_switch
    add esp, 4
    mov esp, eax

    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popa
    add esp, 8
    iret

irq1:
    pusha
    push dword 33
    call irq_handler
    add esp, 4
    popa
    iret

global syscall
extern syscall_handler
syscall:
    pushad
    call syscall_handler
    mov [esp + 28], eax
    popad
    iret

isr_common_stub:
    pusha
    popa
    iret

irq_common_stub:
    pusha
    popa
    iret