#include "syscall.h"
#include "rtc.h"
#include "stdio.h"
#include "vga.h"
#include "acpi.h"
#include "serial.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"

#define USER_HEAP_START 0x01000000U
#define USER_HEAP_END   0x40000000U
#define USER_BLOCK_MAGIC 0x55534552U

struct user_block {
    uint32_t magic;
    uint32_t size;
    uint32_t pages;
    uint32_t free;
    struct user_block *next;
};

static uint32_t user_heap_next = USER_HEAP_START;
static struct user_block *user_free_list;

static uint32_t user_pages_for_size(uint32_t size) {
    return (size + sizeof(struct user_block) + PAGE_SIZE - 1U) / PAGE_SIZE;
}

static struct user_block *user_find_free(uint32_t pages) {
    struct user_block *block = user_free_list;
    while (block != 0) {
        if (block->pages >= pages) {
            return block;
        }
        block = block->next;
    }
    return 0;
}

static void user_remove_free(struct user_block *block) {
    struct user_block **current = &user_free_list;
    while (*current != 0 && *current != block) {
        current = &(*current)->next;
    }
    if (*current == block) {
        *current = block->next;
        block->next = 0;
    }
}

static uint32_t user_restore_pages(struct user_block *block) {
    uint32_t block_addr = (uint32_t)block;
    for (uint32_t page = 1U; page < block->pages; ++page) {
        uint32_t virtual_addr = block_addr + page * PAGE_SIZE;
        if (paging_get_page_physical(virtual_addr) == 0U) {
            void *physical = pmm_alloc_page();
            if (physical == 0) {
                return 0;
            }
            paging_map_page(virtual_addr, (uint32_t)physical,
                            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
    }
    return 1;
}

static void *user_alloc(uint32_t size) {
    if (size == 0U || size > 0xFFFFFFFFU - sizeof(struct user_block)) {
        return 0;
    }

    uint32_t pages = user_pages_for_size(size);
    struct user_block *block = user_find_free(pages);
    if (block != 0) {
        user_remove_free(block);
        if (!user_restore_pages(block)) {
            block->free = 1;
            block->next = user_free_list;
            user_free_list = block;
            return 0;
        }
        block->free = 0;
        block->size = size;
        return (void *)((uint32_t)block + sizeof(struct user_block));
    }

    uint32_t address = (user_heap_next + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
    if (address < USER_HEAP_START || address + pages * PAGE_SIZE < address ||
        address + pages * PAGE_SIZE > USER_HEAP_END) {
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
                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }

    block = (struct user_block *)address;
    block->magic = USER_BLOCK_MAGIC;
    block->size = size;
    block->pages = pages;
    block->free = 0;
    block->next = 0;
    user_heap_next = address + pages * PAGE_SIZE;
    return (void *)(address + sizeof(struct user_block));
}

static void user_free(void *ptr) {
    if (ptr == 0) {
        return;
    }
    uint32_t address = (uint32_t)ptr;
    if (address < USER_HEAP_START + sizeof(struct user_block) || address >= USER_HEAP_END) {
        return;
    }

    struct user_block *block = (struct user_block *)(address - sizeof(struct user_block));
    if (block->magic != USER_BLOCK_MAGIC || block->free != 0U) {
        return;
    }

    for (uint32_t page = 1U; page < block->pages; ++page) {
        uint32_t virtual_addr = (uint32_t)block + page * PAGE_SIZE;
        uint32_t physical_addr = paging_get_page_physical(virtual_addr);
        paging_unmap_page(virtual_addr);
        if (physical_addr != 0U) {
            pmm_free_page((void *)(physical_addr & ~(PAGE_SIZE - 1U)));
        }
    }

    block->free = 1;
    block->next = user_free_list;
    user_free_list = block;
}

static void *user_realloc(void *ptr, uint32_t size) {
    if (ptr == 0) {
        return user_alloc(size);
    }
    if (size == 0U) {
        user_free(ptr);
        return 0;
    }

    struct user_block *block = (struct user_block *)((uint32_t)ptr - sizeof(struct user_block));
    if (block->magic != USER_BLOCK_MAGIC || block->free != 0U) {
        return 0;
    }

    if (user_pages_for_size(size) <= block->pages) {
        block->size = size;
        return ptr;
    }

    void *new_ptr = user_alloc(size);
    if (new_ptr == 0) {
        return 0;
    }
    uint32_t copy_size = block->size < size ? block->size : size;
    memcpy(new_ptr, ptr, copy_size);
    user_free(ptr);
    return new_ptr;
}

uint32_t syscall_handler() {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        : "=r"(eax), "=r"(ebx), "=r"(ecx), "=r"(edx)
        :
        : "memory"
    );

    switch (eax) {
        case SYS_WRITE: { // 参数: ebx: const char *str
            serial_print("[SYSCALL] SYS_WRITE from Ring 3\n");
            vga_print((const char*)ebx);
            return 0;
        }
        case SYS_READ: { // 参数: ebx: char *str, ecx: size_t size
            gets((char*)ebx, ecx);
            return 0;
        }
        case SYS_GETCHAR: {
            int c = getchar();
            return c;
        }
        case SYS_CLEAR: {
            vga_clear();
            return 0;
        }
        case SYS_REBOOT: {
            acpi_reboot();
            return 0;
        }
        case SYS_TIME: {
            rtc_time_t time;
            rtc_read_time(&time);
            uint32_t t = rtc_to_unix_timestamp(&time);
            return t;
        }
        case SYS_MEMORY:
            if (ebx == MEMORY_MALLOC) {
                return (uint32_t)user_alloc(ecx);
            }
            if (ebx == MEMORY_CALLOC) {
                if (ecx != 0U && edx > 0xFFFFFFFFU / ecx) {
                    return 0;
                }
                void *calloc_ptr = user_alloc(ecx * edx);
                if (calloc_ptr != 0) {
                    memset(calloc_ptr, 0, ecx * edx);
                }
                return (uint32_t)calloc_ptr;
            }
            if (ebx == MEMORY_REALLOC) {
                return (uint32_t)user_realloc((void *)ecx, edx);
            }
            if (ebx == MEMORY_FREE) {
                user_free((void *)ecx);
                return 0;
            }
            return 0;
        case SYS_POWER:
            if (ebx == POWER_SHUTDOWN) {
                acpi_shutdown();
            } else if (ebx == POWER_REBOOT) {
                acpi_reboot();
            }
            return 0;

        default:
            return -1;
    }
}

uint32_t sys_call(int call_number, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t res = 0;
    
    __asm__ volatile (
        "int $0x80"
        : "=a"(res)
        : "a"(call_number), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );

    return res;
}

void *sys_malloc(uint32_t size) {
    return (void *)sys_call(SYS_MEMORY, MEMORY_MALLOC, size, 0);
}

void *sys_calloc(uint32_t nmemb, uint32_t size) {
    return (void *)sys_call(SYS_MEMORY, MEMORY_CALLOC, nmemb, size);
}

void *sys_realloc(void *ptr, uint32_t size) {
    return (void *)sys_call(SYS_MEMORY, MEMORY_REALLOC, (uint32_t)ptr, size);
}

void sys_free(void *ptr) {
    sys_call(SYS_MEMORY, MEMORY_FREE, (uint32_t)ptr, 0);
}

void sys_shutdown(void) {
    sys_call(SYS_POWER, POWER_SHUTDOWN, 0, 0);
}

void sys_reboot(void) {
    sys_call(SYS_POWER, POWER_REBOOT, 0, 0);
}