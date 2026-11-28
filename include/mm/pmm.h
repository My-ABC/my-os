#ifndef _PMM_H
#define _PMM_H

#include "stdint.h"

#define PMM_PAGE_SIZE 4096U
#define PMM_PHYSICAL_BITS 36U
#define PMM_PHYSICAL_CEILING (1ULL << PMM_PHYSICAL_BITS)
#define PMM_MAX_FRAMES ((uint32_t)(PMM_PHYSICAL_CEILING / PMM_PAGE_SIZE))
/* Bitmap covers PMM_MAX_FRAMES bits; total_frames should stay a
 * multiple of 8 so whole-byte skips in pmm_alloc_page_from never
 * need a partial tail (a clamp in the scan also guards this). */
#define PMM_BITMAP_SIZE (PMM_MAX_FRAMES / 8U)

typedef struct {
    uint32_t total_frames;
    uint32_t used_frames;
    uint32_t bitmap_size;
    uint8_t *bitmap;
    uint32_t next_free_frame;
} pmm_state_t;

void pmm_init(uint32_t multiboot_info_addr);
uint64_t pmm_alloc_page(void);
#ifdef PMM_QEMU_HIGHMEM_TEST
uint64_t pmm_alloc_page_at_or_above(uint64_t minimum_address);
#endif
void pmm_free_page(uint64_t physical_addr);
uint32_t pmm_get_free_frames(void);

#endif
