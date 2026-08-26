#include "pmm.h"
#include "serial.h"
#include "string.h"

extern uint32_t kernel_end;

#define KERNEL_VIRT_BASE 0xC0000000U
#define KERNEL_PHYS_BASE 0x00100000U

static pmm_state_t g_pmm = {0};

struct multiboot_mmap_entry {
    uint32_t size;
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed));

static uint32_t pmm_align_up(uint32_t value, uint32_t alignment) {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static void pmm_set_frame(uint32_t frame, uint8_t used) {
    if (frame >= g_pmm.total_frames) {
        return;
    }

    uint32_t index = frame / 8U;
    uint8_t bit = (uint8_t)(1U << (frame % 8U));

    if (used) {
        g_pmm.bitmap[index] |= bit;
    } else {
        g_pmm.bitmap[index] &= (uint8_t)(~bit);
    }
}

static uint8_t pmm_is_frame_used(uint32_t frame) {
    if (frame >= g_pmm.total_frames) {
        return 1;
    }

    uint32_t index = frame / 8U;
    uint8_t bit = (uint8_t)(1U << (frame % 8U));
    return (g_pmm.bitmap[index] & bit) != 0U;
}

static void pmm_set_range_state(uint32_t base_addr, uint32_t length, uint8_t used) {
    if (length == 0U) {
        return;
    }

    uint32_t start = base_addr & ~(PMM_PAGE_SIZE - 1U);
    uint32_t end = pmm_align_up(base_addr + length, PMM_PAGE_SIZE);

    if (end <= start) {
        return;
    }

    for (uint32_t addr = start; addr < end; addr += PMM_PAGE_SIZE) {
        uint32_t frame = addr / PMM_PAGE_SIZE;
        if (frame < g_pmm.total_frames) {
            pmm_set_frame(frame, used);
        }
    }
}

static void pmm_mark_free_range(uint32_t base_addr, uint32_t length) {
    pmm_set_range_state(base_addr, length, 0U);
}

static void pmm_mark_used_range(uint32_t base_addr, uint32_t length) {
    pmm_set_range_state(base_addr, length, 1U);
}

void pmm_init(uint32_t multiboot_info_addr) {
    uint32_t total_memory_kib = 0U;
    uint32_t total_frames = 0U;
    uint32_t bitmap_addr = 0U;
    uint32_t bitmap_size = 0U;

    serial_print("[PMM] Initializing physical memory manager\n");

    if (multiboot_info_addr != 0U) {
        uint32_t *mb_info = (uint32_t *)multiboot_info_addr;
        uint32_t flags = mb_info[0];

        if ((flags & 0x1U) != 0U) {
            total_memory_kib = (uint32_t)mb_info[2] * 1024U;
        }

        if ((flags & 0x40U) != 0U) {
            uint32_t mmap_addr = mb_info[6];
            uint32_t mmap_length = mb_info[7];
            uint8_t *mmap = (uint8_t *)mmap_addr;
            uint32_t offset = 0U;

            serial_print("[PMM] Walking multiboot memory map\n");
            while (offset < mmap_length) {
                struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)(mmap + offset);
                uint64_t base = ((uint64_t)entry->addr_high << 32) | (uint64_t)entry->addr_low;
                uint64_t length = ((uint64_t)entry->len_high << 32) | (uint64_t)entry->len_low;

                if (entry->type == 1U && base < 0x100000000ULL) {
                    uint32_t start = (uint32_t)base;
                    uint32_t size = (uint32_t)length;
                    if (start + size > total_memory_kib) {
                        total_memory_kib = start + size;
                    }
                    if (start < 0x100000U) {
                        start = 0x100000U;
                    }

                    if (start < (uint32_t)((uint64_t)base + length)) {
                        pmm_mark_free_range(start, (uint32_t)((uint64_t)base + length - start));
                    }
                }

                offset += entry->size + sizeof(uint32_t);
            }
        }
    }

    if (total_memory_kib == 0U) {
        total_memory_kib = 0x100000U + 0x100000U;
        serial_print("[PMM] No valid memory map, using fallback memory size\n");
    }

    total_frames = total_memory_kib / PMM_PAGE_SIZE;
    if (total_frames > 0x100000U) {
        total_frames = 0x100000U;
    }

    bitmap_size = (total_frames + 7U) / 8U;
    bitmap_addr = pmm_align_up((uint32_t)&kernel_end - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE, PMM_PAGE_SIZE);

    g_pmm.total_frames = total_frames;
    g_pmm.used_frames = 0U;
    g_pmm.bitmap_size = bitmap_size;
    g_pmm.bitmap = (uint8_t *)(KERNEL_VIRT_BASE + bitmap_addr - KERNEL_PHYS_BASE);
    g_pmm.next_free_frame = 0U;

    memset(g_pmm.bitmap, 0x00, bitmap_size);

    serial_print("[PMM] Bitmap at 0x");
    serial_print_hex(bitmap_addr);
    serial_print(" size=");
    serial_print_dec(bitmap_size);
    serial_print(" bytes\n");

    pmm_mark_used_range(0x00000000U, 0x100000U);

    uint32_t kernel_end_page = pmm_align_up((uint32_t)&kernel_end - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE, PMM_PAGE_SIZE);
    pmm_mark_used_range(0x100000U, kernel_end_page - 0x100000U + PMM_PAGE_SIZE * 2U);
    pmm_mark_used_range(bitmap_addr, bitmap_size + PMM_PAGE_SIZE);

    if (total_memory_kib > 0U) {
        uint32_t max_frames = g_pmm.total_frames;
        for (uint32_t frame = 0U; frame < max_frames; ++frame) {
            uint32_t address = frame * PMM_PAGE_SIZE;
            if (address >= 0x100000U && address < kernel_end_page) {
                pmm_set_frame(frame, 1U);
            }
            if (address >= bitmap_addr && address < bitmap_addr + bitmap_size + PMM_PAGE_SIZE) {
                pmm_set_frame(frame, 1U);
            }
        }
    }

    serial_print("[PMM] Total frames: ");
    serial_print_dec(g_pmm.total_frames);
    serial_print("\n");
    serial_print("[PMM] Free frames: ");
    serial_print_dec(pmm_get_free_frames());
    serial_print("\n");
}

void *pmm_alloc_page(void) {
    uint32_t frame = g_pmm.next_free_frame;
    uint32_t scanned = 0U;

    while (scanned < g_pmm.total_frames) {
        if (frame >= g_pmm.total_frames) {
            frame = 0U;
        }

        if (!pmm_is_frame_used(frame)) {
            pmm_set_frame(frame, 1U);
            g_pmm.used_frames++;
            g_pmm.next_free_frame = frame + 1U;
            return (void *)(uintptr_t)(frame * PMM_PAGE_SIZE);
        }

        frame++;
        scanned++;
    }

    return 0;
}

void pmm_free_page(void *addr) {
    if (addr == 0U) {
        return;
    }

    uint32_t frame = (uint32_t)(uintptr_t)addr / PMM_PAGE_SIZE;
    if (frame >= g_pmm.total_frames) {
        return;
    }

    if (pmm_is_frame_used(frame)) {
        pmm_set_frame(frame, 0U);
        g_pmm.used_frames--;
        if (frame < g_pmm.next_free_frame || g_pmm.next_free_frame == g_pmm.total_frames) {
            g_pmm.next_free_frame = frame;
        }
    }
}

uint32_t pmm_get_free_frames(void) {
    uint32_t free_frames = 0U;

    for (uint32_t frame = 0U; frame < g_pmm.total_frames; ++frame) {
        if (!pmm_is_frame_used(frame)) {
            free_frames++;
        }
    }

    return free_frames;
}
