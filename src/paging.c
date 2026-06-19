#include "paging.h"
#include "memory.h"
#include "stdio.h"
#include "string.h"
#include "idt.h"

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_table[1024] __attribute__((aligned(4096)));

// ========== 缺页异常处理 ==========

void page_fault_handler(void) {
    __asm__ volatile ("cli");
    
    uint32_t fault_addr;
    __asm__ volatile ("movl %%cr2, %0" : "=r"(fault_addr));
    
    uint16_t* vga = (uint16_t*)0xB8000;
    vga[0] = (uint16_t)'P' | 0x0F00;
    
    // 只处理 0-128MB 范围内的地址
    if (fault_addr >= 256 * 1024 * 1024) {
        vga[1] = (uint16_t)'O' | 0x0F00;
        while(1) __asm__ volatile ("hlt");
    }
    
    // 直接 identity mapping（物理地址 = 虚拟地址）
    uint32_t phys = fault_addr & ~0xFFF;
    uint32_t virt = fault_addr & ~0xFFF;
    uint32_t dir_idx = virt >> 22;
    uint32_t table_idx = (virt >> 12) & 0x3FF;
    
    if (!(page_directory[dir_idx] & 1)) {
        uint32_t table_phys = pmm_alloc_page();
        if (table_phys == 0) {
            vga[1] = (uint16_t)'E' | 0x0F00;
            while(1) __asm__ volatile ("hlt");
        }
        uint32_t* table = (uint32_t*)table_phys;
        for (int i = 0; i < 1024; i++) table[i] = 0;
        page_directory[dir_idx] = table_phys | 0x03;
    }
    
    uint32_t table_phys = page_directory[dir_idx] & ~0xFFF;
    uint32_t* table = (uint32_t*)table_phys;
    table[table_idx] = (phys & ~0xFFF) | 0x03;
    
    __asm__ volatile ("invlpg (%0)" : : "r"(virt));
    __asm__ volatile ("sti");
}

// ========== 分页初始化 ==========

void paging_init(void) {
    print_info("Initializing paging...\n");
    
    memset(page_directory, 0, sizeof(page_directory));
    memset(page_table, 0, sizeof(page_table));
    
    // 映射前 4MB
    for (int i = 0; i < 1024; i++) {
        page_table[i] = (i * 4096) | 0x03;
    }
    page_directory[0] = ((uint32_t)page_table) | 0x03;
    
    // 预映射 4-128MB（让 pmm_alloc_page 分配的页可访问）
    uint32_t total_pages = 128 * 1024 * 1024 / 4096;
    uint32_t pages_mapped = 1024;
    
    while (pages_mapped < total_pages) {
        uint32_t* table = (uint32_t*)pmm_alloc_page();
        if (table == 0) break;
        memset(table, 0, 4096);
        
        uint32_t count = total_pages - pages_mapped;
        if (count > 1024) count = 1024;
        
        for (uint32_t i = 0; i < count; i++) {
            table[i] = (pages_mapped * 4096) | 0x03;
            pages_mapped++;
        }
        
        uint32_t dir_idx = pages_mapped / 1024;
        if (dir_idx < 1024) {
            page_directory[dir_idx] = ((uint32_t)table) | 0x03;
        }
    }
    
    // 注册缺页异常
    extern void isr14(void);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    
    __asm__ volatile ("movl %0, %%cr3" : : "r"(page_directory));
    
    uint32_t cr0;
    __asm__ volatile ("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    cr0 |= 0x00010000;
    __asm__ volatile ("movl %0, %%cr0" : : "r"(cr0));
    
    print_info("Paging enabled (128MB mapped)\n");
}