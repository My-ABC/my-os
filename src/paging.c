#include "paging.h"
#include "serial.h"
#include "string.h"

// 页目录（物理地址对齐到4KB）
static struct page_directory page_directory __attribute__((aligned(PAGE_SIZE)));

// 页表数组（32个页表，支持128MB虚拟内存）
static struct page_table page_tables[32] __attribute__((aligned(PAGE_SIZE)));

// 当前页目录的物理地址
static uint32_t current_page_dir_phys;

// 内核虚拟地址起始位置（1MB，与物理地址相同）
#define KERNEL_VIRTUAL_BASE 0x100000

// 内核大小（约8MB，预留足够空间）
#define KERNEL_SIZE 0x800000

// 将物理地址转换为页帧号
#define PHYS_TO_FRAME(phys) ((phys) >> 12)

// 将页帧号转换为物理地址
#define FRAME_TO_PHYS(frame) ((frame) << 12)

// 获取虚拟地址的页目录索引
#define VIRT_TO_PD_INDEX(virt) (((virt) >> 22) & 0x3FF)

// 获取虚拟地址的页表索引
#define VIRT_TO_PT_INDEX(virt) (((virt) >> 12) & 0x3FF)

// 获取页目录项指针
static struct page_directory_entry* get_pd_entry(uint32_t virtual_addr) {
    uint32_t pd_index = VIRT_TO_PD_INDEX(virtual_addr);
    return &page_directory.entries[pd_index];
}

// 获取页表项指针
static struct page_table_entry* get_pt_entry(uint32_t virtual_addr) {
    uint32_t pd_index = VIRT_TO_PD_INDEX(virtual_addr);
    uint32_t pt_index = VIRT_TO_PT_INDEX(virtual_addr);
    
    struct page_directory_entry* pd_entry = &page_directory.entries[pd_index];
    
    // 检查页表是否存在
    if (!pd_entry->present) {
        return NULL;
    }
    
    // 获取页表地址
    uint32_t pt_addr = FRAME_TO_PHYS(pd_entry->frame_addr);
    struct page_table* pt = (struct page_table*)pt_addr;
    
    return &pt->entries[pt_index];
}

// 刷新TLB
void paging_flush_tlb(uint32_t virtual_addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

// 映射虚拟地址到物理地址
void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    // 确保地址对齐到页边界
    virtual_addr = virtual_addr & ~(PAGE_SIZE - 1);
    physical_addr = physical_addr & ~(PAGE_SIZE - 1);
    
    uint32_t pd_index = VIRT_TO_PD_INDEX(virtual_addr);
    uint32_t pt_index = VIRT_TO_PT_INDEX(virtual_addr);
    
    serial_print("[PAGING] Mapping page: virt=");
    serial_print_hex(virtual_addr);
    serial_print(", phys=");
    serial_print_hex(physical_addr);
    serial_print("\n");
    
    // 获取或创建页目录项
    struct page_directory_entry* pd_entry = &page_directory.entries[pd_index];
    
    if (!pd_entry->present) {
        // 页表不存在，需要创建
        serial_print("[PAGING] Creating new page table for PD index ");
        serial_print_dec(pd_index);
        serial_print("\n");
        
        // 检查是否超出预分配的页表数组
        if (pd_index >= 32) {
            serial_print("[PAGING] Error: Page table index exceeds pre-allocated array\n");
            return;
        }
        
        // 使用预分配的页表
        uint32_t pt_phys_addr = (uint32_t)&page_tables[pd_index];
        
        // 清空页表
        memset(&page_tables[pd_index], 0, sizeof(struct page_table));
        
        // 设置页目录项
        pd_entry->present = 1;
        pd_entry->writable = 1;
        pd_entry->user = (flags & PAGE_USER) ? 1 : 0;
        pd_entry->frame_addr = PHYS_TO_FRAME(pt_phys_addr);
        
        serial_print("[PAGING] Page table created at phys=");
        serial_print_hex(pt_phys_addr);
        serial_print("\n");
    }
    
    // 获取页表
    uint32_t pt_phys = FRAME_TO_PHYS(pd_entry->frame_addr);
    struct page_table* pt = (struct page_table*)pt_phys;
    
    // 设置页表项
    struct page_table_entry* pt_entry = &pt->entries[pt_index];
    
    if (pt_entry->present) {
        serial_print("[PAGING] Warning: Page already mapped, overwriting\n");
    }
    
    pt_entry->present = 1;
    pt_entry->writable = (flags & PAGE_WRITABLE) ? 1 : 0;
    pt_entry->user = (flags & PAGE_USER) ? 1 : 0;
    pt_entry->frame_addr = PHYS_TO_FRAME(physical_addr);
    
    serial_print("[PAGING] Page mapped successfully\n");
}

// 解除页映射
void paging_unmap_page(uint32_t virtual_addr) {
    virtual_addr = virtual_addr & ~(PAGE_SIZE - 1);
    
    struct page_table_entry* pt_entry = get_pt_entry(virtual_addr);
    
    if (pt_entry == NULL || !pt_entry->present) {
        serial_print("[PAGING] Warning: Attempting to unmapped non-present page\n");
        return;
    }
    
    // 清除页表项
    pt_entry->present = 0;
    pt_entry->frame_addr = 0;
    
    // 刷新TLB
    paging_flush_tlb(virtual_addr);
    
    serial_print("[PAGING] Page unmapped: virt=");
    serial_print_hex(virtual_addr);
    serial_print("\n");
}

// 获取页目录地址
uint32_t paging_get_page_directory(void) {
    return current_page_dir_phys;
}

// 初始化分页
void paging_init(void) {
    serial_print("[PAGING] Initializing paging\n");
    
    // 清空页目录
    memset(&page_directory, 0, sizeof(struct page_directory));
    
    // 计算页目录的物理地址
    current_page_dir_phys = (uint32_t)&page_directory;
    
    serial_print("[PAGING] Page directory at phys=");
    serial_print_hex(current_page_dir_phys);
    serial_print("\n");
    
    // 映射内核区域（虚拟地址 = 物理地址，1:1映射）
    // 映射前4MB内存（内核代码和数据）
    serial_print("[PAGING] Mapping kernel memory (1:1 mapping)\n");
    
    uint32_t kernel_end = KERNEL_VIRTUAL_BASE + KERNEL_SIZE;
    for (uint32_t addr = KERNEL_VIRTUAL_BASE; addr < kernel_end; addr += PAGE_SIZE) {
        paging_map_page(addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
    }
    
    // 映射VGA内存区域（0xB8000-0xBFFFF）
    serial_print("[PAGING] Mapping VGA memory\n");
    paging_map_page(0xB8000, 0xB8000, PAGE_PRESENT | PAGE_WRITABLE);
    
    // 映射页目录和页表自身（用于访问分页结构）
    serial_print("[PAGING] Mapping paging structures\n");
    uint32_t paging_start = current_page_dir_phys;
    uint32_t paging_end = paging_start + sizeof(page_directory) + (sizeof(struct page_table) * 32);
    paging_end = PAGE_ALIGN(paging_end);
    
    for (uint32_t addr = paging_start; addr < paging_end; addr += PAGE_SIZE) {
        paging_map_page(addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
    }
    
    // 映射ACPI相关的内存区域（用于ACPI表查找）
    serial_print("[PAGING] Mapping ACPI memory regions\n");
    // 映射BDA区域（BIOS Data Area，包含EBDA指针）
    paging_map_page(0x400, 0x400, PAGE_PRESENT | PAGE_WRITABLE);
    // 映射EBDA区域（通常在0x9FC00左右）
    paging_map_page(0x9FC00, 0x9FC00, PAGE_PRESENT | PAGE_WRITABLE);
    // 映射BIOS ROM区域（0xE0000-0xFFFFF）
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += PAGE_SIZE) {
        paging_map_page(addr, addr, PAGE_PRESENT);
    }
    
    serial_print("[PAGING] Paging structures initialized\n");
    serial_print("[PAGING] Enabling paging...\n");
    
    // 启用分页
    paging_enable(current_page_dir_phys);
    
    serial_print("[PAGING] Paging enabled successfully\n");
    serial_print("[PAGING] Virtual memory is now active\n");
}