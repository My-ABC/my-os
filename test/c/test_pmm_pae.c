#include "test_support.h"

#define PMM_HOST_TEST
#include "../../src/mm/pmm.c"

uint32_t kernel_end;

void serial_print(const char *text) {
    (void)text;
}

void serial_print_dec(uint32_t value) {
    (void)value;
}

int main(void) {
    const uint64_t expected = 0x100003000ULL;
    const uint32_t frame = (uint32_t)(expected / PMM_PAGE_SIZE);

    g_pmm.total_frames = PMM_MAX_FRAMES;
    g_pmm.used_frames = PMM_MAX_FRAMES;
    g_pmm.bitmap_size = PMM_BITMAP_SIZE;
    g_pmm.bitmap = g_frame_bitmap;
    g_pmm.next_free_frame = frame;
    memset(g_pmm.bitmap, 0xFF, g_pmm.bitmap_size);
    pmm_set_frame(frame, 0U);
    --g_pmm.used_frames;

    uint64_t physical = pmm_alloc_page();
    check(physical == expected, "PMM allocates a physical frame above 4 GiB");
    check(g_pmm.bitmap[frame >> 3] & (uint8_t)(1U << (frame & 7U)),
          "PMM marks the high physical frame as used");

    pmm_free_page(physical);
    check(!pmm_is_frame_used(frame), "PMM frees a physical frame above 4 GiB");

    return finish_test_suite("PAE PMM tests");
}
