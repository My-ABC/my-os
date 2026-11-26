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
#include "tsc.h"
#include "pit.h"
#include "elf.h"

#define USER_BLOCK_MAGIC 0x55534552U
#define SYSFS_MAX_FDS 8U
#define SYSFS_PATH_MAX 128U

extern const uint8_t elf_demo_start[];
extern const uint8_t elf_demo_end[];

/* Set by PROC_EXIT; the syscall stub switches to this kernel frame instead
 * of returning to the exiting process. */
uint32_t syscall_exit_frame;

/* Returns 1 after marking the current process for termination via the
 * saved kernel frame (same mechanism as PROC_EXIT). */
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

extern uint32_t syscall_switch_frame;

/* Yield the CPU while waiting for keyboard input. The saved user frame's
 * EIP is rewound past `int 0x80` (CD 80) so the syscall restarts from
 * scratch when the process is rescheduled. Yields to the kernel idle
 * context on every wait iteration so deferred kernel work (vt_service)
 * is not starved by a lone blocked process. Returns nonzero when
 * yielded. */
static int sys_keyboard_wait_yield(struct irq_frame *frame) {
    if (ring3_current_pid() == 0U) return 0;
    /* Always yield to the kernel idle context while waiting for a
     * key, even when no other Ring 3 process is runnable. The idle
     * loop (shell supervisor / kernel shell) owns vt_service() and
     * other deferred work; without this a lone process blocked in
     * IO_GETCHAR would starve it forever. */
    struct irq_frame *kframe = ring3_yield_current(frame);
    if (kframe == 0) return 0;
    /* Rewind EIP past `int 0x80` (CD 80) only after the yield
     * succeeded: the resumed frame re-executes the syscall with the
     * register state saved in the frame. The syscall stub writes the
     * handler's return value into the frame's eax slot before the
     * diversion, so callers must return the original class (`klass`)
     * on this path — that is what makes the restarted int 0x80
     * dispatch the same syscall again. */
    frame->eip -= 2;
    syscall_switch_frame = (uint32_t)kframe;
    return 1;
}

/* gets() variant that stops when a signal is delivered, fatal, or the
 * CPU is yielded to another process. Returns 0 on a completed line,
 * 1 on a signal abort, 2 when the wait was yielded (the syscall is
 * restarted on resume, so callers must return `klass` to the stub). */
static int sys_gets(char *buf, int size, struct irq_frame *frame) {
    int i = 0;
    int vt = (int)ring3_current_vt();
    while (i < size - 1) {
        /* Only the foreground process on this vt may consume keys;
         * a background reader (e.g. the shell while `run` hosts a
         * child) waits without stealing keystrokes. */
        char c = ring3_foreground_pid_vt(vt) == ring3_current_pid()
                     ? keyboard_getchar_vt(vt) : 0;
        if (c == KEYBOARD_KEY_SIGINT) {
            ring3_signal_send(ring3_current_pid(), SIGINT);
            if (sys_signal_check(frame)) {
                /* A fatal abort already switched to the kernel CR3 and
                 * released the mm; writing buf there would fault. */
                if (syscall_exit_frame == 0U) buf[i] = '\0';
                return 1;
            }
            continue;
        }
        if (c == 0) {
            if (sys_signal_check(frame)) {
                if (syscall_exit_frame == 0U) buf[i] = '\0';
                return 1;
            }
            /* Yield only before the first character: a yield restarts
             * the whole syscall, so yielding mid-line would drop the
             * characters already consumed. */
            if (i == 0 && sys_keyboard_wait_yield(frame)) return 2;
            __asm__ volatile ("hlt" ::: "memory");
            continue;
        }
        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            putchar('\n');
            return 0;
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
    return 0;
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

static uint32_t user_pages_for_size(uint32_t size) {
    return (size + sizeof(struct user_block) + PAGE_SIZE - 1U) / PAGE_SIZE;
}

static struct user_block *user_find_free(struct user_mm *mm,
                                         uint32_t pages) {
    struct user_block *block = mm->user_free_list;
    while (block != 0) {
        if (block->pages >= pages) {
            return block;
        }
        block = block->next;
    }
    return 0;
}

static void user_remove_free(struct user_mm *mm, struct user_block *block) {
    struct user_block **current = &mm->user_free_list;
    while (*current != 0 && *current != block) {
        current = &(*current)->next;
    }
    if (*current == block) {
        *current = block->next;
        block->next = 0;
    }
}

static uint32_t user_restore_pages(struct user_mm *mm,
                                   struct user_block *block) {
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

static void *user_alloc(struct user_mm *mm, uint32_t size) {
    if (size == 0U || size > 0xFFFFFFFFU - sizeof(struct user_block)) {
        return 0;
    }

    uint32_t pages = user_pages_for_size(size);
    struct user_block *block = user_find_free(mm, pages);
    if (block != 0) {
        user_remove_free(mm, block);
        if (!user_restore_pages(mm, block)) {
            block->free = 1;
            block->next = mm->user_free_list;
            mm->user_free_list = block;
            return 0;
        }
        block->free = 0;
        block->size = size;
        return (void *)((uint32_t)block + sizeof(struct user_block));
    }

    uint32_t address = (mm->user_heap_next + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
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
    mm->user_heap_next = address + pages * PAGE_SIZE;
    return (void *)(address + sizeof(struct user_block));
}

static void user_free(struct user_mm *mm, void *ptr) {
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
    /* block->pages is read from user memory; a corrupt value would make
     * the unmap loop walk far outside the heap. Reject zero and any
     * range that does not fit inside USER_HEAP_END. */
    if (block->pages == 0U ||
        (uint64_t)(uint32_t)block + (uint64_t)block->pages * PAGE_SIZE >
            USER_HEAP_END) {
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
    block->next = mm->user_free_list;
    mm->user_free_list = block;
}

static void *user_realloc(struct user_mm *mm, void *ptr, uint32_t size) {
    if (ptr == 0) {
        return user_alloc(mm, size);
    }
    if (size == 0U) {
        user_free(mm, ptr);
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

    void *new_ptr = user_alloc(mm, size);
    if (new_ptr == 0) {
        return 0;
    }
    uint32_t copy_size = block->size < size ? block->size : size;
    memcpy(new_ptr, ptr, copy_size);
    user_free(mm, ptr);
    return new_ptr;
}

uint32_t sys_user_heap_contains(uint32_t addr) {
    return addr >= USER_HEAP_START && addr < USER_HEAP_END;
}

/* Validate that every page covering [addr, addr+len) is mapped for
 * the requested access in the current process's address space.
 * Range-permission checks are not sufficient: a permitted-but-unmapped
 * page (e.g. an unregistered image range or a not-yet-demand-paged
 * stack page) would fault in kernel context and panic. The PTE must
 * be present and user-accessible; write targets additionally require
 * PAE_WRITABLE. This also rejects the borrowed supervisor identity
 * map below 4 MiB that paging_mm_create copies into every user_mm —
 * without the USER/WRITABLE checks a syscall pointer into that range
 * would read/write arbitrary physical memory as the kernel. */
static int sys_user_page_ok(uint32_t page, int writable) {
    struct user_mm *mm = ring3_current_mm();
    if (mm == 0) return 0;
    uint64_t pte = paging_mm_get_pte(mm, page);
    if ((pte & PAE_PRESENT) == 0U || (pte & PAE_USER) == 0U) return 0;
    if (writable != 0 && (pte & PAE_WRITABLE) == 0U) return 0;
    return 1;
}

static int sys_user_buffer_ok(uint32_t addr, uint32_t len, int writable) {
    if (len == 0U) return 1;
    if (addr >= USER_SPACE_END || len > USER_SPACE_END - addr) return 0;
    uint32_t last = addr + len - 1U;
    for (uint32_t page = addr & ~(PAGE_SIZE - 1U); page <= last;
         page += PAGE_SIZE) {
        if (!sys_user_page_ok(page, writable)) {
            return 0;
        }
    }
    return 1;
}

/* Copy a NUL-terminated user string into a kernel buffer, bounded by
 * dst_size. Returns the string length (excluding NUL) or -1. */
static int sys_copy_user_string(uint32_t addr, char *dst, uint32_t dst_size) {
    if (addr >= USER_SPACE_END || dst_size < 2U) return -1;
    uint32_t checked_page = 0xFFFFFFFFU;
    for (uint32_t i = 0; i + 1U < dst_size; ++i) {
        uint32_t a = addr + i;
        if (a >= USER_SPACE_END) return -1;
        uint32_t page = a & ~(PAGE_SIZE - 1U);
        if (page != checked_page) {
            if (!sys_user_page_ok(page, 0)) return -1;
            checked_page = page;
        }
        char c = *(const volatile char *)a;
        dst[i] = c;
        if (c == '\0') return (int)i;
    }
    return -1;
}

struct readdir_ctx {
    char *buf;
    uint32_t len;
    uint32_t cap;
};

static struct readdir_ctx sysfs_readdir_ctx;

/* fat32_list_directory callback: append "d name\n" or "- name\n" lines.
 * Runs with no file I/O, only writes into the kernel staging buffer. */
static void sysfs_readdir_cb(const char *name, uint32_t attr,
                             uint32_t size) {
    struct readdir_ctx *ctx = &sysfs_readdir_ctx;
    uint32_t nlen = 0;
    while (name[nlen] != '\0' && nlen < 255U) ++nlen;
    uint32_t need = 2U + nlen + 1U;
    if (ctx->len + need > ctx->cap) return;
    char *p = ctx->buf + ctx->len;
    *p++ = (attr & 0x10U) ? 'd' : '-';
    *p++ = ' ';
    memcpy(p, name, nlen);
    p += nlen;
    *p++ = '\n';
    ctx->len += need;
    (void)size;
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
    if (!sys_user_buffer_ok(buf, size, 1)) return -1;
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
    if (!sys_user_buffer_ok(buf, size, 0)) return -1;
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
    uint32_t klass = frame->eax;
    uint32_t op = frame->ebx;
    uint32_t a1 = frame->ecx;
    uint32_t a2 = frame->edx;
    uint32_t a3 = frame->esi;

    switch (klass) {
        case SYS_IO:
            switch (op) {
                case IO_WRITE: { // a1: const char *str
                    uint32_t pid = ring3_current_pid();
                    if (pid == 0U ||
                        pid != ring3_foreground_pid_vt((int)ring3_current_vt())) {
                        return 0;
                    }
                    /* Copy the user string into a bounded kernel buffer
                     * first: a bad pointer or a missing NUL used to fault
                     * in kernel context and panic. Cap at 4096 bytes. */
                    char *text = (char *)kmalloc(IO_WRITE_MAX + 1U);
                    if (text == 0) return (uint32_t)-1;
                    int len = sys_copy_user_string(a1, text,
                                                  IO_WRITE_MAX + 1U);
                    if (len < 0 || len > IO_WRITE_MAX) {
                        kfree(text);
                        return (uint32_t)-1;
                    }
                    vga_print(text);
                    serial3_print(text);
                    kfree(text);
                    return 0;
                }
                case IO_READ: // a1: char *buf, a2: size
                    /* a2 arrives as uint32_t; sys_gets takes int, so
                     * values > INT32_MAX wrap negative. Reject non-
                     * positive sizes and buffers that are not wholly
                     * inside permitted user ranges before any write. */
                    if ((int32_t)a2 <= 0 ||
                        !sys_user_buffer_ok(a1, (uint32_t)(int32_t)a2, 1)) {
                        return (uint32_t)-1;
                    }
                    int gets_rc = sys_gets((char *)a1, (int)a2, frame);
                    if (gets_rc == 2) {
                        /* Yielded: the frame's eip was rewound and the
                         * stub diverts to the kernel context. Returning
                         * `klass` lands SYS_IO in the saved eax slot so
                         * the restarted int 0x80 re-dispatches IO_READ
                         * with the original arguments. */
                        return klass;
                    }
                    if (gets_rc != 0) {
                        return (uint32_t)-1;
                    }
                    /* POSIX-style: return the line length in bytes
                     * (excluding the NUL terminator); 0 = empty line. */
                    return (uint32_t)strlen((const char *)a1);
                case IO_GETCHAR: {
                    int vt = (int)ring3_current_vt();
                    for (;;) {
                        char c = ring3_foreground_pid_vt(vt) == ring3_current_pid()
                                     ? keyboard_getchar_vt(vt) : 0;
                        if (c == KEYBOARD_KEY_SIGINT) {
                            /* Ctrl+C read by the syscall itself: deliver SIGINT
                             * to ourselves, then honor the disposition. */
                            ring3_signal_send(ring3_current_pid(), SIGINT);
                            if (sys_signal_check(frame)) return 0;
                            continue;
                        }
                        if (c != 0) return (uint8_t)c;
                        if (sys_signal_check(frame)) return 0;
                        /* Same yield-restart contract as IO_READ: the
                         * stub writes this return into the frame's eax
                         * slot, so `klass` makes int 0x80 re-dispatch
                         * IO_GETCHAR on resume. */
                        if (sys_keyboard_wait_yield(frame)) return klass;
                        __asm__ volatile ("hlt" ::: "memory");
                    }
                }
                case IO_CLEAR:
                    vga_clear();
                    return 0;
                case IO_CURSOR_GET: {
                    uint16_t row = 0, col = 0;
                    vga_get_cursor(&row, &col);
                    return ((uint32_t)row << 16) | col;
                }
                case IO_CURSOR_SET:
                    vga_set_cursor((uint16_t)a1, (uint16_t)a2);
                    return 0;
                case IO_CONSOLE_INFO:
                    return ((uint32_t)vga_rows() << 16) | vga_columns();
                default:
                    return 0;
            }
        case SYS_TIME:
            switch (op) {
                case TIME_GET: {
                    rtc_time_t time;
                    rtc_read_time(&time);
                    return rtc_to_unix_timestamp(&time);
                }
                case TIME_TO: { // a1: offset seconds, a2: rtc_time_t *buf
                    if (!sys_user_buffer_ok(a2, sizeof(rtc_time_t), 1)) {
                        return (uint32_t)-1;
                    }
                    rtc_time_t time;
                    rtc_read_time(&time);
                    rtc_convert_timezone(&time, (int)a1);
                    memcpy((void *)a2, &time, sizeof(time));
                    return 0;
                }
                case TIME_SLEEP: { // a1: microseconds
                    if (tsc_freq_hz() == 0U) return (uint32_t)-1;
                    uint64_t deadline = tsc_read() + tsc_cycles_for_us(a1);
                    uint32_t last_tick = pit_ticks();
                    for (;;) {
                        if ((int64_t)(tsc_read() - deadline) >= 0) break;
                        if (pit_ticks() != last_tick) {
                            last_tick = pit_ticks();
                            if (sys_signal_check(frame)) return 0;
                        }
                        __asm__ volatile ("pause");
                    }
                    return 0;
                }
                default:
                    return 0;
            }
        case SYS_MEMORY: {
            struct user_mm *mm = ring3_current_mm();
            switch (op) {
                case MEMORY_MALLOC:
                    return (uint32_t)user_alloc(mm, a1);
                case MEMORY_CALLOC:
                    if (a1 != 0U && a2 > 0xFFFFFFFFU / a1) {
                        return 0;
                    }
                    {
                        void *calloc_ptr = user_alloc(mm, a1 * a2);
                        if (calloc_ptr != 0) {
                            memset(calloc_ptr, 0, a1 * a2);
                        }
                        return (uint32_t)calloc_ptr;
                    }
                case MEMORY_REALLOC:
                    return (uint32_t)user_realloc(mm, (void *)a1, a2);
                case MEMORY_FREE:
                    user_free(mm, (void *)a1);
                    return 0;
                default:
                    return 0;
            }
        }
        case SYS_POWER:
            if (op == PWR_SHUTDOWN) {
                acpi_shutdown();
            } else if (op == PWR_REBOOT) {
                acpi_reboot();
            }
            return 0;
        case SYS_PROCESS:
            switch (op) {
                case PROC_GETPID:
                    return ring3_current_pid();
                case PROC_EXIT: {
                    struct irq_frame *exit_frame = ring3_terminate_current();
                    if (exit_frame != 0) syscall_exit_frame = (uint32_t)exit_frame;
                    return 0;
                }
                case PROC_YIELD:
                    /* A process spinning in a yield loop (e.g. a shell
                     * waiting for a foreground child) never enters a
                     * blocking wait, so this is the only place a
                     * pending fatal/handled signal can be noticed. */
                    if (sys_signal_check(frame)) return 0;
                    syscall_switch_frame = (uint32_t)ring3_yield_current(frame);
                    return 0;
                case PROC_LIST: {
                    if (!sys_user_buffer_ok(a1, a2, 1)) return (uint32_t)-1;
                    uint32_t pids[RING3_PROCESS_COUNT];
                    uint32_t states[RING3_PROCESS_COUNT];
                    uint32_t fg = ring3_foreground_pid_vt((int)ring3_current_vt());
                    uint32_t count = ring3_process_list(pids, states,
                                                        RING3_PROCESS_COUNT);
                    if (a2 < count * 12U) return (uint32_t)-1;
                    for (uint32_t i = 0; i < count; ++i) {
                        uint32_t *rec = (uint32_t *)(a1 + i * 12U);
                        rec[0] = pids[i];
                        rec[1] = states[i];
                        rec[2] = (fg != 0U && pids[i] == fg) ? 1U : 0U;
                    }
                    return count;
                }
                case PROC_SET_FG:
                    if (a1 == 0U) {
                        uint32_t fg = ring3_foreground_pid_vt(
                            (int)ring3_current_vt());
                        return fg != 0U && ring3_set_background(fg) == 0
                               ? 0 : (uint32_t)-1;
                    }
                    return ring3_set_foreground(a1) == 0 ? 0 : (uint32_t)-1;
                case PROC_EXEC: {
                    char path[SYSFS_PATH_MAX];
                    if (sysfs_resolve_path(a1, path, sizeof(path)) != 0) {
                        return (uint32_t)-1;
                    }
                    uint8_t *image = (uint8_t *)kmalloc(FAT32_MAX_FILE_SIZE);
                    if (image == 0) return (uint32_t)-1;
                    uint32_t size = 0;
                    int rc = fat32_read_file(path, image, FAT32_MAX_FILE_SIZE,
                                             &size);
                    if (rc == 0) rc = elf_run(image, size);
                    kfree(image);
                    return rc == 0 ? 0U : (uint32_t)-1;
                }
                case PROC_RUN_DEMO: {
                    uint32_t size =
                        (uint32_t)(elf_demo_end - elf_demo_start);
                    return elf_run(elf_demo_start, size) == 0
                           ? 0U : (uint32_t)-1;
                }
                case PROC_RUN_USERCODE: {
                    struct user_mm *mm = paging_mm_create();
                    if (mm == 0) return (uint32_t)-1;
                    paging_mm_map(mm, (uint32_t)user_entry,
                                  (uint32_t)user_entry,
                                  PAGE_PRESENT | PAGE_WRITABLE |
                                  PAGE_USER | PAGE_BORROWED);
                    if (paging_mm_get_phys(mm, (uint32_t)user_entry) == 0U) {
                        paging_mm_release(mm);
                        return (uint32_t)-1;
                    }
                    return ring3_schedule_mm(user_entry, mm, 0U, 0U, 1,
                                             (int)ring3_current_vt()) == 0
                           ? 0U : (uint32_t)-1;
                }
                default:
                    return 0;
            }
        case SYS_FS:
            switch (op) {
                case FS_OPEN:
                    return (uint32_t)sysfs_fopen(a1, a2);
                case FS_CLOSE: {
                    int index = sysfs_fd_index(a1);
                    if (index < 0) return (uint32_t)-1;
                    memset(&sysfs_fds[index], 0, sizeof(sysfs_fds[index]));
                    return 0;
                }
                case FS_READ: {
                    int index = sysfs_fd_index(a1);
                    if (index < 0) return (uint32_t)-1;
                    return (uint32_t)sysfs_fread(&sysfs_fds[index], a2, a3);
                }
                case FS_WRITE: {
                    int index = sysfs_fd_index(a1);
                    if (index < 0) return (uint32_t)-1;
                    return (uint32_t)sysfs_fwrite(&sysfs_fds[index], a2, a3);
                }
                case FS_UNLINK: {
                    char path[SYSFS_PATH_MAX];
                    if (sysfs_resolve_path(a1, path, sizeof(path)) != 0) {
                        return (uint32_t)-1;
                    }
                    return fat32_delete_file(path) == 0 ? 0U : (uint32_t)-1;
                }
                case FS_MKDIR: {
                    char path[SYSFS_PATH_MAX];
                    if (sysfs_resolve_path(a1, path, sizeof(path)) != 0) {
                        return (uint32_t)-1;
                    }
                    return fat32_make_directory(path) == 0 ? 0U : (uint32_t)-1;
                }
                case FS_RMDIR: {
                    char path[SYSFS_PATH_MAX];
                    if (sysfs_resolve_path(a1, path, sizeof(path)) != 0) {
                        return (uint32_t)-1;
                    }
                    return fat32_remove_directory(path) == 0 ? 0U : (uint32_t)-1;
                }
                case FS_STAT: {
                    char path[SYSFS_PATH_MAX];
                    if (sysfs_resolve_path(a1, path, sizeof(path)) != 0) {
                        return (uint32_t)-1;
                    }
                    uint32_t size = 0U;
                    int is_dir = 0;
                    if (fat32_stat(path, &size, &is_dir) != 0) return (uint32_t)-1;
                    return is_dir != 0 ? (uint32_t)-2 : size;
                }
                case FS_GETCWD: {
                    if (!sys_user_buffer_ok(a1, a2, 1)) return (uint32_t)-1;
                    char cwd[SYSFS_PATH_MAX];
                    int length = fat32_get_cwd(cwd, sizeof(cwd));
                    if (length < 0 || (uint32_t)length + 1U > a2) {
                        return (uint32_t)-1;
                    }
                    memcpy((void *)a1, cwd, (uint32_t)length + 1U);
                    return (uint32_t)length;
                }
                case FS_CHDIR: {
                    char path[SYSFS_PATH_MAX];
                    if (sysfs_resolve_path(a1, path, sizeof(path)) != 0 ||
                        fat32_list_directory(path, 0) != 0 ||
                        fat32_set_cwd(path) != 0) {
                        return (uint32_t)-1;
                    }
                    return 0;
                }
                case FS_RENAME: {
                    char old_path[SYSFS_PATH_MAX];
                    char new_path[SYSFS_PATH_MAX];
                    if (sysfs_resolve_path(a1, old_path, sizeof(old_path)) != 0 ||
                        sysfs_resolve_path(a2, new_path, sizeof(new_path)) != 0) {
                        return (uint32_t)-1;
                    }
                    return fat32_move(old_path, new_path) == 0 ? 0U : (uint32_t)-1;
                }
                case FS_READDIR: {
                    if (!sys_user_buffer_ok(a2, a3, 1)) return (uint32_t)-1;
                    char path[SYSFS_PATH_MAX];
                    if (sysfs_resolve_path(a1, path, sizeof(path)) != 0) {
                        return (uint32_t)-1;
                    }
                    sysfs_readdir_ctx.buf = (char *)kmalloc(4096);
                    if (sysfs_readdir_ctx.buf == 0) return (uint32_t)-1;
                    sysfs_readdir_ctx.len = 0;
                    sysfs_readdir_ctx.cap = 4096;
                    int rc = fat32_list_directory(path, sysfs_readdir_cb);
                    if (rc == 0) {
                        uint32_t copy = sysfs_readdir_ctx.len < a3
                                        ? sysfs_readdir_ctx.len : a3;
                        if (copy > 0) {
                            memcpy((void *)a2, sysfs_readdir_ctx.buf,
                                   copy);
                        }
                        rc = (int)copy;
                    } else {
                        rc = -1;
                    }
                    kfree(sysfs_readdir_ctx.buf);
                    return rc < 0 ? (uint32_t)-1 : (uint32_t)rc;
                }
                default:
                    return 0;
            }
        case SYS_SIG:
            switch (op) {
                case SIG_KILL:
                    if (ring3_signal_send(a1, a2) != 0) {
                        return (uint32_t)-1;
                    }
                    /* A self-directed signal must take effect inside this
                     * syscall: otherwise `kill <self>` returns to user mode
                     * and the caller keeps running (printing a stray prompt)
                     * until its next blocking wait notices the pending
                     * fatal signal. */
                    if (a1 == ring3_current_pid() &&
                        sys_signal_check(frame)) {
                        return 0;
                    }
                    return 0;
                case SIG_SIGNAL:
                    return ring3_signal_set_handler(a1, a2) == 0 ? 0 : (uint32_t)-1;
                case SIG_RETURN:
                    return ring3_signal_sigreturn(frame);
                default:
                    return 0;
            }
        default:
            return -1;
    }
}

uint32_t sys_call(uint32_t klass, uint32_t op,
                  uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t res = 0;

    __asm__ volatile (
        "int $0x80"
        : "=a"(res)
        : "a"(klass), "b"(op), "c"(arg1), "d"(arg2), "S"(arg3)
        : "memory"
    );

    return res;
}

void *sys_malloc(uint32_t size) {
    return (void *)sys_call(SYS_MEMORY, MEMORY_MALLOC, size, 0, 0);
}

void *sys_calloc(uint32_t nmemb, uint32_t size) {
    return (void *)sys_call(SYS_MEMORY, MEMORY_CALLOC, nmemb, size, 0);
}

void *sys_realloc(void *ptr, uint32_t size) {
    return (void *)sys_call(SYS_MEMORY, MEMORY_REALLOC, (uint32_t)ptr, size, 0);
}

void sys_free(void *ptr) {
    sys_call(SYS_MEMORY, MEMORY_FREE, (uint32_t)ptr, 0, 0);
}

void sys_shutdown(void) {
    sys_call(SYS_POWER, PWR_SHUTDOWN, 0, 0, 0);
}

void sys_reboot(void) {
    sys_call(SYS_POWER, PWR_REBOOT, 0, 0, 0);
}

uint32_t sys_getpid(void) {
    return sys_call(SYS_PROCESS, PROC_GETPID, 0, 0, 0);
}