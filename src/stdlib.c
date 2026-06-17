#include "stdlib.h"
#include "stdio.h"
#include "memory.h"
#include "string.h"
#include "stdint.h"
#include "keyboard.h"
#include "isr.h"

// ========== 堆内存管理 ==========

// 堆块头（8 字节）
struct block_header {
    uint32_t size;          // 块大小（包含头）
    uint32_t magic;         // 魔数（用于检测损坏）
    struct block_header* next;
    struct block_header* prev;
    int used;               // 1=已使用, 0=空闲
};

#define BLOCK_MAGIC 0xDEADBEEF
#define HEADER_SIZE sizeof(struct block_header)
#define MIN_BLOCK_SIZE (HEADER_SIZE + 16)

// 堆起始地址
static void* heap_start = NULL;
static struct block_header* free_list = NULL;
static uint32_t heap_pages = 0;
static int heap_initialized = 0;

// ========== 堆初始化 ==========

void heap_init(void) {
    if (heap_initialized) return;
    
    // 从 5MB 开始作为堆
    heap_start = (void*)(5 * 1024 * 1024);
    heap_pages = 0;
    free_list = NULL;
    
    // 初始分配 16 页（64KB）作为堆
    void* addr = heap_start;
    for (int i = 0; i < 16; i++) {
        uint32_t page = pmm_alloc_page();
        if (page == 0) {
            print_error("heap_init: failed to allocate initial pages\n");
            return;
        }
        heap_pages++;
    }
    
    // 初始化第一个空闲块
    struct block_header* block = (struct block_header*)heap_start;
    block->size = 16 * 4096;  // 64KB
    block->magic = BLOCK_MAGIC;
    block->next = NULL;
    block->prev = NULL;
    block->used = 0;
    
    free_list = block;
    heap_initialized = 1;
    
    print_info("Heap initialized at 0x");
    char buf[16];
    uint32_t addr_val = (uint32_t)heap_start;
    const char* hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[addr_val & 0xF];
        addr_val >>= 4;
    }
    buf[8] = '\0';
    print_info(buf);
    print_info(" (64KB)\n");
}

// ========== 堆扩展 ==========

static int heap_extend(uint32_t size) {
    uint32_t pages = (size + 4095) / 4096;
    uint32_t needed = pages * 4096;
    
    // 在当前堆末尾分配新页
    void* new_addr = (void*)((uint32_t)heap_start + heap_pages * 4096);
    
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t page = pmm_alloc_page();
        if (page == 0) {
            return 0;  // 分配失败
        }
        heap_pages++;
    }
    
    // 创建新的空闲块
    struct block_header* block = (struct block_header*)new_addr;
    block->size = needed;
    block->magic = BLOCK_MAGIC;
    block->next = NULL;
    block->prev = NULL;
    block->used = 0;
    
    // 添加到空闲链表
    if (free_list) {
        struct block_header* last = free_list;
        while (last->next) last = last->next;
        last->next = block;
        block->prev = last;
    } else {
        free_list = block;
    }
    
    return 1;
}

// ========== 块合并 ==========

static void merge_blocks(struct block_header* block) {
    // 合并后一个块
    if (block->next && !block->next->used) {
        block->size += block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }
    // 合并前一个块
    if (block->prev && !block->prev->used) {
        block->prev->size += block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
        block = block->prev;
    }
}

// ========== 块分割 ==========

static void split_block(struct block_header* block, uint32_t size) {
    if (block->size < size + MIN_BLOCK_SIZE) return;
    
    uint32_t new_size = block->size - size;
    block->size = size;
    
    struct block_header* new_block = (struct block_header*)((uint32_t)block + size);
    new_block->size = new_size;
    new_block->magic = BLOCK_MAGIC;
    new_block->used = 0;
    new_block->next = block->next;
    new_block->prev = block;
    
    if (block->next) {
        block->next->prev = new_block;
    }
    block->next = new_block;
}

// ========== malloc ==========

void* malloc(size_t size) {
    if (size == 0) return NULL;
    
    if (!heap_initialized) {
        heap_init();
    }
    
    size += HEADER_SIZE;
    if (size < MIN_BLOCK_SIZE) size = MIN_BLOCK_SIZE;
    size = (size + 7) & ~7;  // 对齐到 8 字节
    
    // 查找空闲块
    struct block_header* block = free_list;
    while (block) {
        if (!block->used && block->size >= size) {
            if (block->size > size + MIN_BLOCK_SIZE) {
                split_block(block, size);
            }
            block->used = 1;
            block->magic = BLOCK_MAGIC;
            return (void*)((uint32_t)block + HEADER_SIZE);
        }
        block = block->next;
    }
    
    // 没有找到，扩展堆
    if (!heap_extend(size)) {
        return NULL;
    }
    
    // 重新查找（新块在链表末尾）
    block = free_list;
    while (block) {
        if (!block->used && block->size >= size) {
            if (block->size > size + MIN_BLOCK_SIZE) {
                split_block(block, size);
            }
            block->used = 1;
            block->magic = BLOCK_MAGIC;
            return (void*)((uint32_t)block + HEADER_SIZE);
        }
        block = block->next;
    }
    
    return NULL;
}

// ========== calloc ==========

void* calloc(size_t num, size_t size) {
    void* ptr = malloc(num * size);
    if (ptr) {
        memset(ptr, 0, num * size);
    }
    return ptr;
}

// ========== realloc ==========

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    struct block_header* block = (struct block_header*)((uint32_t)ptr - HEADER_SIZE);
    
    if (block->magic != BLOCK_MAGIC) {
        print_error("realloc: corrupted block\n");
        return NULL;
    }
    
    uint32_t old_data_size = block->size - HEADER_SIZE;
    
    // 如果当前块足够大
    if (block->size >= size + HEADER_SIZE) {
        if (block->size > size + HEADER_SIZE + MIN_BLOCK_SIZE) {
            split_block(block, size + HEADER_SIZE);
        }
        return ptr;
    }
    
    // 尝试合并下一个块
    if (block->next && !block->next->used) {
        uint32_t total_size = block->size + block->next->size;
        if (total_size >= size + HEADER_SIZE) {
            block->size = total_size;
            block->next = block->next->next;
            if (block->next) {
                block->next->prev = block;
            }
            if (block->size > size + HEADER_SIZE + MIN_BLOCK_SIZE) {
                split_block(block, size + HEADER_SIZE);
            }
            block->used = 1;
            return ptr;
        }
    }
    
    // 需要重新分配
    void* new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    
    uint32_t copy_size = (old_data_size < size) ? old_data_size : size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);
    return new_ptr;
}

// ========== free ==========

void free(void* ptr) {
    if (!ptr) return;
    
    struct block_header* block = (struct block_header*)((uint32_t)ptr - HEADER_SIZE);
    
    if (block->magic != BLOCK_MAGIC) {
        print_error("free: corrupted block\n");
        return;
    }
    
    block->used = 0;
    merge_blocks(block);
}

// ========== 堆检查 ==========

void heap_check(void) {
    struct block_header* block = free_list;
    while (block) {
        if (block->magic != BLOCK_MAGIC) {
            print_error("heap_check: corrupted block at ");
            char buf[16];
            uint32_t addr = (uint32_t)block;
            const char* hex = "0123456789ABCDEF";
            for (int i = 7; i >= 0; i--) {
                buf[i] = hex[addr & 0xF];
                addr >>= 4;
            }
            buf[8] = '\0';
            print_info(buf);
            print_info("\n");
            return;
        }
        block = block->next;
    }
}

// ========== 堆统计 ==========

void heap_stats(uint32_t* total, uint32_t* used, uint32_t* free_mem) {
    *total = 0;
    *used = 0;
    *free_mem = 0;
    
    struct block_header* block = free_list;
    while (block) {
        *total += block->size;
        if (block->used) {
            *used += block->size;
        } else {
            *free_mem += block->size;
        }
        block = block->next;
    }
}

// ========== atoi ==========

int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

// ========== atol ==========

long atol(const char* str) {
    long result = 0;
    int sign = 1;
    
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

// ========== itoa ==========

char* itoa(int value, char* str, int base) {
    if (base < 2 || base > 36) {
        str[0] = '\0';
        return str;
    }
    
    char* ptr = str;
    char* ptr1 = str;
    char tmp;
    int is_negative = 0;
    unsigned int num;
    
    if (value < 0 && base == 10) {
        is_negative = 1;
        num = -value;
    } else {
        num = value;
    }
    
    do {
        int digit = num % base;
        *ptr++ = (digit < 10) ? '0' + digit : 'A' + digit - 10;
        num /= base;
    } while (num);
    
    if (is_negative) {
        *ptr++ = '-';
    }
    *ptr = '\0';
    
    ptr--;
    while (ptr1 < ptr) {
        tmp = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp;
    }
    
    return str;
}

// ========== utoa ==========

char* utoa(unsigned int value, char* str, int base) {
    if (base < 2 || base > 36) {
        str[0] = '\0';
        return str;
    }
    
    char* ptr = str;
    char* ptr1 = str;
    char tmp;
    unsigned int num = value;
    
    do {
        int digit = num % base;
        *ptr++ = (digit < 10) ? '0' + digit : 'A' + digit - 10;
        num /= base;
    } while (num);
    
    *ptr = '\0';
    ptr--;
    while (ptr1 < ptr) {
        tmp = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp;
    }
    
    return str;
}

// ========== rand / srand ==========

static unsigned int _rand_seed = 1;
static int _rand_initialized = 0;

void srand_auto(void) {
    uint32_t seed = 0;
    
    // 使用 RDTSC 指令读取 CPU 时间戳
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    seed = lo ^ hi;
    
    // 混合返回地址
    seed ^= (uint32_t)__builtin_return_address(0);
    
    // 混合栈地址
    seed ^= (uint32_t)&seed;
    
    // 混合键盘数据
    if (keyboard_has_key()) {
        uint8_t sc;
        __asm__ volatile ("inb $0x60, %0" : "=a"(sc));
        seed ^= sc << 16;
    }
    
    // 最终混合
    seed = (seed << 13) ^ seed;
    seed = (seed >> 7) ^ seed;
    seed = (seed << 17) ^ seed;
    
    if (seed == 0) seed = 0x12345678;
    srand(seed);
}

void srand(unsigned int seed) {
    _rand_seed = seed;
    _rand_initialized = 1;
}

int rand(void) {
    if (!_rand_initialized) {
        srand_auto();
        _rand_initialized = 1;
    }
    _rand_seed = _rand_seed * 1103515245 + 12345;
    return (int)((_rand_seed / 65536) % 32768);
}

// ========== abort ==========

void abort(void) {
    print_error("abort() called!\n");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

// ========== exit ==========

void exit(int status) {
    print_info("exit(");
    char buf[16];
    itoa(status, buf, 10);
    print_info(buf);
    print_info(") called\n");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

// ========== system ==========

void system(const char* command) {
    print_info("system: ");
    print_info(command);
    print_info("\n");
}