bits 32

section .elf.demo progbits alloc nowrite

global fault_demo_start
global fault_demo_end

; Assembled three times with -D FAULT_MODE=N at distinct vaddrs so the
; images can coexist in the shared address space:
;   0 = stack.elf  : grow the user stack to force demand mapping, then halt
;   1 = faultu.elf : print a marker, then touch an unregistered user address
;   2 = faultp.elf : print a marker, then write to its own read-only code page
;   3 = oom.elf    : exhaust the user heap, then fault until demand mapping fails
;   4 = kfault.elf : pass an unregistered user pointer to IO_WRITE so the
;                    kernel faults while dereferencing it (panic demo)
%ifndef FAULT_MODE
%define FAULT_MODE 0
%endif

; Loaded from file offset 0x100, so a label's runtime address is
; label + VBASE where VBASE = LOAD_ADDR - 0x100.
%define LOAD_ADDR (0x00600000 + FAULT_MODE * 0x2000)
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_IO       1
%define SYS_MEMORY   3
%define IO_WRITE     1
%define MEMORY_MALLOC 1

fault_demo_start:
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
    dd fault_demo_end - fault_demo_code
    dd fault_demo_end - fault_demo_code
    dd 5
    dd 0x1000

    times 0x100 - ($ - fault_demo_start) db 0
fault_demo_code:
%if FAULT_MODE = 0
    mov esi, esp
    mov edi, esp
    sub edi, 0x6000
.touch:
    mov dword [edi], 0x51515151
    add edi, 0x1000
    cmp edi, esi
    jb .touch
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, stack_ok + VBASE
    int 0x80
%elif FAULT_MODE = 1
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, unmapped_msg + VBASE
    int 0x80
    mov eax, [0x80000000]
%elif FAULT_MODE = 2
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, prot_msg + VBASE
    int 0x80
    mov dword [fault_demo_code + VBASE], 0
%elif FAULT_MODE = 3
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, oom_msg + VBASE
    int 0x80
    ; Drain physical memory through the global user heap. Large blocks go
    ; first; once they fail, 4 KiB blocks shrink the remainder to at most
    ; one free page. Blocks are intentionally leaked: heap memory is not
    ; reclaimed when a process dies.
    mov ecx, 0x00100000
.big:
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_MALLOC
    int 0x80
    test eax, eax
    jnz .big
    mov ecx, 0x00001000
.small:
    mov eax, SYS_MEMORY
    mov ebx, MEMORY_MALLOC
    int 0x80
    test eax, eax
    jnz .small
    ; 0x30000000 sits inside USER_HEAP_* but was never allocated, so each
    ; touch is a permitted demand-map fault. With the PMM empty the handler
    ; must kill this process instead of mapping the page.
    mov edi, 0x30000000
.touch:
    mov dword [edi], 0x4F4F4D21
    add edi, 0x1000
    cmp edi, 0x3FF00000
    jb .touch
%else
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, kfault_msg + VBASE
    int 0x80
    ; 0x50000000 is a user address outside every registered range; the
    ; syscall handler dereferences it in kernel mode and must panic.
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, 0x50000000
    int 0x80
%endif
.halt:
    jmp .halt

stack_ok:     db "STACK-OK", 10, 0
unmapped_msg: db "UNMAPPED-NEXT", 10, 0
prot_msg:     db "PROT-NEXT", 10, 0
oom_msg:      db "OOM-NEXT", 10, 0
kfault_msg:   db "KFAULT-NEXT", 10, 0

    times 0x200 - ($ - fault_demo_start) db 0
fault_demo_end:
