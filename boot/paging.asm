section .boot.text progbits alloc exec nowrite
global paging_enable

; 启用分页
; 参数: uint32_t page_dir_addr (页目录物理地址，通过栈传递)
paging_enable:
    mov eax, [esp + 4]    ; 获取页目录物理地址
    mov edx, cr4
    or edx, 0x20
    mov cr4, edx
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    ret

global paging_load_cr3

; 只切换 CR3（PAE 已启用时用于进程地址空间切换）
paging_load_cr3:
    mov eax, [esp + 4]
    mov cr3, eax
    ret