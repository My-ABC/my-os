section .multiboot

align 4
    dd 0x1BADB002
    dd 0x07
    dd -(0x1BADB002 + 0x07)
    dd 0
    dd 640
    dd 480
    dd 32

%define KERNEL_VIRT_BASE 0xC0000000

section .boot.text progbits alloc exec nowrite
global start
extern kmain
global stack_top
global early_page_directory

start:
    mov esp, stack_top
    push ebx

    mov edi, early_page_directory
    xor eax, eax
    mov ecx, 1024
    rep stosd

    mov edi, early_page_table_low
    xor ebx, ebx
    mov edx, 4
.next_low_table:
    mov ecx, 1024
.fill_low:
    mov eax, ebx
    or eax, 0x003
    stosd
    add ebx, 0x1000
    loop .fill_low
    dec edx
    jnz .next_low_table

    mov edi, early_page_table_high
     mov ebx, 0x00100000
    mov edx, 4
.next_high_table:
    mov ecx, 1024
.fill_high:
    mov eax, ebx
    or eax, 0x003
    stosd
    add ebx, 0x1000
    loop .fill_high
    dec edx
    jnz .next_high_table

    mov dword [early_page_directory + 0], early_page_table_low + 0x003
    mov dword [early_page_directory + 4], early_page_table_low + 0x1003
    mov dword [early_page_directory + 8], early_page_table_low + 0x2003
    mov dword [early_page_directory + 12], early_page_table_low + 0x3003
    mov dword [early_page_directory + 3072], early_page_table_high + 0x003
    mov dword [early_page_directory + 3076], early_page_table_high + 0x1003
    mov dword [early_page_directory + 3080], early_page_table_high + 0x2003
    mov dword [early_page_directory + 3084], early_page_table_high + 0x3003
    mov eax, early_page_directory
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    call kmain
    cli
.hang:
    hlt
    jmp .hang

section .boot.bss nobits alloc noexec write
align 4096
early_page_directory:
    resb 4096
early_page_table_low:
    resb 16384
early_page_table_high:
    resb 16384

align 16
stack_bottom:
    resb 16384
stack_top: