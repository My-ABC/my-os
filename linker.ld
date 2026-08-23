ENTRY(start)

SECTIONS
{
    . = 1M;

    .multiboot ALIGN(4K) :
    {
        *(.multiboot)
    }

    .text ALIGN(4K) :
    {
        *(.text)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata)
    }

    .data ALIGN(4K) :
    {
        *(.data)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
        
        /* 为分页结构预留空间 */
        /* 页目录: 4KB */
        /* 预留64KB给页表（16个页表，支持64MB虚拟内存） */
        . = ALIGN(4K);
        paging_structures_start = .;
        . = . + 0x10000;  /* 预留64KB给分页结构 */
        paging_structures_end = .;
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.gnu.build-id)
    }
}