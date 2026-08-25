#ifndef _PMM_H
#define _PMM_H

#include "stdint.h"

#define PMM_PAGE_SIZE 4096U

typedef struct {
    uint32_t total_frames;
    uint32_t used_frames;
    uint32_t bitmap_size;
    uint8_t *bitmap;
    uint32_t next_free_frame;
} pmm_state_t;

void pmm_init(uint32_t multiboot_info_addr);
void *pmm_alloc_page(void);
void pmm_free_page(void *addr);
uint32_t pmm_get_free_frames(void);

#endif
