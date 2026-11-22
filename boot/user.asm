section .user.text progbits alloc exec nowrite

global user_entry

; Syscall ABI: eax = class, ebx = sub-op, args in ecx/edx/esi.
%define SYS_IO       1
%define SYS_TIME     2
%define SYS_PROCESS  5
%define IO_WRITE     1
%define TIME_GET     1
%define PROC_GETPID  1
%define PROC_YIELD   3

user_entry:
    sub esp, 64
.run:
    mov eax, SYS_TIME
    mov ebx, TIME_GET
    int 0x80
    mov ebp, eax

    mov eax, SYS_PROCESS
    mov ebx, PROC_GETPID
    int 0x80
    mov ebx, eax

    mov edi, esp
    mov esi, user_prefix
.copy_prefix:
    lodsb
    test al, al
    jz .prefix_done
    stosb
    jmp .copy_prefix

.prefix_done:
    mov eax, ebx
    xor ecx, ecx
.digits:
    xor edx, edx
    mov ebx, 10
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .digits
.print_digits:
    pop eax
    add al, '0'
    stosb
    loop .print_digits
    mov byte [edi], 10
    inc edi
    mov byte [edi], 0

    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, esp
    int 0x80

.wait_next_second:
    mov eax, SYS_TIME
    mov ebx, TIME_GET
    int 0x80
    cmp eax, ebp
    jne .run
    mov eax, SYS_PROCESS
    mov ebx, PROC_YIELD
    int 0x80
    jmp .wait_next_second

user_prefix:
    db "RING3 is RUN, procss_id: ", 0
