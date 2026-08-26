section .user.text progbits alloc exec nowrite

global user_entry

user_entry:
    mov eax, 7
    mov ebx, 1
    mov ecx, 64
    int 0x80
    mov esi, eax

    mov eax, 7
    mov ebx, 3
    mov ecx, esi
    mov edx, 4096
    int 0x80
    mov edi, eax

    mov eax, 7
    mov ebx, 4
    mov ecx, edi
    int 0x80

    mov eax, 7
    mov ebx, 2
    mov ecx, 4
    mov edx, 32
    int 0x80
    mov edi, eax

    mov eax, 7
    mov ebx, 4
    mov ecx, edi
    int 0x80

    mov eax, 1
    mov ebx, user_message
    int 0x80

.halt:
    jmp .halt

user_message:
    db "Hello from Ring 3 via int 0x80", 10, 0