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

/* ---- per-process user address spaces ---- */

extern void paging_load_cr3(uint32_t phys);

static uint32_t active_cr3;

struct user_mm_slot {
    struct page_directory_pointer pdpt __attribute__((aligned(32)));
    struct page_directory pd0 __attribute__((aligned(PAGE_SIZE)));
    uint32_t in_use;
};

static struct user_mm_slot user_mms[USER_MM_MAX];
static struct user_mm user_mm_objs[USER_MM_MAX];

static uint64_t mm_phys(const void *virt) {
    return (uint64_t)((uint32_t)virt - (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE));
}

uint32_t paging_kernel_cr3(void) {
    return current_page_dir_phys;
}

uint32_t paging_switch_cr3(uint32_t phys) {
    uint32_t prev = active_cr3;
    if (phys != prev) {
        active_cr3 = phys;
        paging_load_cr3(phys);
    }
    return prev;
}

static struct user_mm_slot *mm_slot_of(struct user_mm *mm) {
    if (mm == 0 || mm->slot >= USER_MM_MAX) return 0;
    return &user_mms[mm->slot];
}

static int mm_ensure_pt(struct user_mm_slot *slot, uint32_t pd_index,
                        uint64_t *pt_phys_out);

struct user_mm *paging_mm_create(void) {
    for (uint32_t i = 0; i < USER_MM_MAX; ++i) {
        if (user_mms[i].in_use) continue;
        struct user_mm_slot *slot = &user_mms[i];
        memset(&slot->pdpt, 0, sizeof(slot->pdpt));
        memset(&slot->pd0, 0, sizeof(slot->pd0));
        slot->pdpt.entries[0] =
            mm_phys(&slot->pd0) | PAE_PRESENT | PAE_WRITABLE | PAE_USER;
        /* Share the kernel PD so the kernel half and KMAP window are
         * identical in every address space. */
        slot->pdpt.entries[3] = pae_pdpt.entries[3];
        slot->in_use = 1;
        user_mm_objs[i].slot = i;
        user_mm_objs[i].pdpt_phys = (uint32_t)mm_phys(&slot->pdpt);

        /* Give every address space the same supervisor-only low 4 MiB
         * identity map: boot stubs, the initial kernel stack, and other
         * kernel low-memory users stay reachable after any CR3 switch.
         * Entries are private copies marked borrowed so mm_release never
         * frees kernel-owned low memory. */
        for (uint32_t pd = 0; pd < 2U; ++pd) {
            uint64_t pt_phys = 0U;
            if (!mm_ensure_pt(slot, pd, &pt_phys)) continue;
            struct page_table *dst =
                (struct page_table *)paging_map_physical(pt_phys);
            if (dst == NULL) continue;
            for (uint32_t e = 0; e < PT_ENTRIES; ++e) {
                dst->entries[e].value =
                    pae_low_pt[pd].entries[e].value | (1ULL << 9);
            }
            paging_unmap_physical();
        }
        return &user_mm_objs[i];
    }
    return 0;
}

static int mm_ensure_pt(struct user_mm_slot *slot, uint32_t pd_index,
                        uint64_t *pt_phys_out) {
    struct page_directory_entry *pde = &slot->pd0.entries[pd_index];
    if ((pde->value & PAE_PRESENT) != 0U) {
        if ((pde->value & PAE_PAGE_SIZE) != 0U) return 0;
        *pt_phys_out = paging_entry_physical(pde->value);
        return 1;
    }
    uint64_t pt_phys = pmm_alloc_page();
    if (pt_phys == 0U) return 0;
    struct page_table *pt = (struct page_table *)paging_map_physical(pt_phys);
    if (pt == NULL) {
        pmm_free_page(pt_phys);
        return 0;
    }
    memset(pt, 0, sizeof(*pt));
    paging_unmap_physical();
    pde->value = (pt_phys & PAE_ADDRESS_MASK) | PAE_PRESENT | PAE_WRITABLE |
                 PAE_USER;
    *pt_phys_out = pt_phys;
    return 1;
}

void paging_mm_map(struct user_mm *mm, uint32_t virtual_addr,
                   uint64_t physical_addr, uint32_t flags) {
    if (mm == 0) return;
    struct user_mm_slot *slot = mm_slot_of(mm);
    if (slot == 0) return;
    virtual_addr &= ~(PAGE_SIZE - 1U);
    physical_addr &= ~(uint64_t)(PAGE_SIZE - 1U);
    if (virtual_addr >= USER_MM_SPACE_LIMIT ||
        ((flags & PAGE_USER) != 0U && virtual_addr < 0x400000U) ||
        physical_addr >= PMM_PHYSICAL_CEILING) {
        serial_print("[PAGING] Refusing out-of-range user-mm map\n");
        return;
    }

    uint32_t pd_index = (virtual_addr >> 21) & 0x1FFU;
    uint32_t pt_index = (virtual_addr >> 12) & 0x1FFU;
    uint64_t pt_phys = 0U;
    uint32_t eflags = paging_disable_interrupts();
    if (!mm_ensure_pt(slot, pd_index, &pt_phys)) {
        paging_restore_interrupts(eflags);
        return;
    }
    struct page_table *pt =
        (struct page_table *)paging_map_physical(pt_phys);
    if (pt == NULL) {
        paging_restore_interrupts(eflags);
        return;
    }
    pt->entries[pt_index].value = (physical_addr & PAE_ADDRESS_MASK) |
        PAE_PRESENT |
        ((flags & PAGE_WRITABLE) ? PAE_WRITABLE : 0U) |
        ((flags & PAGE_USER) ? PAE_USER : 0U) |
        ((flags & PAGE_BORROWED) ? (1ULL << 9) : 0U);
    paging_unmap_physical();
    paging_flush_tlb(virtual_addr);
    paging_restore_interrupts(eflags);
}

uint64_t paging_mm_get_phys(struct user_mm *mm, uint32_t virtual_addr) {
    struct user_mm_slot *slot = mm_slot_of(mm);
    if (slot == 0) return 0U;
    virtual_addr &= ~(PAGE_SIZE - 1U);
    if (virtual_addr >= USER_MM_SPACE_LIMIT) return 0U;
    struct page_directory_entry *pde =
        &slot->pd0.entries[(virtual_addr >> 21) & 0x1FFU];
    if ((pde->value & PAE_PRESENT) == 0U ||
        (pde->value & PAE_PAGE_SIZE) != 0U) {
        return 0U;
    }
    uint64_t pt_phys = paging_entry_physical(pde->value);
    struct page_table *pt =
        (struct page_table *)paging_map_physical(pt_phys);
    if (pt == NULL) return 0U;
    uint64_t pte = pt->entries[(virtual_addr >> 12) & 0x1FFU].value;
    paging_unmap_physical();
    if ((pte & PAE_PRESENT) == 0U) return 0U;
    return paging_entry_physical(pte) | (virtual_addr & (PAGE_SIZE - 1U));
}

void paging_mm_unmap(struct user_mm *mm, uint32_t virtual_addr) {
    struct user_mm_slot *slot = mm_slot_of(mm);
    if (slot == 0) return;
    virtual_addr &= ~(PAGE_SIZE - 1U);
    uint32_t pd_index = (virtual_addr >> 21) & 0x1FFU;
    uint32_t pt_index = (virtual_addr >> 12) & 0x1FFU;
    uint32_t eflags = paging_disable_interrupts();
    struct page_directory_entry *pde = &slot->pd0.entries[pd_index];
    if ((pde->value & PAE_PRESENT) == 0U ||
        (pde->value & PAE_PAGE_SIZE) != 0U) {
        paging_restore_interrupts(eflags);
        return;
    }
    uint64_t pt_phys = paging_entry_physical(pde->value);
    struct page_table *pt =
        (struct page_table *)paging_map_physical(pt_phys);
    if (pt == NULL) {
        paging_restore_interrupts(eflags);
        return;
    }
    pt->entries[pt_index].value = 0U;
    paging_unmap_physical();
    paging_flush_tlb(virtual_addr);

    /* Reclaim the PT page when it became empty. */
    pt = (struct page_table *)paging_map_physical(pt_phys);
    if (pt != NULL) {
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
            pmm_free_page(pt_phys);
        }
    }
    paging_restore_interrupts(eflags);
}

void paging_mm_release(struct user_mm *mm) {
    struct user_mm_slot *slot = mm_slot_of(mm);
    if (slot == 0) return;
    uint32_t eflags = paging_disable_interrupts();
    for (uint32_t pd = 0; pd < PT_ENTRIES; ++pd) {
        struct page_directory_entry *pde = &slot->pd0.entries[pd];
        if ((pde->value & PAE_PRESENT) == 0U ||
            (pde->value & PAE_PAGE_SIZE) != 0U) {
            continue;
        }
        uint64_t pt_phys = paging_entry_physical(pde->value);
        struct page_table *pt =
            (struct page_table *)paging_map_physical(pt_phys);
        if (pt == NULL) continue;
        for (uint32_t i = 0; i < PT_ENTRIES; ++i) {
            uint64_t pte = pt->entries[i].value;
            if ((pte & PAE_PRESENT) != 0U && (pte & (1ULL << 9)) == 0U) {
                pmm_free_page(paging_entry_physical(pte));
            }
        }
        paging_unmap_physical();
        pmm_free_page(pt_phys);
        pde->value = 0U;
    }
    memset(&slot->pdpt, 0, sizeof(slot->pdpt));
    slot->in_use = 0;
    paging_flush_tlb(0U);
    paging_restore_interrupts(eflags);
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
    active_cr3 = current_page_dir_phys;
    paging_enable(current_page_dir_phys);
}
