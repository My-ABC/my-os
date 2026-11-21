section .user.text progbits alloc exec nowrite

global user_entry

%define SYS_WRITE 1
%define SYS_TIME 6
%define SYS_GETPID 9
%define SYS_YIELD 21

user_entry:
    sub esp, 64
.run:
    mov eax, SYS_TIME
    int 0x80
    mov ebp, eax

    mov eax, SYS_GETPID
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

    mov eax, SYS_WRITE
    mov ebx, esp
    int 0x80

.wait_next_second:
    mov eax, SYS_TIME
    int 0x80
    cmp eax, ebp
    jne .run
    mov eax, SYS_YIELD
    int 0x80
    jmp .wait_next_second

user_prefix:
    db "RING3 is RUN, procss_id: ", 0