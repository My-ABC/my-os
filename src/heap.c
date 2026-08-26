#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"

#define KERNEL_HEAP_START 0xD0000000U
#define KERNEL_HEAP_END   0xE0000000U
#define HEAP_BLOCK_MAGIC 0x48454150U

struct heap_block {
    uint32_t magic;
    uint32_t size;
    uint32_t pages;
    uint32_t free;
    struct heap_block *next;
    struct heap_block *prev;
} __attribute__((packed));

static uint32_t heap_next = KERNEL_HEAP_START;
static struct heap_block *free_list;

static uint32_t heap_align_up(uint32_t value) {
    return (value + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
}

static uint32_t heap_pages_for_size(uint32_t size) {
    return heap_align_up(size + sizeof(struct heap_block)) / PAGE_SIZE;
}

static void heap_remove_free(struct heap_block *block) {
    if (block->prev != 0) {
        block->prev->next = block->next;
    } else {
        free_list = block->next;
    }
    if (block->next != 0) {
        block->next->prev = block->prev;
    }
    block->next = 0;
    block->prev = 0;
}

static void heap_insert_free(struct heap_block *block) {
    struct heap_block *current = free_list;
    struct heap_block *previous = 0;

    block->free = 1;
    while (current != 0 && (uint32_t)current < (uint32_t)block) {
        previous = current;
        current = current->next;
    }

    block->prev = previous;
    block->next = current;
    if (previous != 0) {
        previous->next = block;
    } else {
        free_list = block;
    }
    if (current != 0) {
        current->prev = block;
    }
}

static uint32_t heap_adjacent(struct heap_block *left, struct heap_block *right) {
    return (uint32_t)left + left->pages * PAGE_SIZE == (uint32_t)right;
}

static void heap_merge_right(struct heap_block *left, struct heap_block *right) {
    left->pages += right->pages;
    left->size = left->pages * PAGE_SIZE - sizeof(struct heap_block);
    left->next = right->next;
    if (right->next != 0) {
        right->next->prev = left;
    }
}

static void heap_coalesce(struct heap_block *block) {
    if (block->prev != 0 && heap_adjacent(block->prev, block)) {
        block = block->prev;
        heap_merge_right(block, block->next);
    }
    if (block->next != 0 && heap_adjacent(block, block->next)) {
        heap_merge_right(block, block->next);
    }
}

static struct heap_block *heap_find_free(uint32_t pages) {
    struct heap_block *block = free_list;
    while (block != 0) {
        if (block->pages >= pages) {
            return block;
        }
        block = block->next;
    }
    return 0;
}

static void heap_split(struct heap_block *block, uint32_t pages) {
    uint32_t remaining = block->pages - pages;
    if (remaining == 0U) {
        return;
    }

    struct heap_block *remainder = (struct heap_block *)((uint32_t)block + pages * PAGE_SIZE);
    remainder->magic = HEAP_BLOCK_MAGIC;
    remainder->size = remaining * PAGE_SIZE - sizeof(struct heap_block);
    remainder->pages = remaining;
    remainder->free = 1;
    remainder->prev = block->prev;
    remainder->next = block->next;
    if (remainder->prev != 0) {
        remainder->prev->next = remainder;
    } else {
        free_list = remainder;
    }
    if (remainder->next != 0) {
        remainder->next->prev = remainder;
    }
    block->pages = pages;
    block->prev = 0;
    block->next = 0;
}

static void *heap_map_new(uint32_t pages, uint32_t size) {
    uint32_t mapping_size = pages * PAGE_SIZE;
    uint32_t address = heap_align_up(heap_next);
    if (address < KERNEL_HEAP_START || address + mapping_size < address ||
        address + mapping_size > KERNEL_HEAP_END) {
        return 0;
    }

    for (uint32_t page = 0; page < pages; ++page) {
        void *physical = pmm_alloc_page();
        if (physical == 0) {
            while (page > 0U) {
                --page;
                uint32_t virtual_addr = address + page * PAGE_SIZE;
                uint32_t physical_addr = paging_get_page_physical(virtual_addr);
                paging_unmap_page(virtual_addr);
                if (physical_addr != 0U) {
                    pmm_free_page((void *)(physical_addr & ~(PAGE_SIZE - 1U)));
                }
            }
            return 0;
        }
        paging_map_page(address + page * PAGE_SIZE, (uint32_t)physical,
                        PAGE_PRESENT | PAGE_WRITABLE);
    }

    struct heap_block *block = (struct heap_block *)address;
    block->magic = HEAP_BLOCK_MAGIC;
    block->size = size;
    block->pages = pages;
    block->free = 0;
    block->next = 0;
    block->prev = 0;
    heap_next = address + mapping_size;
    return (void *)(address + sizeof(struct heap_block));
}

void *kmalloc(size_t size) {
    uint32_t requested = (uint32_t)size;
    if (requested == 0U || requested > 0xFFFFFFFFU - sizeof(struct heap_block)) {
        return 0;
    }

    uint32_t pages = heap_pages_for_size(requested);
    struct heap_block *block = heap_find_free(pages);
    if (block == 0) {
        return heap_map_new(pages, requested);
    }

    heap_remove_free(block);
    heap_split(block, pages);
    block->size = requested;
    block->free = 0;
    return (void *)((uint32_t)block + sizeof(struct heap_block));
}

void kfree(void *ptr) {
    if (ptr == 0) {
        return;
    }

    uint32_t address = (uint32_t)ptr;
    if (address < KERNEL_HEAP_START + sizeof(struct heap_block) || address >= KERNEL_HEAP_END) {
        serial_print("[HEAP] Invalid pointer passed to kfree\n");
        return;
    }

    struct heap_block *block = (struct heap_block *)(address - sizeof(struct heap_block));
    if (block->magic != HEAP_BLOCK_MAGIC || block->pages == 0U || block->free != 0U) {
        serial_print("[HEAP] Invalid allocation passed to kfree\n");
        return;
    }

    heap_insert_free(block);
    heap_coalesce(block);
}

void *kcalloc(size_t nmemb, size_t size) {
    uint32_t count = (uint32_t)nmemb;
    uint32_t element_size = (uint32_t)size;
    if (count != 0U && element_size > 0xFFFFFFFFU / count) {
        return 0;
    }

    uint32_t total = count * element_size;
    void *ptr = kmalloc(total);
    if (ptr != 0) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *krealloc(void *ptr, size_t size) {
    uint32_t requested = (uint32_t)size;
    if (ptr == 0) {
        return kmalloc(size);
    }
    if (requested == 0U) {
        kfree(ptr);
        return 0;
    }

    struct heap_block *block = (struct heap_block *)((uint32_t)ptr - sizeof(struct heap_block));
    if (block->magic != HEAP_BLOCK_MAGIC || block->free != 0U) {
        return 0;
    }

    uint32_t new_pages = heap_pages_for_size(requested);
    if (new_pages <= block->pages) {
        uint32_t old_pages = block->pages;
        block->size = requested;
        if (old_pages > new_pages) {
            struct heap_block *remainder = (struct heap_block *)((uint32_t)block + new_pages * PAGE_SIZE);
            remainder->magic = HEAP_BLOCK_MAGIC;
            remainder->size = (old_pages - new_pages) * PAGE_SIZE - sizeof(struct heap_block);
            remainder->pages = old_pages - new_pages;
            remainder->free = 0;
            block->pages = new_pages;
            kfree((void *)((uint32_t)remainder + sizeof(struct heap_block)));
        }
        return ptr;
    }

    struct heap_block *next = (struct heap_block *)((uint32_t)block + block->pages * PAGE_SIZE);
    if ((uint32_t)next < heap_next && next->magic == HEAP_BLOCK_MAGIC && next->free != 0U &&
        block->pages + next->pages >= new_pages) {
        heap_remove_free(next);
        block->pages += next->pages;
        if (block->pages > new_pages) {
            struct heap_block *remainder = (struct heap_block *)((uint32_t)block + new_pages * PAGE_SIZE);
            remainder->magic = HEAP_BLOCK_MAGIC;
            remainder->size = (block->pages - new_pages) * PAGE_SIZE - sizeof(struct heap_block);
            remainder->pages = block->pages - new_pages;
            remainder->free = 0;
            block->pages = new_pages;
            kfree((void *)((uint32_t)remainder + sizeof(struct heap_block)));
        }
        block->size = requested;
        return ptr;
    }

    void *new_ptr = kmalloc(size);
    if (new_ptr == 0) {
        return 0;
    }
    uint32_t copy_size = block->size < requested ? block->size : requested;
    memcpy(new_ptr, ptr, copy_size);
    kfree(ptr);
    return new_ptr;
}
