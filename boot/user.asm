section .user.text progbits alloc exec nowrite

global user_entry

user_entry:
    mov eax, 1
    mov ebx, user_message
    int 0x80

.halt:
    jmp .halt

user_message:
    db "Hello from Ring 3 via int 0x80", 10, 0