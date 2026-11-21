bits 32

section .elf.demo progbits alloc nowrite

global sig_demo_start
global sig_demo_end

; User-space demo for signal handler delivery. Assembled once and loaded
; from the FAT32 test image as sigdemo.elf; run with `run sigdemo.elf`.
; Installs a SIGINT handler via SYS_SIGNAL, prints SIGDEMO-ARMED, then
; yields until Ctrl+C delivers SIGINT. The handler prints
; SIGDEMO-HANDLED, clobbers ecx, and returns through the kernel restorer
; (SYS_SIGRETURN), which must restore the interrupted context: the main
; loop then verifies ecx and prints SIGDEMO-AFTER/SIGDEMO-DONE.

%define LOAD_ADDR 0x00700000
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_WRITE    1
%define SYS_EXIT     10
%define SYS_YIELD    21
%define SYS_SIGNAL   23
%define SIGINT       2
%define SENTINEL     0x12345678

sig_demo_start:
    db 0x7F, "ELF", 1, 1, 1, 0
    times 8 db 0
    dw 2
    dw 3
    dd 1
    dd LOAD_ADDR
    dd 52
    dd 0
    dd 0
    dw 52
    dw 32
    dw 1
    dw 0
    dw 0
    dw 0

    dd 1
    dd 0x100
    dd LOAD_ADDR
    dd LOAD_ADDR
    dd sig_demo_end - sig_demo_code
    dd sig_demo_end - sig_demo_code
    dd 7
    dd 0x1000

    times 0x100 - ($ - sig_demo_start) db 0
sig_demo_code:
    ; SYS_WRITE "SIGDEMO-START"
    mov eax, SYS_WRITE
    mov ebx, start_msg + VBASE
    int 0x80

    ; SYS_SIGNAL(SIGINT, sig_handler)
    mov eax, SYS_SIGNAL
    mov ebx, SIGINT
    mov ecx, sig_handler + VBASE
    int 0x80
    cmp eax, -1
    je .fail

    ; SYS_WRITE "SIGDEMO-ARMED"
    mov eax, SYS_WRITE
    mov ebx, armed_msg + VBASE
    int 0x80

    mov ecx, SENTINEL
.wait_loop:
    mov eax, SYS_YIELD
    int 0x80
    cmp byte [handled_flag + VBASE], 0
    je .wait_loop

    ; The handler clobbered ecx; sigreturn must have restored it.
    cmp ecx, SENTINEL
    jne .fail

    ; SYS_WRITE "SIGDEMO-AFTER"
    mov eax, SYS_WRITE
    mov ebx, after_msg + VBASE
    int 0x80

    ; SYS_WRITE "SIGDEMO-DONE"
    mov eax, SYS_WRITE
    mov ebx, done_msg + VBASE
    int 0x80

    ; SYS_EXIT
    mov eax, SYS_EXIT
    int 0x80

.fail:
    mov eax, SYS_WRITE
    mov ebx, fail_msg + VBASE
    int 0x80
    mov eax, SYS_EXIT
    int 0x80
.halt:
    jmp .halt

; Entered via iret with [esp]=restorer, [esp+4]=sig. `ret` jumps to the
; kernel-placed restorer which issues SYS_SIGRETURN.
sig_handler:
    mov byte [handled_flag + VBASE], 1
    mov ecx, 0xDEADBEEF        ; clobber a caller-saved register
    mov eax, SYS_WRITE
    mov ebx, handled_msg + VBASE
    int 0x80
    ret

start_msg:    db "SIGDEMO-START", 10, 0
armed_msg:    db "SIGDEMO-ARMED", 10, 0
handled_msg:  db "SIGDEMO-HANDLED", 10, 0
after_msg:    db "SIGDEMO-AFTER", 10, 0
done_msg:     db "SIGDEMO-DONE", 10, 0
fail_msg:     db "SIGDEMO-FAIL", 10, 0

handled_flag:
    db 0

    times 0x400 - ($ - sig_demo_start) db 0
sig_demo_end:
