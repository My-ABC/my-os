bits 32

section .elf.demo progbits alloc nowrite

global elf_demo_start
global elf_demo_end

elf_demo_start:
    db 0x7F, "ELF", 1, 1, 1, 0
    times 8 db 0
    dw 2
    dw 3
    dd 1
    dd 0x00500000
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
    dd 0x00500000
    dd 0x00500000
    dd elf_demo_end - elf_demo_code
    dd elf_demo_end - elf_demo_code
    dd 5
    dd 0x1000

    times 0x100 - ($ - elf_demo_start) db 0
elf_demo_code:
    ; Install the SIGINT handler first, so "Hello" also means "armed".
    mov eax, 7         ; SYS_SIG
    mov ebx, 2         ; SIG_SIGNAL
    mov ecx, 2         ; SIGINT
    mov edx, bye_handler - elf_demo_code + 0x00500000
    int 0x80

    mov eax, 1         ; SYS_IO
    mov ebx, 1         ; IO_WRITE
    mov ecx, hello_msg - elf_demo_code + 0x00500000
    int 0x80

    ; Wait for SIGINT; the handler prints "Bye" and exits.
.wait:
    mov eax, 5         ; SYS_PROCESS
    mov ebx, 3         ; PROC_YIELD
    int 0x80
    jmp .wait

bye_handler:
    mov eax, 1         ; SYS_IO
    mov ebx, 1         ; IO_WRITE
    mov ecx, bye_msg - elf_demo_code + 0x00500000
    int 0x80
    mov eax, 5         ; SYS_PROCESS
    mov ebx, 2         ; PROC_EXIT
    xor ecx, ecx
    int 0x80
.halt:
    jmp .halt
    times 0x160 - ($ - elf_demo_start) db 0
hello_msg:
    db "Hello from ELF32", 10, 0
bye_msg:
    db "Bye", 10, 0
elf_demo_end: