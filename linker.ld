ENTRY(start)

KERNEL_VIRT_BASE = 0xC0000000;
KERNEL_PHYS_BASE = 1M;

SECTIONS
{
    . = KERNEL_PHYS_BASE;

    .multiboot ALIGN(4K) :
    {
        *(.multiboot)
    }

    .boot.text ALIGN(4K) :
    {
        *(.boot.text)
    }

    .boot.bss ALIGN(4K) (NOLOAD) :
    {
        *(.boot.bss)
    }

    . = ALIGN(4M);
    .user.text :
    {
        user_start = .;
        *(.user.text)
        user_end = .;
    }

    boot_end = ALIGN(user_end);
    . = KERNEL_VIRT_BASE + (boot_end - KERNEL_PHYS_BASE);

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.text)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.rodata)
        *(.elf.demo)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
    {
        *(.data)
    }

    .bss ALIGN(4K) (NOLOAD) : AT(ADDR(.bss) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)
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
        kernel_end = .;
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.gnu.build-id)
    }
}