section .text
global paging_enable

; 启用分页
; 参数: uint32_t page_dir_addr (页目录物理地址，通过栈传递)
paging_enable:
    mov eax, [esp + 4]    ; 获取页目录物理地址
    
    ; 将页目录地址加载到 CR3 寄存器
    mov cr3, eax
    
    ; 启用分页（设置 CR0 的 PG 位）
    mov eax, cr0
    or eax, 0x80000000    ; 设置 PG 位（bit 31）
    mov cr0, eax
    
    ; 分页现在已启用
    ret