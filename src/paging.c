#include "paging.h"
#include "serial.h"
#include "string.h"
#include "pmm.h"

// 页目录（物理地址对齐到4KB）
static struct page_directory page_directory __attribute__((aligned(PAGE_SIZE)));

// 当前页目录的物理地址
static uint32_t current_page_dir_phys;

#define PHYS_TO_FRAME(phys) ((phys) >> 12)
#define FRAME_TO_PHYS(frame) ((frame) << 12)
#define VIRT_TO_PD_INDEX(virt) (((virt) >> 22) & 0x3FF)
#define VIRT_TO_PT_INDEX(virt) (((virt) >> 12) & 0x3FF)

static struct page_table* paging_alloc_page_table(void) {
    void *table_page = pmm_alloc_page();
    if (table_page == NULL) {
        serial_print("[PAGING] PMM failed to allocate page table page\n");
        return NULL;
    }

    memset(table_page, 0, sizeof(struct page_table));
    return (struct page_table*)table_page;
}

static void paging_try_reclaim_page_table(uint32_t pd_index) {
    struct page_directory_entry *pd_entry = &page_directory.entries[pd_index];
    if (pd_entry == NULL || !pd_entry->present) {
        return;
    }

    struct page_table *pt = (struct page_table *)FRAME_TO_PHYS(pd_entry->frame_addr);
    uint32_t has_live = 0;

    for (uint32_t i = 0; i < PT_ENTRIES; ++i) {
        if (pt->entries[i].present) {
            has_live = 1;
            break;
        }
    }

    if (!has_live) {
        pmm_free_page(pt);
        pd_entry->present = 0;
        pd_entry->writable = 0;
        pd_entry->user = 0;
        pd_entry->frame_addr = 0;
        serial_print("[PAGING] Reclaimed empty page table page for PD index ");
        serial_print_dec(pd_index);
        serial_print("\n");
    }
}

void paging_reclaim_empty_page_tables(void) {
    for (uint32_t pd_index = 0; pd_index < PD_ENTRIES; ++pd_index) {
        paging_try_reclaim_page_table(pd_index);
    }
}

static struct page_directory_entry* get_pd_entry(uint32_t virtual_addr) {
    return &page_directory.entries[VIRT_TO_PD_INDEX(virtual_addr)];
}

static struct page_table_entry* get_pt_entry(uint32_t virtual_addr) {
    uint32_t pd_index = VIRT_TO_PD_INDEX(virtual_addr);
    uint32_t pt_index = VIRT_TO_PT_INDEX(virtual_addr);
    struct page_directory_entry* pd_entry = &page_directory.entries[pd_index];

    if (!pd_entry->present) {
        return NULL;
    }

    struct page_table* pt = (struct page_table*)FRAME_TO_PHYS(pd_entry->frame_addr);
    return &pt->entries[pt_index];
}

void paging_flush_tlb(uint32_t virtual_addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    virtual_addr = virtual_addr & ~(PAGE_SIZE - 1);
    physical_addr = physical_addr & ~(PAGE_SIZE - 1);

    uint32_t pd_index = VIRT_TO_PD_INDEX(virtual_addr);
    uint32_t pt_index = VIRT_TO_PT_INDEX(virtual_addr);
    struct page_directory_entry* pd_entry = &page_directory.entries[pd_index];

    if (!pd_entry->present) {
        struct page_table* pt = paging_alloc_page_table();
        if (pt == NULL) {
            serial_print("[PAGING] Failed to allocate page table for PD index ");
            serial_print_dec(pd_index);
            serial_print("\n");
            return;
        }
        pd_entry->present = 1;
        pd_entry->writable = 1;
        pd_entry->user = (flags & PAGE_USER) ? 1 : 0;
        pd_entry->frame_addr = PHYS_TO_FRAME((uint32_t)pt);
    }

    struct page_table* pt = (struct page_table*)FRAME_TO_PHYS(pd_entry->frame_addr);
    struct page_table_entry* pt_entry = &pt->entries[pt_index];

    pt_entry->present = 1;
    pt_entry->writable = (flags & PAGE_WRITABLE) ? 1 : 0;
    pt_entry->user = (flags & PAGE_USER) ? 1 : 0;
    pt_entry->frame_addr = PHYS_TO_FRAME(physical_addr);

    paging_flush_tlb(virtual_addr);
}

void paging_unmap_page(uint32_t virtual_addr) {
    virtual_addr = virtual_addr & ~(PAGE_SIZE - 1);
    uint32_t pd_index = VIRT_TO_PD_INDEX(virtual_addr);
    uint32_t pt_index = VIRT_TO_PT_INDEX(virtual_addr);
    struct page_directory_entry* pd_entry = &page_directory.entries[pd_index];
    struct page_table_entry* pt_entry = get_pt_entry(virtual_addr);

    if (pt_entry == NULL || !pt_entry->present) {
        serial_print("[PAGING] Warning: attempt to unmap missing page\n");
        return;
    }

    pt_entry->present = 0;
    pt_entry->frame_addr = 0;
    pt_entry->writable = 0;
    pt_entry->user = 0;

    if (pd_entry->present) {
        paging_try_reclaim_page_table(pd_index);
    }

    paging_flush_tlb(virtual_addr);
}

uint32_t paging_get_page_directory(void) {
    return current_page_dir_phys;
}

void paging_init(void) {
    serial_print("[PAGING] Initializing paging (PMM-backed lazy allocation)\n");

    memset(&page_directory, 0, sizeof(struct page_directory));
    current_page_dir_phys = (uint32_t)&page_directory;

    serial_print("[PAGING] Page directory at phys=");
    serial_print_hex(current_page_dir_phys);
    serial_print("\n");

    // 预先映射前 4MB 低内存，确保内核/中断/串口等已映射。
    for (uint32_t pd_index = 0; pd_index < 1; ++pd_index) {
        struct page_directory_entry* pd_entry = &page_directory.entries[pd_index];
        struct page_table* pt = paging_alloc_page_table();
        if (pt == NULL) {
            serial_print("[PAGING] Failed to allocate initial PT\n");
            return;
        }

        pd_entry->present = 1;
        pd_entry->writable = 1;
        pd_entry->user = 0;
        pd_entry->frame_addr = PHYS_TO_FRAME((uint32_t)pt);

        for (uint32_t pt_index = 0; pt_index < 1024; ++pt_index) {
            uint32_t addr = (pd_index << 22) + (pt_index << 12);
            pt->entries[pt_index].present = 1;
            pt->entries[pt_index].writable = 1;
            pt->entries[pt_index].user = 0;
            pt->entries[pt_index].frame_addr = PHYS_TO_FRAME(addr);
        }
    }

    serial_print("[PAGING] Basic low-memory mapping ready, enabling paging\n");
    paging_enable(current_page_dir_phys);
    serial_print("[PAGING] Paging enabled successfully\n");
}