bits 32

section .elf.demo progbits alloc nowrite

; sleep.elf — R3-N2 regression: TIME_SLEEP must park the process in
; SLEEPING so the kernel idle loop (vt_service) keeps running.
; Sleeps 500 ms, prints SLEEP-OK, exits.

global sleep_start
global sleep_end

; 0x00900000 is reserved for the per-process sigreturn trampoline
; (USER_SIG_TRAMPOLINE); loading the image there gets its first page
; overwritten by the trampoline stub.
%define LOAD_ADDR 0x00830000
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_IO       1
%define SYS_TIME     2
%define SYS_PROCESS  5
%define IO_WRITE     1
%define TIME_SLEEP   3
%define PROC_EXIT    2

sleep_start:
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
    dd sleep_end - sleep_code
    dd sleep_end - sleep_code
    dd 5
    dd 0x1000

    times 0x100 - ($ - sleep_start) db 0
sleep_code:
    mov eax, SYS_TIME
    mov ebx, TIME_SLEEP
    mov ecx, 500000        ; 500 ms
    int 0x80

    mov ecx, ok_msg + VBASE
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    int 0x80

    mov eax, SYS_PROCESS
    mov ebx, PROC_EXIT
    int 0x80
.halt:
    jmp .halt

ok_msg:    db "SLEEP-OK", 10, 0

    times 0x200 - ($ - sleep_start) db 0
sleep_end:
