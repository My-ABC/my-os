bits 32

section .elf.demo progbits alloc nowrite

; bufcheck.elf — R3-N1 regression: syscall buffers pointing into the
; borrowed low-4MiB supervisor identity map (VA == physical address,
; no PAE_USER) must be rejected with -1 instead of letting the kernel
; read/write arbitrary physical pages. Legal buffers must still work.

global bufcheck_start
global bufcheck_end

%define LOAD_ADDR 0x00700000
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_IO       1
%define IO_WRITE     1
%define IO_READ      3

bufcheck_start:
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
    dd bufcheck_end - bufcheck_code
    dd bufcheck_end - bufcheck_code
    dd 5
    dd 0x1000

    times 0x100 - ($ - bufcheck_start) db 0
bufcheck_code:
    ; IO_WRITE(0x00100000): borrowed supervisor page, no PAE_USER -> -1
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, 0x00100000
    int 0x80
    cmp eax, -1
    je .bad_write_ok
    mov ecx, badw_fail + VBASE
    jmp .print
.bad_write_ok:
    mov ecx, badw_rej + VBASE
.print:
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    int 0x80

    ; IO_READ(0x00100000, 16): same range, write direction -> -1
    mov eax, SYS_IO
    mov ebx, IO_READ
    mov ecx, 0x00100000
    mov edx, 16
    int 0x80
    cmp eax, -1
    je .bad_read_ok
    mov ecx, badr_fail + VBASE
    jmp .print2
.bad_read_ok:
    mov ecx, badr_rej + VBASE
.print2:
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    int 0x80

    ; IO_WRITE(ok_msg): legal read-only user page -> 0, prints itself
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, ok_msg + VBASE
    int 0x80
    test eax, eax
    jnz .good_write_fail

    ; IO_READ(buf on user stack, 64): legal writable user page ->
    ; blocks for a line, returns its length (>= 0)
    mov eax, SYS_IO
    mov ebx, IO_READ
    mov ecx, esp
    sub ecx, 0x800
    mov edx, 64
    int 0x80
    test eax, eax
    js .good_read_fail
    mov ecx, goodr_ok + VBASE
    jmp .print3
.good_write_fail:
    mov ecx, goodw_fail + VBASE
    jmp .print3
.good_read_fail:
    mov ecx, goodr_fail + VBASE
.print3:
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    int 0x80

.halt:
    jmp .halt

badw_rej:   db "BADW-REJ", 10, 0
badw_fail:  db "BADW-FAIL", 10, 0
badr_rej:   db "BADR-REJ", 10, 0
badr_fail:  db "BADR-FAIL", 10, 0
ok_msg:     db "GOODW-OK", 10, 0
goodw_fail: db "GOODW-FAIL", 10, 0
goodr_ok:   db "GOODR-OK", 10, 0
goodr_fail: db "GOODR-FAIL", 10, 0

    times 0x400 - ($ - bufcheck_start) db 0
bufcheck_end:
