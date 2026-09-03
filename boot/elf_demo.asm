section .elf.demo progbits alloc nowrite

global elf_demo_start
global elf_demo_end

elf_demo_start:
    db 0x7F, "ELF", 1, 1, 1, 0
    times 8 db 0
    dw 2
    dw 3
    dd 1
    dd 0x00400000
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
    dd 0x00400000
    dd 0x00400000
    dd elf_demo_end - elf_demo_code
    dd elf_demo_end - elf_demo_code
    dd 5
    dd 0x1000

    times 0x100 - ($ - elf_demo_start) db 0
elf_demo_code:
    mov eax, 1
    mov ebx, 0x00400010
    int 0x80
.halt:
    jmp .halt
    times 0x110 - ($ - elf_demo_start) db 0
    db "Hello from ELF32", 10, 0
elf_demo_end: