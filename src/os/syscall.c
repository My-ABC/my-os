#include "syscall.h"
#include "rtc.h"
#include "stdio.h"
#include "vga.h"
#include "acpi.h"
#include "serial.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"
#include "ring3.h"
#include "signal.h"
#include "keyboard.h"
#include "fat32.h"
#include "heap.h"

#define USER_BLOCK_MAGIC 0x55534552U
#define SYSFS_MAX_FDS 8U
#define SYSFS_PATH_MAX 128U

/* Set by SYS_EXIT; the syscall stub switches to this kernel frame instead
 * of returning to the exiting process. */
uint32_t syscall_exit_frame;

/* Returns 1 after marking the current process for termination via the
 * saved kernel frame (same mechanism as SYS_EXIT). */
static int sys_signal_fatal_abort(void) {
    if (!ring3_signal_pending_fatal()) return 0;
    struct irq_frame *frame = ring3_terminate_current();
    if (frame != 0) syscall_exit_frame = (uint32_t)frame;
    return 1;
}

/* Returns 1 when the wait loop must stop: either a fatal signal killed
 * the process, or a handled signal redirected `frame` into its handler
 * (the in-flight syscall is abandoned, like an interrupted call). */
static int sys_signal_check(struct irq_frame *frame) {
    if (sys_signal_fatal_abort()) return 1;
    if (ring3_signal_deliver_current(frame)) return 1;
    return 0;
}

/* gets() variant that stops when a signal is delivered or fatal. */
static void sys_gets(char *buf, int size, struct irq_frame *frame) {
    int i = 0;
    while (i < size - 1) {
        char c = keyboard_getchar();
        if (c == KEYBOARD_KEY_SIGINT) {
            ring3_signal_send(ring3_current_pid(), SIGINT);
            if (sys_signal_check(frame)) return;
            continue;
        }
        if (c == 0) {
            if (sys_signal_check(frame)) return;
            __asm__ volatile ("hlt" ::: "memory");
            continue;
        }
        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            putchar('\n');
            return;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else {
            buf[i++] = c;
            putchar(c);
        }
    }
    buf[i] = '\0';
}
uint32_t syscall_switch_frame;

struct sysfs_fd {
    uint8_t used;
    uint8_t mode;
    uint32_t offset;
    uint32_t size;
    char path[SYSFS_PATH_MAX];
};

static struct sysfs_fd sysfs_fds[SYSFS_MAX_FDS];

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
    struct user_mm *mm = ring3_current_mm();
    if (mm == 0) return 0;
    uint32_t block_addr = (uint32_t)block;
    for (uint32_t page = 1U; page < block->pages; ++page) {
        uint32_t virtual_addr = block_addr + page * PAGE_SIZE;
        if (paging_mm_get_phys(mm, virtual_addr) == 0U) {
            uint64_t physical = pmm_alloc_page();
            if (physical == 0U) {
                return 0;
            }
            paging_mm_map(mm, virtual_addr, physical,
                          PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
    }
    return 1;
}

static void *user_alloc(uint32_t size) {
    struct user_mm *mm = ring3_current_mm();
    if (mm == 0) return 0;
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
        uint64_t physical = pmm_alloc_page();
        if (physical == 0U) {
            while (page > 0U) {
                --page;
                uint32_t virtual_addr = address + page * PAGE_SIZE;
                uint64_t physical_addr = paging_mm_get_phys(mm, virtual_addr);
                paging_mm_unmap(mm, virtual_addr);
                if (physical_addr != 0U) {
                    pmm_free_page(physical_addr & ~(uint64_t)(PAGE_SIZE - 1U));
                }
            }
            return 0;
        }
        paging_mm_map(mm, address + page * PAGE_SIZE, physical,
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
    struct user_mm *mm = ring3_current_mm();
    if (ptr == 0 || mm == 0) {
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
        uint64_t physical_addr = paging_mm_get_phys(mm, virtual_addr);
        paging_mm_unmap(mm, virtual_addr);
        if (physical_addr != 0U) {
            pmm_free_page(physical_addr & ~(uint64_t)(PAGE_SIZE - 1U));
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

uint32_t sys_user_heap_contains(uint32_t addr) {
    return addr >= USER_HEAP_START && addr < USER_HEAP_END;
}

/* Validate that every page covering [addr, addr+len) belongs to a registered
 * user range (live ELF image, user stack, or user heap). Rejects kernel
 * addresses and unmapped holes so bad pointers return -1 instead of
 * panicking the kernel on the subsequent copy. */
static int sys_user_buffer_ok(uint32_t addr, uint32_t len) {
    if (len == 0U) return 1;
    if (addr >= USER_SPACE_END || len > USER_SPACE_END - addr) return 0;
    uint32_t last = addr + len - 1U;
    for (uint32_t page = addr & ~(PAGE_SIZE - 1U); page <= last;
         page += PAGE_SIZE) {
        if (!ring3_user_range_permitted(page) &&
            !sys_user_heap_contains(page)) {
            return 0;
        }
    }
    return 1;
}

/* Copy a NUL-terminated user string into a kernel buffer, bounded by
 * dst_size. Returns the string length (excluding NUL) or -1. */
static int sys_copy_user_string(uint32_t addr, char *dst, uint32_t dst_size) {
    if (addr >= USER_SPACE_END || dst_size < 2U) return -1;
    for (uint32_t i = 0; i + 1U < dst_size; ++i) {
        uint32_t a = addr + i;
        if (a >= USER_SPACE_END ||
            (!ring3_user_range_permitted(a) && !sys_user_heap_contains(a))) {
            return -1;
        }
        char c = *(const volatile char *)a;
        dst[i] = c;
        if (c == '\0') return (int)i;
    }
    return -1;
}

static int sysfs_resolve_path(uint32_t user_path, char *out,
                              uint32_t out_size) {
    if (!fat32_mounted()) return -1;
    char input[SYSFS_PATH_MAX];
    if (sys_copy_user_string(user_path, input, sizeof(input)) < 0) return -1;
    return fat32_resolve_path(input, out, out_size);
}

static int sysfs_fd_index(uint32_t fd) {
    return fd < SYSFS_MAX_FDS && sysfs_fds[fd].used != 0U ? (int)fd : -1;
}

static int sysfs_fopen(uint32_t user_path, uint32_t mode) {
    if (mode > SYSFS_O_RDWR) return -1;
    char path[SYSFS_PATH_MAX];
    if (sysfs_resolve_path(user_path, path, sizeof(path)) != 0) return -1;

    uint32_t size = 0U;
    int is_dir = 0;
    int exists = fat32_stat(path, &size, &is_dir) == 0;
    if (is_dir != 0 || (exists == 0 && mode == SYSFS_O_RDONLY)) return -1;

    for (uint32_t i = 0; i < SYSFS_MAX_FDS; ++i) {
        if (sysfs_fds[i].used == 0U) {
            sysfs_fds[i].used = 1U;
            sysfs_fds[i].mode = (uint8_t)mode;
            sysfs_fds[i].offset = 0U;
            sysfs_fds[i].size = exists ? size : 0U;
            strncpy(sysfs_fds[i].path, path, SYSFS_PATH_MAX - 1U);
            sysfs_fds[i].path[SYSFS_PATH_MAX - 1U] = '\0';
            return (int)i;
        }
    }
    return -1;
}

static int sysfs_fread(struct sysfs_fd *fd, uint32_t buf, uint32_t size) {
    if (!sys_user_buffer_ok(buf, size)) return -1;
    if (size == 0U) return 0;

    uint8_t *kbuf = (uint8_t *)kmalloc(FAT32_MAX_FILE_SIZE);
    if (kbuf == 0) return -1;
    uint32_t file_size = 0U;
    if (fat32_read_file(fd->path, kbuf, FAT32_MAX_FILE_SIZE,
                        &file_size) != 0) {
        kfree(kbuf);
        return -1;
    }
    fd->size = file_size;
    if (fd->offset >= file_size) {
        kfree(kbuf);
        return 0;
    }
    uint32_t chunk = size;
    if (chunk > file_size - fd->offset) chunk = file_size - fd->offset;
    memcpy((void *)buf, kbuf + fd->offset, chunk);
    fd->offset += chunk;
    kfree(kbuf);
    return (int)chunk;
}

static int sysfs_fwrite(struct sysfs_fd *fd, uint32_t buf, uint32_t size) {
    if (fd->mode != SYSFS_O_RDWR) return -1;
    if (!sys_user_buffer_ok(buf, size)) return -1;
    if (size == 0U) return 0;
    if ((uint64_t)fd->offset + size > FAT32_MAX_FILE_SIZE) return -1;

    uint32_t old_size = 0U;
    int is_dir = 0;
    int exists = fat32_stat(fd->path, &old_size, &is_dir) == 0;
    if (exists != 0 && is_dir != 0) return -1;

    uint32_t needed = fd->offset + size;
    if (needed < old_size) needed = old_size;
    uint8_t *kbuf = (uint8_t *)kmalloc(needed);
    if (kbuf == 0) return -1;
    memset(kbuf, 0, needed);
    if (exists != 0) {
        uint32_t got = 0U;
        if (fat32_read_file(fd->path, kbuf, needed, &got) != 0) {
            kfree(kbuf);
            return -1;
        }
    }
    memcpy(kbuf + fd->offset, (const void *)buf, size);
    if (fat32_write_file(fd->path, kbuf, needed) != 0) {
        kfree(kbuf);
        return -1;
    }
    fd->offset += size;
    fd->size = needed;
    kfree(kbuf);
    return (int)size;
}

uint32_t syscall_handler(struct irq_frame *frame) {
    syscall_exit_frame = 0U;
    syscall_switch_frame = 0U;
    uint32_t eax = frame->eax;
    uint32_t ebx = frame->ebx;
    uint32_t ecx = frame->ecx;
    uint32_t edx = frame->edx;

    switch (eax) {
        case SYS_WRITE: { // 参数: ebx: const char *str
            uint32_t pid = ring3_current_pid();
            if (pid != 0U && pid == ring3_foreground_pid()) {
                vga_print((const char *)ebx);
                serial_print((const char *)ebx);
            }
            return 0;
        }
        case SYS_READ: { // 参数: ebx: char *str, ecx: size_t size
            sys_gets((char*)ebx, ecx, frame);
            return 0;
        }
        case SYS_GETCHAR: {
            for (;;) {
                char c = keyboard_getchar();
                if (c == KEYBOARD_KEY_SIGINT) {
                    /* Ctrl+C read by the syscall itself: deliver SIGINT
                     * to ourselves, then honor the disposition. */
                    ring3_signal_send(ring3_current_pid(), SIGINT);
                    if (sys_signal_check(frame)) return 0;
                    continue;
                }
                if (c != 0) return (uint8_t)c;
                if (sys_signal_check(frame)) return 0;
                __asm__ volatile ("hlt" ::: "memory");
            }
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
        case SYS_GETPID:
            return ring3_current_pid();
        case SYS_EXIT: {
            struct irq_frame *frame = ring3_terminate_current();
            if (frame != 0) syscall_exit_frame = (uint32_t)frame;
            return 0;
        }
        case SYS_YIELD:
            syscall_switch_frame = (uint32_t)ring3_yield_current(frame);
            return 0;
        case SYS_KILL:
            return ring3_signal_send(ebx, ecx) == 0 ? 0 : (uint32_t)-1;
        case SYS_SIGNAL:
            return ring3_signal_set_handler(ebx, ecx) == 0 ? 0 : (uint32_t)-1;
        case SYS_SIGRETURN:
            return ring3_signal_sigreturn(frame);
        case SYS_FOPEN:
            return (uint32_t)sysfs_fopen(ebx, ecx);
        case SYS_FCLOSE: {
            int index = sysfs_fd_index(ebx);
            if (index < 0) return (uint32_t)-1;
            memset(&sysfs_fds[index], 0, sizeof(sysfs_fds[index]));
            return 0;
        }
        case SYS_FREAD: {
            int index = sysfs_fd_index(ebx);
            if (index < 0) return (uint32_t)-1;
            return (uint32_t)sysfs_fread(&sysfs_fds[index], ecx, edx);
        }
        case SYS_FWRITE: {
            int index = sysfs_fd_index(ebx);
            if (index < 0) return (uint32_t)-1;
            return (uint32_t)sysfs_fwrite(&sysfs_fds[index], ecx, edx);
        }
        case SYS_FUNLINK: {
            char path[SYSFS_PATH_MAX];
            if (sysfs_resolve_path(ebx, path, sizeof(path)) != 0) {
                return (uint32_t)-1;
            }
            return fat32_delete_file(path) == 0 ? 0U : (uint32_t)-1;
        }
        case SYS_FMKDIR: {
            char path[SYSFS_PATH_MAX];
            if (sysfs_resolve_path(ebx, path, sizeof(path)) != 0) {
                return (uint32_t)-1;
            }
            return fat32_make_directory(path) == 0 ? 0U : (uint32_t)-1;
        }
        case SYS_FRMDIR: {
            char path[SYSFS_PATH_MAX];
            if (sysfs_resolve_path(ebx, path, sizeof(path)) != 0) {
                return (uint32_t)-1;
            }
            return fat32_remove_directory(path) == 0 ? 0U : (uint32_t)-1;
        }
        case SYS_FSTAT: {
            char path[SYSFS_PATH_MAX];
            if (sysfs_resolve_path(ebx, path, sizeof(path)) != 0) {
                return (uint32_t)-1;
            }
            uint32_t size = 0U;
            int is_dir = 0;
            if (fat32_stat(path, &size, &is_dir) != 0) return (uint32_t)-1;
            return is_dir != 0 ? (uint32_t)-2 : size;
        }
        case SYS_GETCWD: {
            if (!sys_user_buffer_ok(ebx, ecx)) return (uint32_t)-1;
            char cwd[SYSFS_PATH_MAX];
            int length = fat32_get_cwd(cwd, sizeof(cwd));
            if (length < 0 || (uint32_t)length + 1U > ecx) {
                return (uint32_t)-1;
            }
            memcpy((void *)ebx, cwd, (uint32_t)length + 1U);
            return (uint32_t)length;
        }
        case SYS_CHDIR: {
            char path[SYSFS_PATH_MAX];
            if (sysfs_resolve_path(ebx, path, sizeof(path)) != 0 ||
                fat32_list_directory(path, 0) != 0 ||
                fat32_set_cwd(path) != 0) {
                return (uint32_t)-1;
            }
            return 0;
        }

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

uint32_t sys_getpid(void) {
    return sys_call(SYS_GETPID, 0, 0, 0);
}