bits 32

section .elf.demo progbits alloc nowrite

; heap.elf — P1-1 smoke test: exercise the per-process user heap
; (malloc -> write -> free -> exit) and report HEAP-DONE. Run twice
; by the integration suite; each run must complete without a panic.
;
; NOTE: the QEMU test disk has no shell3.elf, so both vts fall back
; to the ring0 kernel shell and only the foreground process's
; IO_WRITE reaches COM3 — a concurrent two-vt magic-value check is
; not possible in this harness. Strict isolation assertions live in
; AUDIT P1-1's design; this demo only proves the heap path works.

global heap_start
global heap_end

%define LOAD_ADDR 0x00800000
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_IO       1
%define SYS_MEMORY   3
%define SYS_PROCESS  5
%define IO_WRITE     1
%define MEMORY_MALLOC 1
%define MEMORY_FREE  4
%define PROC_EXIT    2

heap_start:
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
    dd heap_end - heap_code
    dd heap_end - heap_code
    dd 5
    dd 0x1000

    times 0x100 - ($ - heap_start) db 0
heap_code:
    ; small block
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_MALLOC
    mov ecx, 64
    int 0x80
    test eax, eax
    jz .fail
    mov ebp, eax
    mov dword [ebp], 0x34A0001

    ; big block
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_MALLOC
    mov ecx, 8000
    int 0x80
    test eax, eax
    jz .fail
    mov esi, eax
    mov dword [esi], 0x34A0002
    mov dword [esi + 8000 - 4], 0x34A0003

    ; free both
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_FREE
    mov ecx, ebp
    int 0x80
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_FREE
    mov ecx, esi
    int 0x80

    mov ecx, ok_msg + VBASE
    jmp .print
.fail:
    mov ecx, fail_msg + VBASE
.print:
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    int 0x80

    mov eax, SYS_PROCESS
    mov ebx, PROC_EXIT
    int 0x80
.halt:
    jmp .halt

ok_msg:    db "HEAP-DONE", 10, 0
fail_msg:  db "HEAP-FAIL", 10, 0

    times 0x200 - ($ - heap_start) db 0
heap_end:
