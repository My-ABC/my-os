#include "paging.h"
#include "serial.h"
#include "string.h"
#include "pmm.h"

#define KERNEL_VIRT_BASE 0xC0000000U
#define KERNEL_PHYS_BASE 0x00100000U
#define KMAP_BASE 0xF0000000U
#define PAE_ADDRESS_MASK 0x0000000FFFFFF000ULL

static struct page_directory_pointer pae_pdpt __attribute__((aligned(32)));
static struct page_directory pae_pd[4] __attribute__((aligned(PAGE_SIZE)));
static struct page_table pae_low_pt[2] __attribute__((aligned(PAGE_SIZE)));
static struct page_table pae_high_pt[8] __attribute__((aligned(PAGE_SIZE)));
static struct page_table pae_kmap_pt __attribute__((aligned(PAGE_SIZE)));
static uint32_t current_page_dir_phys;
static uint32_t kmap_saved_eflags;

static uint64_t paging_entry_physical(uint64_t entry) {
    return entry & PAE_ADDRESS_MASK;
}

static uint32_t paging_disable_interrupts(void) {
    uint32_t eflags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(eflags) : : "memory");
    return eflags;
}

static void paging_restore_interrupts(uint32_t eflags) {
    if ((eflags & 0x200U) != 0U) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

uint32_t paging_is_user_address(uint32_t virtual_addr) {
    return virtual_addr < USER_SPACE_END;
}

void *paging_map_physical(uint64_t physical_addr) {
    if (physical_addr >= PMM_PHYSICAL_CEILING) return NULL;
    kmap_saved_eflags = paging_disable_interrupts();
    uint64_t page = physical_addr & ~(uint64_t)(PAGE_SIZE - 1U);
    uint32_t offset = (uint32_t)(physical_addr & (PAGE_SIZE - 1U));
    pae_kmap_pt.entries[0].value =
        (page & PAE_ADDRESS_MASK) | PAE_PRESENT | PAE_WRITABLE;
    paging_flush_tlb(KMAP_BASE);
    return (void *)(uintptr_t)(KMAP_BASE + offset);
}

void paging_unmap_physical(void) {
    pae_kmap_pt.entries[0].value = 0U;
    paging_flush_tlb(KMAP_BASE);
    paging_restore_interrupts(kmap_saved_eflags);
}

static int paging_alloc_page_table(uint64_t *physical_out) {
    uint64_t physical = pmm_alloc_page();
    if (physical == 0U) {
        serial_print("[PAGING] PMM failed to allocate PAE page table\n");
        return 0;
    }
    struct page_table *table = (struct page_table *)paging_map_physical(physical);
    if (table == NULL) {
        pmm_free_page(physical);
        return 0;
    }
    memset(table, 0, sizeof(*table));
    paging_unmap_physical();
    *physical_out = physical;
    return 1;
}

static uint64_t paging_lookup_entry(uint32_t virtual_addr) {
    uint32_t eflags = paging_disable_interrupts();
    uint32_t pdpt_index = (virtual_addr >> 30) & 3U;
    uint32_t pd_index = (virtual_addr >> 21) & 0x1FFU;
    uint32_t pt_index = (virtual_addr >> 12) & 0x1FFU;
    uint64_t pde = pae_pd[pdpt_index].entries[pd_index].value;
    if ((pde & PAE_PRESENT) == 0U || (pde & PAE_PAGE_SIZE) != 0U) {
        paging_restore_interrupts(eflags);
        return 0U;
    }
    struct page_table *pt =
        (struct page_table *)paging_map_physical(paging_entry_physical(pde));
    if (pt == NULL) {
        paging_restore_interrupts(eflags);
        return 0U;
    }
    uint64_t pte = pt->entries[pt_index].value;
    paging_unmap_physical();
    paging_restore_interrupts(eflags);
    return pte;
}

static void paging_try_reclaim_page_table(uint32_t pdpt_index, uint32_t pd_index) {
    uint32_t eflags = paging_disable_interrupts();
    if (pdpt_index == 3U && pd_index == ((KMAP_BASE >> 21) & 0x1FFU))
        goto done;
    struct page_directory_entry *pde = &pae_pd[pdpt_index].entries[pd_index];
    if ((pde->value & PAE_PRESENT) == 0U ||
        (pde->value & PAE_PAGE_SIZE) != 0U) goto done;
    uint64_t table_phys = paging_entry_physical(pde->value);
    struct page_table *pt = (struct page_table *)paging_map_physical(table_phys);
    if (pt == NULL) goto done;
    uint32_t has_live = 0U;
    for (uint32_t i = 0; i < PT_ENTRIES; ++i) {
        if ((pt->entries[i].value & PAE_PRESENT) != 0U) {
            has_live = 1U;
            break;
        }
    }
    paging_unmap_physical();
    if (!has_live) {
        pde->value = 0U;
        paging_flush_tlb(0U);
        pmm_free_page(table_phys);
    }
done:
    paging_restore_interrupts(eflags);
}

void paging_reclaim_empty_page_tables(void) {
    for (uint32_t pdpt = 0; pdpt < 4U; ++pdpt)
        for (uint32_t pd = 0; pd < PT_ENTRIES; ++pd)
            paging_try_reclaim_page_table(pdpt, pd);
}

uint64_t paging_get_page_physical(uint32_t virtual_addr) {
    uint64_t pte = paging_lookup_entry(virtual_addr);
    if ((pte & PAE_PRESENT) == 0U) return 0U;
    return paging_entry_physical(pte) | (virtual_addr & (PAGE_SIZE - 1U));
}

void paging_flush_tlb(uint32_t virtual_addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

void paging_map_page(uint32_t virtual_addr, uint64_t physical_addr,
                     uint32_t flags) {
    virtual_addr &= ~(PAGE_SIZE - 1U);
    physical_addr &= ~(uint64_t)(PAGE_SIZE - 1U);
    if (physical_addr >= PMM_PHYSICAL_CEILING) {
        serial_print("[PAGING] Refusing physical address at/above 64 GiB\n");
        return;
    }
    if (!paging_is_user_address(virtual_addr) && (flags & PAGE_USER) != 0U) {
        serial_print("[PAGING] Refusing user mapping in kernel address space\n");
        return;
    }

    uint32_t pdpt_index = (virtual_addr >> 30) & 3U;
    uint32_t pd_index = (virtual_addr >> 21) & 0x1FFU;
    uint32_t pt_index = (virtual_addr >> 12) & 0x1FFU;
    struct page_directory_entry *pde = &pae_pd[pdpt_index].entries[pd_index];
    uint64_t table_phys = 0U;
    uint32_t eflags = paging_disable_interrupts();
    if ((pde->value & PAE_PRESENT) == 0U) {
        paging_restore_interrupts(eflags);
        if (!paging_alloc_page_table(&table_phys)) return;
        eflags = paging_disable_interrupts();
    }
    if ((pde->value & PAE_PRESENT) == 0U) {
        pde->value = (table_phys & PAE_ADDRESS_MASK) | PAE_PRESENT | PAE_WRITABLE;
    } else if (table_phys != 0U) {
        pmm_free_page(table_phys);
    }
    if ((pde->value & PAE_PRESENT) == 0U) {
        paging_restore_interrupts(eflags);
        return;
    }
    if ((flags & PAGE_USER) != 0U) pde->value |= PAE_USER;

    struct page_table *pt =
        (struct page_table *)paging_map_physical(paging_entry_physical(pde->value));
    if (pt == NULL) {
        paging_restore_interrupts(eflags);
        return;
    }
    pt->entries[pt_index].value = (physical_addr & PAE_ADDRESS_MASK) |
        PAE_PRESENT |
        ((flags & PAGE_WRITABLE) ? PAE_WRITABLE : 0U) |
        ((flags & PAGE_USER) ? PAE_USER : 0U);
    paging_unmap_physical();
    paging_flush_tlb(virtual_addr);
    paging_restore_interrupts(eflags);
}

void paging_unmap_page(uint32_t virtual_addr) {
    virtual_addr &= ~(PAGE_SIZE - 1U);
    uint32_t eflags = paging_disable_interrupts();
    uint32_t pdpt_index = (virtual_addr >> 30) & 3U;
    uint32_t pd_index = (virtual_addr >> 21) & 0x1FFU;
    uint64_t pte = paging_lookup_entry(virtual_addr);
    if ((pte & PAE_PRESENT) == 0U) {
        serial_print("[PAGING] Warning: attempt to unmap missing PAE page\n");
        paging_restore_interrupts(eflags);
        return;
    }
    uint64_t pde = pae_pd[pdpt_index].entries[pd_index].value;
    struct page_table *pt =
        (struct page_table *)paging_map_physical(paging_entry_physical(pde));
    if (pt == NULL) {
        paging_restore_interrupts(eflags);
        return;
    }
    pt->entries[(virtual_addr >> 12) & 0x1FFU].value = 0U;
    paging_unmap_physical();
    paging_flush_tlb(virtual_addr);
    paging_try_reclaim_page_table(pdpt_index, pd_index);
    paging_restore_interrupts(eflags);
}

uint32_t paging_get_page_directory(void) {
    return current_page_dir_phys;
}

void paging_init(void) {
    serial_print("[PAGING] Initializing 36-bit PAE paging\n");
    memset(&pae_pdpt, 0, sizeof(pae_pdpt));
    memset(pae_pd, 0, sizeof(pae_pd));
    memset(pae_low_pt, 0, sizeof(pae_low_pt));
    memset(pae_high_pt, 0, sizeof(pae_high_pt));
    memset(&pae_kmap_pt, 0, sizeof(pae_kmap_pt));

    current_page_dir_phys = (uint32_t)&pae_pdpt -
                            (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
    for (uint32_t i = 0; i < 4U; ++i) {
        uint32_t pd_phys = (uint32_t)&pae_pd[i] -
                           (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
        pae_pdpt.entries[i] = (uint64_t)pd_phys | PAE_PRESENT;
    }

    uint32_t low_phys = (uint32_t)&pae_low_pt[0] -
                        (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
    for (uint32_t i = 0; i < 2U; ++i)
        pae_pd[0].entries[i].value =
            (uint64_t)(low_phys + i * PAGE_SIZE) | PAE_PRESENT | PAE_WRITABLE;
    for (uint32_t i = 0; i < 1024U; ++i) {
        uint64_t phys = (uint64_t)i * PAGE_SIZE;
        pae_low_pt[i / 512U].entries[i % 512U].value =
            phys | PAE_PRESENT | PAE_WRITABLE;
    }

    uint32_t high_phys = (uint32_t)&pae_high_pt[0] -
                         (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
    for (uint32_t i = 0; i < 8U; ++i)
        pae_pd[3].entries[i].value =
            (uint64_t)(high_phys + i * PAGE_SIZE) | PAE_PRESENT | PAE_WRITABLE;
    for (uint32_t i = 0; i < 4096U; ++i) {
        uint64_t phys = 0x00100000ULL + (uint64_t)i * PAGE_SIZE;
        pae_high_pt[i / 512U].entries[i % 512U].value =
            phys | PAE_PRESENT | PAE_WRITABLE;
    }

    uint32_t kmap_phys = (uint32_t)&pae_kmap_pt -
                         (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
    uint32_t kmap_pd_index = (KMAP_BASE >> 21) & 0x1FFU;
    pae_pd[3].entries[kmap_pd_index].value =
        (uint64_t)kmap_phys | PAE_PRESENT | PAE_WRITABLE;

    serial_print("[PAGING] PDPT physical address=");
    serial_print_hex(current_page_dir_phys);
    serial_print("; 64 GiB physical ceiling\n");
    paging_enable(current_page_dir_phys);
}
