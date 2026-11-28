#include "pmm.h"
#include "serial.h"
#include "string.h"

#define KERNEL_VIRT_BASE 0xC0000000U
#define KERNEL_PHYS_BASE 0x00100000U
#define PMM_LEGACY_PHYSICAL_CEILING (1ULL << 32)
#define MULTIBOOT_INFO_FLAGS_INDEX 0U
#define MULTIBOOT_INFO_MEM_UPPER_INDEX 2U
#define MULTIBOOT_INFO_MMAP_LENGTH_INDEX 11U
#define MULTIBOOT_INFO_MMAP_ADDR_INDEX 12U
#define MULTIBOOT_INFO_MMAP_END_INDEX 13U

extern uint32_t kernel_end;

static pmm_state_t g_pmm;
static uint8_t g_frame_bitmap[PMM_BITMAP_SIZE] __attribute__((aligned(4096)));

struct multiboot_mmap_entry {
    uint32_t size;
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed));

#ifdef PMM_HOST_TEST
static uint32_t pmm_disable_interrupts(void) {
    return 0U;
}

static void pmm_restore_interrupts(uint32_t eflags) {
    (void)eflags;
}
#else
static uint32_t pmm_disable_interrupts(void) {
    uint32_t eflags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(eflags) : : "memory");
    return eflags;
}

static void pmm_restore_interrupts(uint32_t eflags) {
    if ((eflags & 0x200U) != 0U)
        __asm__ volatile ("sti" : : : "memory");
}
#endif

static void pmm_set_frame(uint32_t frame, uint8_t used) {
    if (frame >= g_pmm.total_frames) return;
    uint32_t index = frame >> 3;
    uint8_t bit = (uint8_t)(1U << (frame & 7U));
    if (used) g_pmm.bitmap[index] |= bit;
    else g_pmm.bitmap[index] &= (uint8_t)~bit;
}

static uint8_t pmm_is_frame_used(uint32_t frame) {
    if (frame >= g_pmm.total_frames) return 1U;
    return (g_pmm.bitmap[frame >> 3] & (uint8_t)(1U << (frame & 7U))) != 0U;
}

static uint64_t mmap_entry_base(const struct multiboot_mmap_entry *entry) {
    return ((uint64_t)entry->addr_high << 32) | entry->addr_low;
}

static uint64_t mmap_entry_length(const struct multiboot_mmap_entry *entry) {
    return ((uint64_t)entry->len_high << 32) | entry->len_low;
}

static void pmm_set_range(uint64_t base, uint64_t length, uint8_t used) {
    if (length == 0U || base >= PMM_PHYSICAL_CEILING) return;
    uint64_t end = base + length;
    if (end < base || end > PMM_PHYSICAL_CEILING) end = PMM_PHYSICAL_CEILING;
    uint32_t first = (uint32_t)(used
        ? base / PMM_PAGE_SIZE
        : (base + PMM_PAGE_SIZE - 1U) / PMM_PAGE_SIZE);
    uint32_t last = (uint32_t)(used
        ? (end + PMM_PAGE_SIZE - 1U) / PMM_PAGE_SIZE
        : end / PMM_PAGE_SIZE);
    if (last > g_pmm.total_frames) last = g_pmm.total_frames;
    for (uint32_t frame = first; frame < last; ++frame)
        pmm_set_frame(frame, used);
}

static int pmm_walk_mmap(uint32_t info_addr, uint32_t mark_ranges) {
    uint32_t *info = (uint32_t *)(uintptr_t)info_addr;
    uint32_t flags = info[MULTIBOOT_INFO_FLAGS_INDEX];
    if ((flags & 0x40U) == 0U) return 0;
    uint32_t mmap_addr = info[MULTIBOOT_INFO_MMAP_ADDR_INDEX];
    uint32_t mmap_length = info[MULTIBOOT_INFO_MMAP_LENGTH_INDEX];
    if ((uint64_t)mmap_addr + mmap_length > 0x100000000ULL) return 0;
    uint32_t offset = 0U;
    int found = 0;
    while (offset < mmap_length) {
        if (mmap_length - offset < sizeof(uint32_t)) break;
        uint8_t *raw = (uint8_t *)(uintptr_t)(mmap_addr + offset);
        uint32_t entry_size = *(uint32_t *)raw;
        if (entry_size < 20U || entry_size > mmap_length - offset - sizeof(uint32_t))
            break;
        struct multiboot_mmap_entry *entry =
            (struct multiboot_mmap_entry *)raw;
        uint64_t base = mmap_entry_base(entry);
        uint64_t length = mmap_entry_length(entry);
        if (entry->type == 1U && length != 0U && base < PMM_PHYSICAL_CEILING) {
            found = 1;
            if (mark_ranges) pmm_set_range(base, length, 0U);
        } else if (mark_ranges && length != 0U && base < PMM_PHYSICAL_CEILING) {
            /* Non-usable entries override any overlapping usable ranges. */
            pmm_set_range(base, length, 1U);
        }
        offset += entry_size + sizeof(uint32_t);
    }
    return found;
}

void pmm_init(uint32_t multiboot_info_addr) {
    g_pmm.total_frames = PMM_MAX_FRAMES;
    g_pmm.used_frames = PMM_MAX_FRAMES;
    g_pmm.bitmap_size = PMM_BITMAP_SIZE;
    g_pmm.bitmap = g_frame_bitmap;
    g_pmm.next_free_frame = 0U;
    memset(g_pmm.bitmap, 0xFF, g_pmm.bitmap_size);

    serial_print("[PMM] Initializing PAE frame allocator (64 GiB ceiling)\n");
    if (multiboot_info_addr != 0U && pmm_walk_mmap(multiboot_info_addr, 1)) {
        /* First pass already marked non-usable; reapply usable last is wrong
           for overlap, so non-usable ranges are reapplied in a second pass. */
        uint32_t *info = (uint32_t *)(uintptr_t)multiboot_info_addr;
        uint32_t mmap_addr = info[MULTIBOOT_INFO_MMAP_ADDR_INDEX];
        uint32_t mmap_length = info[MULTIBOOT_INFO_MMAP_LENGTH_INDEX];
        uint32_t offset = 0U;
        while (offset < mmap_length) {
            if (mmap_length - offset < sizeof(uint32_t)) break;
            uint8_t *raw = (uint8_t *)(uintptr_t)(mmap_addr + offset);
            uint32_t entry_size = *(uint32_t *)raw;
            if (entry_size < 20U ||
                entry_size > mmap_length - offset - sizeof(uint32_t)) break;
            struct multiboot_mmap_entry *entry =
                (struct multiboot_mmap_entry *)raw;
            if (entry->type != 1U)
                pmm_set_range(mmap_entry_base(entry), mmap_entry_length(entry), 1U);
            offset += entry_size + sizeof(uint32_t);
        }
    } else {
        uint64_t end = 0x200000ULL;
        if (multiboot_info_addr != 0U) {
            uint32_t *info = (uint32_t *)(uintptr_t)multiboot_info_addr;
            if ((info[MULTIBOOT_INFO_FLAGS_INDEX] & 0x1U) != 0U)
                end = 0x100000ULL +
                      (uint64_t)info[MULTIBOOT_INFO_MEM_UPPER_INDEX] * 1024ULL;
        }
        /* Multiboot mem_upper is a legacy low-memory summary, not a 64-bit map. */
        if (end > PMM_LEGACY_PHYSICAL_CEILING)
            end = PMM_LEGACY_PHYSICAL_CEILING;
        if (end > PMM_PHYSICAL_CEILING) end = PMM_PHYSICAL_CEILING;
        if (end > 0x100000ULL)
            pmm_set_range(0x100000ULL, end - 0x100000ULL, 0U);
        serial_print("[PMM] No usable Multiboot map; using bounded mem_upper fallback\n");
    }

    pmm_set_range(0U, 0x100000U, 1U);
    uint32_t kernel_phys_end = (uint32_t)&kernel_end - KERNEL_VIRT_BASE +
                               KERNEL_PHYS_BASE;
    pmm_set_range(KERNEL_PHYS_BASE,
                  (uint64_t)kernel_phys_end - KERNEL_PHYS_BASE +
                  PMM_PAGE_SIZE * 2U, 1U);
    if (multiboot_info_addr != 0U) {
        uint32_t *info = (uint32_t *)(uintptr_t)multiboot_info_addr;
        pmm_set_range(multiboot_info_addr,
                      sizeof(uint32_t) * MULTIBOOT_INFO_MMAP_END_INDEX, 1U);
        if ((info[MULTIBOOT_INFO_FLAGS_INDEX] & 0x40U) != 0U)
            pmm_set_range(info[MULTIBOOT_INFO_MMAP_ADDR_INDEX],
                          info[MULTIBOOT_INFO_MMAP_LENGTH_INDEX], 1U);
    }

    g_pmm.used_frames = PMM_MAX_FRAMES - pmm_get_free_frames();
    /* The bitmap remains free only for firmware-reported usable frames. */
    serial_print("[PMM] Bitmap size: ");
    serial_print_dec(g_pmm.bitmap_size);
    serial_print(" bytes; maximum frames: ");
    serial_print_dec(g_pmm.total_frames);
    serial_print("\n");
    serial_print("[PMM] Free frames: ");
    serial_print_dec(pmm_get_free_frames());
    serial_print("\n");
}

static uint64_t pmm_alloc_page_from(uint32_t first_frame, uint32_t wrap) {
    uint32_t frame = first_frame;
    uint32_t scanned = 0U;
    uint32_t limit = wrap ? g_pmm.total_frames :
                     (first_frame < g_pmm.total_frames
                          ? g_pmm.total_frames - first_frame : 0U);
    while (scanned < limit) {
        if (frame >= g_pmm.total_frames) frame = 0U;
        /* A bitmap byte of 0xFF marks 8 consecutive used frames — skip
         * the whole byte at once. total_frames covers the 64 GiB
         * ceiling (16M frames) while real machines expose far fewer
         * usable frames, so the all-ones tail dominated every scan and
         * made bulk allocation (e.g. the oom.elf drain) take minutes
         * under TCG. Only all-ones bytes are skipped, so no free frame
         * is ever jumped over. */
        if (g_pmm.bitmap[frame >> 3] == 0xFFU) {
            uint32_t skip = 8U - (frame & 7U);
            /* Clamp to the remaining range: do not rely on
             * total_frames being a multiple of 8 or on the bitmap
             * tail being addressable past total_frames. */
            if (skip > g_pmm.total_frames - frame)
                skip = g_pmm.total_frames - frame;
            frame += skip;
            scanned += skip;
            continue;
        }
        if (!pmm_is_frame_used(frame)) {
            uint32_t eflags = pmm_disable_interrupts();
            if (!pmm_is_frame_used(frame)) {
                pmm_set_frame(frame, 1U);
                --g_pmm.used_frames;
                g_pmm.next_free_frame = frame + 1U;
                pmm_restore_interrupts(eflags);
                return (uint64_t)frame * PMM_PAGE_SIZE;
            }
            pmm_restore_interrupts(eflags);
        }
        ++frame;
        ++scanned;
    }
    return 0U;
}

uint64_t pmm_alloc_page(void) {
    return pmm_alloc_page_from(g_pmm.next_free_frame, 1U);
}

#ifdef PMM_QEMU_HIGHMEM_TEST
uint64_t pmm_alloc_page_at_or_above(uint64_t minimum_address) {
    if (minimum_address >= PMM_PHYSICAL_CEILING) return 0U;
    uint64_t first_frame =
        (minimum_address + PMM_PAGE_SIZE - 1U) / PMM_PAGE_SIZE;
    if (first_frame >= g_pmm.total_frames) return 0U;
    return pmm_alloc_page_from((uint32_t)first_frame, 0U);
}
#endif

void pmm_free_page(uint64_t physical_addr) {
    if (physical_addr == 0U || physical_addr >= PMM_PHYSICAL_CEILING ||
        (physical_addr & (PMM_PAGE_SIZE - 1U)) != 0U) return;
    uint32_t eflags = pmm_disable_interrupts();
    uint32_t frame = (uint32_t)(physical_addr / PMM_PAGE_SIZE);
    if (frame < g_pmm.total_frames && pmm_is_frame_used(frame)) {
        pmm_set_frame(frame, 0U);
        ++g_pmm.used_frames;
        if (frame < g_pmm.next_free_frame) g_pmm.next_free_frame = frame;
    }
    pmm_restore_interrupts(eflags);
}

uint32_t pmm_get_free_frames(void) {
    uint32_t free_frames = 0U;
    for (uint32_t frame = 0U; frame < g_pmm.total_frames; ++frame)
        if (!pmm_is_frame_used(frame)) ++free_frames;
    return free_frames;
}
