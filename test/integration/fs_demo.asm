bits 32

section .elf.demo progbits alloc nowrite

global fs_demo_start
global fs_demo_end

; User-space demo for the filesystem syscalls. Assembled once and loaded
; from the FAT32 test image as fsdemo.elf; run with `run fsdemo.elf`.
; Exercises FS_OPEN/FS_WRITE/FS_STAT/FS_CLOSE/FS_OPEN/FS_READ/FS_GETCWD/
; PROC_EXIT and prints FSDEMO-* markers over IO_WRITE.
; Syscall ABI: eax = class, ebx = sub-op, args in ecx/edx/esi.

%define LOAD_ADDR 0x00700000
%define VBASE (LOAD_ADDR - 0x100)

%define SYS_IO       1
%define SYS_TIME     2
%define SYS_PROCESS  5
%define SYS_FS       6
%define IO_WRITE     1
%define TIME_SLEEP   3
%define PROC_EXIT    2
%define FS_OPEN      1
%define FS_CLOSE     2
%define FS_READ      3
%define FS_WRITE     4
%define FS_STAT      8
%define FS_GETCWD    9
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
    ; IO_WRITE "FSDEMO-START"
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, start_msg + VBASE
    int 0x80

    ; TIME_SLEEP(100000) ~= 100 ms via the calibrated TSC
    mov eax, SYS_TIME
    mov ebx, TIME_SLEEP
    mov ecx, 100000
    int 0x80
    test eax, eax
    jnz .fail

    ; fd = FS_OPEN("/fsdemo.txt", O_RDWR)
    mov eax, SYS_FS
    mov ebx, FS_OPEN
    mov ecx, path_str + VBASE
    mov edx, SYSFS_O_RDWR
    int 0x80
    cmp eax, -1
    je .fail
    cmp eax, 8
    jae .fail
    mov ebp, eax

    ; FS_WRITE(fd, msg, msg_len)
    mov eax, SYS_FS
    mov ebx, FS_WRITE
    mov ecx, ebp
    mov edx, file_msg + VBASE
    mov esi, FILE_MSG_LEN
    int 0x80
    cmp eax, FILE_MSG_LEN
    jne .fail

    ; FS_STAT("/fsdemo.txt") must equal msg_len
    mov eax, SYS_FS
    mov ebx, FS_STAT
    mov ecx, path_str + VBASE
    int 0x80
    cmp eax, FILE_MSG_LEN
    jne .fail

    ; FS_CLOSE(fd)
    mov eax, SYS_FS
    mov ebx, FS_CLOSE
    mov ecx, ebp
    int 0x80
    cmp eax, 0
    jne .fail

    ; fd2 = FS_OPEN(path, O_RDONLY)
    mov eax, SYS_FS
    mov ebx, FS_OPEN
    mov ecx, path_str + VBASE
    mov edx, SYSFS_O_RDONLY
    int 0x80
    cmp eax, -1
    je .fail
    cmp eax, 8
    jae .fail
    mov ebp, eax

    ; n = FS_READ(fd2, read_buf, 64)
    mov eax, SYS_FS
    mov ebx, FS_READ
    mov ecx, ebp
    mov edx, read_buf + VBASE
    mov esi, 64
    int 0x80
    cmp eax, FILE_MSG_LEN
    jne .fail
    mov byte [read_buf + VBASE + eax], 0

    ; print the read-back content
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, read_buf + VBASE
    int 0x80

    ; FS_GETCWD(cwd_buf, 32)
    mov eax, SYS_FS
    mov ebx, FS_GETCWD
    mov ecx, cwd_buf + VBASE
    mov edx, 32
    int 0x80
    test eax, eax
    js .fail
    mov byte [cwd_buf + VBASE + eax], 0
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, cwd_msg + VBASE
    int 0x80
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, cwd_buf + VBASE
    int 0x80

    ; IO_WRITE "FSDEMO-DONE"
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, done_msg + VBASE
    int 0x80

    ; PROC_EXIT
    mov eax, SYS_PROCESS
    mov ebx, PROC_EXIT
    xor ecx, ecx
    int 0x80

.fail:
    mov eax, SYS_IO
    mov ebx, IO_WRITE
    mov ecx, fail_msg + VBASE
    int 0x80
    mov eax, SYS_PROCESS
    mov ebx, PROC_EXIT
    xor ecx, ecx
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
