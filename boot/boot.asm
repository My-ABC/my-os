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

start:
    mov esp, stack_top
    push ebx

    ; Require CPUID PAE support (CPUID leaf 1, EDX bit 6).
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x00200000
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    jz .pae_hang
    mov eax, 1
    cpuid
    test edx, 1 << 6
    jz .pae_hang
    mov edi, early_pae_pdpt
    xor eax, eax
    mov ecx, 8
    rep stosd
    mov dword [early_pae_pdpt + 0], early_pae_pd0 + 0x001
    mov dword [early_pae_pdpt + 4], 0
    mov dword [early_pae_pdpt + 8], early_pae_pd1 + 0x001
    mov dword [early_pae_pdpt + 12], 0
    mov dword [early_pae_pdpt + 16], early_pae_pd2 + 0x001
    mov dword [early_pae_pdpt + 20], 0
    mov dword [early_pae_pdpt + 24], early_pae_pd3 + 0x001
    mov dword [early_pae_pdpt + 28], 0

    ; Zero all four 512-entry PAE page directories.
    mov edi, early_pae_pd0
    xor eax, eax
    mov ecx, 4096
    rep stosd

    mov edi, early_pae_pd0
    mov ebx, early_pae_pt_low
    mov ecx, 8
.fill_low_pdes:
    mov eax, ebx
    or eax, 0x003
    mov dword [edi], eax
    mov dword [edi + 4], 0
    add ebx, 0x1000
    add edi, 8
    loop .fill_low_pdes

    ; Map the high kernel alias across 16 MiB so kernel BSS/PMM metadata
    ; remain accessible before and after the kernel paging structures change.
    mov edi, early_pae_pd3
    mov ebx, early_pae_pt_high
    mov ecx, 8
.fill_high_pdes:
    mov eax, ebx
    or eax, 0x003
    mov dword [edi], eax
    mov dword [edi + 4], 0
    add ebx, 0x1000
    add edi, 8
    loop .fill_high_pdes

    ; Sixteen MiB identity mapping for early boot and Multiboot structures.
    mov edi, early_pae_pt_low
    xor ebx, ebx
    mov ecx, 4096
.fill_pae_low:
    mov eax, ebx
    or eax, 0x003
    mov dword [edi], eax
    mov dword [edi + 4], 0
    add ebx, 0x1000
    add edi, 8
    loop .fill_pae_low

    ; C0000000 -> physical 1 MiB, spanning 16 MiB.
    mov edi, early_pae_pt_high
    mov ebx, 0x00100000
    mov ecx, 4096
.fill_pae_high:
    mov eax, ebx
    or eax, 0x003
    mov dword [edi], eax
    mov dword [edi + 4], 0
    add ebx, 0x1000
    add edi, 8
    loop .fill_pae_high

    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, early_pae_pdpt
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    mov ebx, [esp]
    jmp .pae_entry
.pae_hang:
    cli
    hlt
    jmp .pae_hang
.pae_entry:
    call kmain
    cli
.hang:
    hlt
    jmp .hang

section .boot.bss nobits alloc noexec write
alignb 4096
early_pae_pdpt:
    resb 32
alignb 4096
early_pae_pd0:
    resb 4096
early_pae_pd1:
    resb 4096
early_pae_pd2:
    resb 4096
early_pae_pd3:
    resb 4096
alignb 4096
early_pae_pt_low:
    resb 32768
early_pae_pt_high:
    resb 32768

alignb 16
stack_bottom:
    resb 16384
stack_top: