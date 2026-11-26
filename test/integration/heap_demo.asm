bits 32

section .elf.demo progbits alloc nowrite

; heap.elf — P1-1 regression: the user heap free list and bump pointer
; must be per-process. Two copies run concurrently on vt0 and vt1.
; Each allocates a small block (freed onto the free list) and a big
; block holding a magic value, then yields so the peer can run, then
; allocates again — the allocation must not trip over the peer's freed
; block headers, and the big block must still hold its magic.
;
; TIME_SLEEP is unusable here: it busy-waits in kernel context without
; yielding (R3-N2), which would freeze the peer. PROC_YIELD loops give
; the peer CPU time instead.

global heap_start
global heap_end

%define LOAD_ADDR 0x00800000
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_IO       1
%define SYS_TIME     2
%define SYS_MEMORY   3
%define SYS_PROCESS  5
%define IO_WRITE     1
%define TIME_SLEEP   3
%define MEMORY_MALLOC 1
%define MEMORY_FREE  4
%define PROC_GETPID  1
%define PROC_YIELD   3
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
    ; edi = magic derived from pid so each process verifies its own data
    mov eax, SYS_PROCESS
    mov ebx, PROC_GETPID
    int 0x80
    mov edi, eax
    or  edi, 0x5A5A0000

    ; ebp = small block (will be freed onto the free list)
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_MALLOC
    mov ecx, 64
    int 0x80
    test eax, eax
    jz .fail
    mov ebp, eax

    ; esi = big block holding the magic
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_MALLOC
    mov ecx, 8000
    int 0x80
    test eax, eax
    jz .fail
    mov esi, eax
    mov [esi], edi
    mov [esi + 4], edi

    ; free the small block, then yield so the peer publishes its own
    ; freed block before we allocate again
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_FREE
    mov ecx, ebp
    int 0x80

    mov ecx, 400
.yield_loop:
    mov eax, SYS_PROCESS
    mov ebx, PROC_YIELD
    int 0x80
    loop .yield_loop

    ; allocate again: must not reuse or trip over the peer's freed
    ; block headers (per-mm free lists make this a fresh allocation)
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_MALLOC
    mov ecx, 8000
    int 0x80
    test eax, eax
    jz .fail

    mov eax, [esi]
    cmp eax, edi
    jne .fail
    mov eax, [esi + 4]
    cmp eax, edi
    jne .fail

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

ok_msg:    db "HEAP-OK", 10, 0
fail_msg:  db "HEAP-FAIL", 10, 0

    times 0x200 - ($ - heap_start) db 0
heap_end:
