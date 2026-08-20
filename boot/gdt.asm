section .text
global gdt_load

; 加载 GDT
; 参数: struct gdt_ptr* ptr (通过栈传递)
gdt_load:
    mov eax, [esp + 4]    ; 获取 GDT 指针
    lgdt [eax]            ; 加载 GDT
    
    ; 重新加载数据段寄存器
    mov ax, 0x10          ; 数据段选择子
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; 刷新代码段寄存器 (CS) —— 必须做！
    jmp 0x08:.flush       ; 远跳转到 .flush 标签（0x08 是内核代码段选择子）
.flush:
    ret