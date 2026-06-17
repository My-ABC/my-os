section .multiboot
align 4
    dd 0x1BADB002          ; 魔数
    dd 0x03                ; 标志（请求内存信息）
    dd -(0x1BADB002 + 0x03) ; 校验和

section .text
global start
extern kernel_main
extern multiboot_info

start:
    cli
    mov esp, stack_top
    
    ; 保存 Multiboot 信息结构地址
    push ebx               ; ebx 指向 multiboot_info 结构
    call kernel_main
    hlt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top: