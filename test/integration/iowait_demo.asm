bits 32

section .elf.demo progbits alloc nowrite

; iowait.elf — R3-N3 regression: a process blocked in IO_READ mid-line
; (chars typed, no Enter yet) must yield so vt_service keeps running.
; Reads one line, prints LINE-OK with the byte count, exits.

global iowait_start
global iowait_end

%define LOAD_ADDR 0x00840000
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_IO       1
%define SYS_PROCESS  5
%define IO_WRITE     1
%define IO_READ      2
%define PROC_EXIT    2

iowait_start:
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
    dd iowait_end - iowait_code
    dd iowait_end - iowait_code
    dd 5
    dd 0x1000

    times 0x100 - ($ - iowait_start) db 0
iowait_code:
    ; buf at a fixed user address inside our own page is not needed —
    ; use the stack: esp points into the user stack already.
    mov ecx, esp
    sub ecx, 128           ; scratch buffer below the stack pointer
    mov eax, SYS_IO
    mov ebx, IO_READ
    mov edx, 64
    int 0x80               ; blocks until Enter; may yield mid-line
    test eax, eax
    js .fail

    mov ecx, ok_msg + VBASE
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    int 0x80

    mov eax, SYS_PROCESS
    mov ebx, PROC_EXIT
    int 0x80
.fail:
    mov ecx, fail_msg + VBASE
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    int 0x80
    mov eax, SYS_PROCESS
    mov ebx, PROC_EXIT
    int 0x80
.halt:
    jmp .halt

ok_msg:    db "LINE-OK", 10, 0
fail_msg:  db "LINE-FAIL", 10, 0

    times 0x200 - ($ - iowait_start) db 0
iowait_end:
