#include "paging.h"
#include "memory.h"
#include "stdio.h"
#include "string.h"
#include "idt.h"

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_table[1024] __attribute__((aligned(4096)));

void paging_init(void) {
    print_info("Initializing paging...\n");
    
    memset(page_directory, 0, sizeof(page_directory));
    memset(page_table, 0, sizeof(page_table));
    
    for (int i = 0; i < 1024; i++) {
        page_table[i] = (i * 4096) | 0x03;
    }
    page_directory[0] = ((uint32_t)page_table) | 0x03;
    
    __asm__ volatile ("movl %0, %%cr3" : : "r"(page_directory));
    
    uint32_t cr0;
    __asm__ volatile ("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("movl %0, %%cr0" : : "r"(cr0));
    
    print_info("Paging enabled (identity mapped 4MB)\n");
}