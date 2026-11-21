bits 32

section .elf.demo progbits alloc nowrite

global fs_demo_start
global fs_demo_end

; User-space demo for the filesystem syscalls. Assembled once and loaded
; from the FAT32 test image as fsdemo.elf; run with `run fsdemo.elf`.
; Exercises SYS_FOPEN/FWRITE/FSTAT/FCLOSE/FOPEN/FREAD/GETCWD/EXIT and
; prints FSDEMO-* markers over SYS_WRITE.

%define LOAD_ADDR 0x00700000
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_WRITE   1
%define SYS_EXIT    10
%define SYS_FOPEN   11
%define SYS_FCLOSE  12
%define SYS_FREAD   13
%define SYS_FWRITE  14
%define SYS_FSTAT   18
%define SYS_GETCWD  19
%define SYSFS_O_RDONLY 0
%define SYSFS_O_RDWR   1
%define FILE_MSG_LEN 13

fs_demo_start:
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
    dd fs_demo_end - fs_demo_code
    dd fs_demo_end - fs_demo_code
    dd 7
    dd 0x1000

    times 0x100 - ($ - fs_demo_start) db 0
fs_demo_code:
    ; SYS_WRITE "FSDEMO-START"
    mov eax, SYS_WRITE
    mov ebx, start_msg + VBASE
    int 0x80

    ; fd = SYS_FOPEN("/fsdemo.txt", O_RDWR)
    mov eax, SYS_FOPEN
    mov ebx, path_str + VBASE
    mov ecx, SYSFS_O_RDWR
    int 0x80
    cmp eax, -1
    je .fail
    cmp eax, 8
    jae .fail
    mov esi, eax

    ; SYS_FWRITE(fd, msg, msg_len)
    mov eax, SYS_FWRITE
    mov ebx, esi
    mov ecx, file_msg + VBASE
    mov edx, FILE_MSG_LEN
    int 0x80
    cmp eax, edx
    jne .fail

    ; SYS_FSTAT("/fsdemo.txt") must equal msg_len
    mov eax, SYS_FSTAT
    mov ebx, path_str + VBASE
    int 0x80
    cmp eax, FILE_MSG_LEN
    jne .fail

    ; SYS_FCLOSE(fd)
    mov eax, SYS_FCLOSE
    mov ebx, esi
    int 0x80
    cmp eax, 0
    jne .fail

    ; fd2 = SYS_FOPEN(path, O_RDONLY)
    mov eax, SYS_FOPEN
    mov ebx, path_str + VBASE
    mov ecx, SYSFS_O_RDONLY
    int 0x80
    cmp eax, -1
    je .fail
    cmp eax, 8
    jae .fail
    mov esi, eax

    ; n = SYS_FREAD(fd2, read_buf, 64)
    mov eax, SYS_FREAD
    mov ebx, esi
    mov ecx, read_buf + VBASE
    mov edx, 64
    int 0x80
    cmp eax, FILE_MSG_LEN
    jne .fail
    mov byte [read_buf + VBASE + eax], 0

    ; print the read-back content
    mov eax, SYS_WRITE
    mov ebx, read_buf + VBASE
    int 0x80

    ; SYS_GETCWD(cwd_buf, 32)
    mov eax, SYS_GETCWD
    mov ebx, cwd_buf + VBASE
    mov ecx, 32
    int 0x80
    test eax, eax
    js .fail
    mov byte [cwd_buf + VBASE + eax], 0
    mov eax, SYS_WRITE
    mov ebx, cwd_msg + VBASE
    int 0x80
    mov eax, SYS_WRITE
    mov ebx, cwd_buf + VBASE
    int 0x80

    ; SYS_WRITE "FSDEMO-DONE"
    mov eax, SYS_WRITE
    mov ebx, done_msg + VBASE
    int 0x80

    ; SYS_EXIT
    mov eax, SYS_EXIT
    int 0x80

.fail:
    mov eax, SYS_WRITE
    mov ebx, fail_msg + VBASE
    int 0x80
    mov eax, SYS_EXIT
    int 0x80
.halt:
    jmp .halt

start_msg:   db "FSDEMO-START", 10, 0
done_msg:    db "FSDEMO-DONE", 10, 0
fail_msg:    db "FSDEMO-FAIL", 10, 0
cwd_msg:     db "FSDEMO-CWD=", 0
path_str:    db "/fsdemo.txt", 0
file_msg:    db "fs-hello-1234", 0

read_buf:
    times 64 db 0
cwd_buf:
    times 32 db 0

    times 0x400 - ($ - fs_demo_start) db 0
fs_demo_end:
