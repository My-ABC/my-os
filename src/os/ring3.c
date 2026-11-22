#include "ring3.h"
#include "signal.h"
#include "paging.h"
#include "pmm.h"
#include "tss.h"
#include "string.h"
#include "serial.h"

#define RING3_KERNEL_STACK_SIZE 8192U
#define USER_DATA_SELECTOR      0x23U
#define USER_CODE_SELECTOR      0x1BU
#define USER_STACK_SIZE         0x8000U
#define USER_STACK_TOP_0        0x00810000U
#define USER_STACK_TOP_1        0x00820000U
/* Fixed user page holding the signal restorer stub (mov eax,SYS_SIGRETURN;
 * int 0x80). Shared by all processes because they share one address space. */
#define USER_SIG_TRAMPOLINE     0x00900000U

struct ring3_process {
    uint32_t pid;
    uint32_t state;
    uint32_t user_stack_top;
    uint32_t image_start;
    uint32_t image_end;
    struct user_mm *mm;
    uint32_t sig_pending;
    uint32_t sig_blocked;
    uint32_t sig_handler[NSIG];
    uint32_t sig_active;            /* signal number whose handler is running, 0 = none */
    struct irq_frame sig_saved_frame; /* interrupted context restored by sigreturn */
    struct irq_frame *frame;
};

static struct ring3_process processes[RING3_PROCESS_COUNT];
static uint8_t user_kernel_stacks[RING3_PROCESS_COUNT][RING3_KERNEL_STACK_SIZE]
    __attribute__((aligned(16)));
static struct irq_frame *kernel_frame;
static int32_t current_process = -1;
static uint32_t next_pid = 1U;
static uint32_t next_process;
static uint32_t foreground_pid;

extern void irq_handler(uint32_t int_no);
extern char stack_top[];

void ring0_to_ring3(uint32_t *ustack_top, void (*func)(void)) {
    __asm__ volatile (
        "cli\n"
        "pushl $0x23\n"
        "pushl %0\n"
        "pushl $0x202\n"
        "pushl $0x1B\n"
        "pushl %1\n"
        "iret\n"
        :
        : "r"(ustack_top), "r"(func)
        : "memory"
    );
}

static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & 0x200U) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

static void release_process_resources(struct ring3_process *process) {
    /* The mm owns every user page (stack, image, heap, signal pages);
     * releasing it frees them all plus the private page tables. */
    if (process->mm != 0) {
        paging_mm_release(process->mm);
        process->mm = 0;
    }
    process->image_start = 0U;
    process->image_end = 0U;
}

static int find_runnable_process(void) {
    for (uint32_t offset = 0; offset < RING3_PROCESS_COUNT; ++offset) {
        uint32_t index = (next_process + offset) % RING3_PROCESS_COUNT;
        if (processes[index].state == RING3_PROCESS_READY) {
            next_process = (index + 1U) % RING3_PROCESS_COUNT;
            return (int)index;
        }
    }
    return -1;
}

/* Write the sigreturn restorer stub into a freshly allocated physical
 * page through the KMAP window, then map it into the process address
 * space. Runs at schedule time so every process owns its trampoline. */
static int mm_install_sig_trampoline(struct user_mm *mm) {
    uint64_t physical = pmm_alloc_page();
    if (physical == 0U) return 0;
    volatile uint8_t *code =
        (volatile uint8_t *)paging_map_physical(physical);
    if (code == 0) {
        pmm_free_page(physical);
        return 0;
    }
    /* mov eax, SYS_SIGRETURN ; int 0x80 */
    code[0] = 0xB8U;
    code[1] = 24U;
    code[2] = 0U;
    code[3] = 0U;
    code[4] = 0U;
    code[5] = 0xCDU;
    code[6] = 0x80U;
    for (uint32_t i = 7; i < PAGE_SIZE; ++i) code[i] = 0x90U;
    paging_unmap_physical();
    paging_mm_map(mm, USER_SIG_TRAMPOLINE, physical,
                  PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (paging_mm_get_phys(mm, USER_SIG_TRAMPOLINE) == 0U) {
        pmm_free_page(physical);
        return 0;
    }
    return 1;
}

static int ring3_schedule_internal(void (*func)(void),
                                  struct user_mm *mm,
                                  uint32_t image_start, uint32_t image_end,
                                  int foreground) {
    uint32_t flags = irq_save();
    uint32_t i;

    if ((image_start == 0U) != (image_end == 0U) ||
        (image_start != 0U &&
         (image_start >= image_end ||
          (image_start & (PAGE_SIZE - 1U)) != 0U ||
          (image_end & (PAGE_SIZE - 1U)) != 0U ||
          image_end > USER_SPACE_END ||
          (image_start < USER_STACK_TOP_0 &&
           image_end > USER_STACK_TOP_0 - USER_STACK_SIZE) ||
          (image_start < USER_STACK_TOP_1 &&
           image_end > USER_STACK_TOP_1 - USER_STACK_SIZE)))) {
        irq_restore(flags);
        return -1;
    }

    for (i = 0; i < RING3_PROCESS_COUNT; ++i) {
        if (processes[i].state == RING3_PROCESS_UNUSED ||
            processes[i].state == RING3_PROCESS_DEAD) {
            break;
        }
    }
    if (i == RING3_PROCESS_COUNT) {
        irq_restore(flags);
        return -1;
    }

    if (mm == 0) {
        mm = paging_mm_create();
        if (mm == 0) {
            irq_restore(flags);
            return -1;
        }
    }

    uint32_t stack_top = (i == 0U) ? USER_STACK_TOP_0 : USER_STACK_TOP_1;
    uint64_t physical = pmm_alloc_page();
    if (physical == 0U) {
        if (mm != 0) paging_mm_release(mm);
        irq_restore(flags);
        return -1;
    }
    paging_mm_map(mm, stack_top - PAGE_SIZE, physical,
                  PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (paging_mm_get_phys(mm, stack_top - PAGE_SIZE) == 0U) {
        pmm_free_page(physical);
        if (mm != 0) paging_mm_release(mm);
        irq_restore(flags);
        return -1;
    }
    if (!mm_install_sig_trampoline(mm)) {
        paging_mm_release(mm);
        irq_restore(flags);
        return -1;
    }

    struct irq_frame *frame =
        (struct irq_frame *)(user_kernel_stacks[i]
                             + RING3_KERNEL_STACK_SIZE
                             - sizeof(struct irq_frame));
    frame->gs = USER_DATA_SELECTOR;
    frame->fs = USER_DATA_SELECTOR;
    frame->es = USER_DATA_SELECTOR;
    frame->ds = USER_DATA_SELECTOR;
    frame->edi = 0;
    frame->esi = 0;
    frame->ebp = 0;
    frame->esp = 0;
    frame->ebx = 0;
    frame->edx = 0;
    frame->ecx = 0;
    frame->eax = 0;
    frame->int_no = 32;
    frame->err_code = 0;
    frame->eip = (uint32_t)func;
    frame->cs = USER_CODE_SELECTOR;
    frame->eflags = 0x202U;
    frame->useresp = stack_top;
    frame->ss = USER_DATA_SELECTOR;

    processes[i].pid = next_pid++;
    if (next_pid == 0U) next_pid = 1U;
    processes[i].state = RING3_PROCESS_READY;
    processes[i].user_stack_top = stack_top;
    processes[i].image_start = image_start;
    processes[i].image_end = image_end;
    processes[i].mm = mm;
    processes[i].sig_pending = 0U;
    processes[i].sig_blocked = 0U;
    for (uint32_t s = 0; s < NSIG; ++s) {
        processes[i].sig_handler[s] = SIG_DFL;
    }
    processes[i].sig_active = 0U;
    processes[i].frame = frame;
    if (foreground) foreground_pid = processes[i].pid;

    next_process = i;
    irq_restore(flags);
    return 0;
}

int ring3_schedule(void (*func)(void)) {
    return ring3_schedule_mm(func, 0, 0U, 0U, 0);
}

int ring3_schedule_mm(void (*func)(void), struct user_mm *mm,
                      uint32_t image_start, uint32_t image_end,
                      int foreground) {
    return ring3_schedule_internal(func, mm, image_start, image_end,
                                   foreground);
}

struct irq_frame *ring3_timer_switch(struct irq_frame *frame) {
    irq_handler(32);
    ring3_signal_scan(frame);

    if (current_process >= 0 && (frame->cs & 3U) != 3U) {
        return frame;
    }

    if (current_process >= 0) {
        struct ring3_process *process = &processes[(uint32_t)current_process];
        if (process->state != RING3_PROCESS_DEAD) {
            process->frame = frame;
            process->state = RING3_PROCESS_READY;
        } else {
            /* Release on the kernel page directory: the dying process's
             * CR3 may lack mappings the teardown path needs. */
            paging_switch_cr3(paging_kernel_cr3());
            release_process_resources(process);
        }
        current_process = -1;
        tss_set_kernel_stack((uint32_t)stack_top);
        paging_switch_cr3(paging_kernel_cr3());
        return kernel_frame;
    }

    kernel_frame = frame;
    int next = find_runnable_process();
    if (next < 0) {
        tss_set_kernel_stack((uint32_t)stack_top);
        return frame;
    }

    current_process = next;
    processes[next].state = RING3_PROCESS_RUNNING;
    tss_set_kernel_stack((uint32_t)user_kernel_stacks[next]
                         + RING3_KERNEL_STACK_SIZE);
    if (processes[next].mm != 0) {
        paging_switch_cr3(processes[next].mm->pdpt_phys);
    }
    return processes[next].frame;
}

struct irq_frame *ring3_yield_current(struct irq_frame *frame) {
    uint32_t flags = irq_save();
    if (current_process < 0 || kernel_frame == 0 ||
        (frame->cs & 3U) != 3U) {
        irq_restore(flags);
        return 0;
    }

    struct ring3_process *process = &processes[(uint32_t)current_process];
    process->frame = frame;
    process->state = RING3_PROCESS_READY;
    current_process = -1;
    tss_set_kernel_stack((uint32_t)stack_top);
    paging_switch_cr3(paging_kernel_cr3());
    /* Keep IRQs disabled until the assembly stub switches back to the shell. */
    return kernel_frame;
}

struct user_mm *ring3_current_mm(void) {
    if (current_process < 0) return 0;
    return processes[(uint32_t)current_process].mm;
}

uint32_t ring3_current_pid(void) {
    if (current_process < 0) return 0U;
    return processes[(uint32_t)current_process].pid;
}

uint32_t ring3_foreground_pid(void) {
    return foreground_pid;
}

uint32_t ring3_process_list(uint32_t *pids, uint32_t *states,
                            uint32_t capacity) {
    uint32_t flags = irq_save();
    uint32_t count = capacity < RING3_PROCESS_COUNT
                   ? capacity : RING3_PROCESS_COUNT;
    for (uint32_t i = 0; i < count; ++i) {
        pids[i] = processes[i].pid;
        states[i] = processes[i].state;
    }
    irq_restore(flags);
    return count;
}

int ring3_set_foreground(uint32_t pid) {
    uint32_t flags = irq_save();
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        if (processes[i].pid == pid &&
            (processes[i].state == RING3_PROCESS_READY ||
             processes[i].state == RING3_PROCESS_RUNNING)) {
            foreground_pid = pid;
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int ring3_set_background(uint32_t pid) {
    uint32_t flags = irq_save();
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        if (processes[i].pid == pid &&
            (processes[i].state == RING3_PROCESS_READY ||
             processes[i].state == RING3_PROCESS_RUNNING)) {
            if (foreground_pid == pid) foreground_pid = 0U;
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int ring3_user_range_permitted(uint32_t addr) {
    /* A page fault can only belong to the currently running process now
     * that each process owns a private address space. */
    if (current_process < 0) return 0;
    struct ring3_process *process = &processes[(uint32_t)current_process];
    if (process->state != RING3_PROCESS_READY &&
        process->state != RING3_PROCESS_RUNNING) {
        return 0;
    }
    if (process->image_start != 0U &&
        addr >= process->image_start && addr < process->image_end) {
        return 1;
    }
    if (addr >= process->user_stack_top - USER_STACK_SIZE &&
        addr < process->user_stack_top) {
        return 1;
    }
    return 0;
}

struct irq_frame *ring3_terminate_current(void) {
    uint32_t flags = irq_save();
    if (current_process < 0) {
        irq_restore(flags);
        return 0;
    }
    struct ring3_process *process = &processes[(uint32_t)current_process];
    uint32_t pid = process->pid;
    process->state = RING3_PROCESS_DEAD;
    if (foreground_pid == pid) foreground_pid = 0U;
    /* Tear down on the kernel page directory before releasing the mm. */
    paging_switch_cr3(paging_kernel_cr3());
    release_process_resources(process);
    current_process = -1;
    tss_set_kernel_stack((uint32_t)stack_top);
    struct irq_frame *frame = kernel_frame;
    kernel_frame = 0;
    /* The exception/syscall stub switches stacks immediately after this. */
    return frame;
}

int ring3_kill(uint32_t pid) {
    uint32_t flags = irq_save();
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        if (processes[i].pid == pid &&
            (processes[i].state == RING3_PROCESS_READY ||
             processes[i].state == RING3_PROCESS_RUNNING)) {
            processes[i].state = RING3_PROCESS_DEAD;
            if (foreground_pid == pid) foreground_pid = 0U;
            if (current_process != (int32_t)i) {
                release_process_resources(&processes[i]);
            }
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

/* ---- signals ---- */

static int sig_fatal_default(uint32_t sig) {
    return !SIG_DEFAULT_IGNORE(sig);
}

/* Classify pending & ~blocked signals: clears ignored bits, returns the
 * fatal-default mask, and reports the lowest-numbered signal that has a
 * custom handler (0 when none). */
static uint32_t sig_actionable(struct ring3_process *p, uint32_t *handled) {
    uint32_t mask = p->sig_pending & ~p->sig_blocked;
    uint32_t fatal = 0;
    *handled = 0;
    for (uint32_t s = 1; s < NSIG; ++s) {
        uint32_t bit = 1U << s;
        if ((mask & bit) == 0U) continue;
        if (p->sig_handler[s] == SIG_IGN) {
            p->sig_pending &= ~bit;
        } else if (p->sig_handler[s] == SIG_DFL) {
            if (sig_fatal_default(s)) {
                fatal |= bit;
            } else {
                p->sig_pending &= ~bit;
            }
        } else if (*handled == 0U) {
            *handled = s;
        }
    }
    return fatal;
}

/* Map a fresh zeroed user page into `mm`. The page is cleared through
 * the KMAP window so this works for processes that are not running. */
static int sig_ensure_user_page(struct user_mm *mm, uint32_t addr) {
    uint32_t page = addr & ~(PAGE_SIZE - 1U);
    if (paging_mm_get_phys(mm, page) != 0U) return 1;
    uint64_t physical = pmm_alloc_page();
    if (physical == 0U) return 0;
    volatile uint8_t *zero =
        (volatile uint8_t *)paging_map_physical(physical);
    if (zero == 0) {
        pmm_free_page(physical);
        return 0;
    }
    memset((void *)zero, 0, PAGE_SIZE);
    paging_unmap_physical();
    paging_mm_map(mm, page, physical,
                  PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (paging_mm_get_phys(mm, page) == 0U) {
        pmm_free_page(physical);
        return 0;
    }
    return 1;
}

/* Redirect `frame` so the process resumes inside its handler for `sig`.
 * The interrupted context is kept in sig_saved_frame and restored by
 * SYS_SIGRETURN; the handler sees a normal cdecl frame
 * ([esp]=restorer, [esp+4]=sig) on the user stack. */
static int sig_deliver(struct ring3_process *p, uint32_t sig,
                       struct irq_frame *frame) {
    if ((frame->cs & 3U) != 3U || p->sig_active != 0U ||
        p->mm == 0) {
        return 0;
    }
    uint32_t sp = (frame->useresp - 16U) & ~0xFU;
    if (!sig_ensure_user_page(p->mm, sp) ||
        !sig_ensure_user_page(p->mm, sp + 4U)) {
        return 0;
    }

    p->sig_saved_frame = *frame;
    p->sig_active = sig;
    p->sig_blocked |= 1U << sig;
    p->sig_pending &= ~(1U << sig);

    /* The user stack lives in the target process's private address
     * space; switch CR3 briefly to write the call frame. */
    uint32_t saved_cr3 = paging_switch_cr3(p->mm->pdpt_phys);
    volatile uint32_t *stack = (volatile uint32_t *)sp;
    stack[0] = USER_SIG_TRAMPOLINE;
    stack[1] = sig;
    paging_switch_cr3(saved_cr3);

    frame->eip = p->sig_handler[sig];
    frame->useresp = sp;
    serial_print("[SIG] Deliver sig ");
    serial_print_dec(sig);
    serial_print(" to pid ");
    serial_print_dec(p->pid);
    serial_print(" handler=");
    serial_print_hex(p->sig_handler[sig]);
    serial_putchar('\n');
    return 1;
}

int ring3_signal_send(uint32_t pid, uint32_t sig) {
    if (!SIG_VALID(sig)) return -1;
    uint32_t flags = irq_save();
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        if (processes[i].pid == pid &&
            (processes[i].state == RING3_PROCESS_READY ||
             processes[i].state == RING3_PROCESS_RUNNING)) {
            processes[i].sig_pending |= 1U << sig;
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int ring3_signal_set_handler(uint32_t sig, uint32_t handler) {
    if (!SIG_VALID(sig) || SIG_UNCATCHABLE(sig)) return -1;
    if (handler > SIG_IGN && handler >= USER_SPACE_END) return -1;
    if (current_process < 0) return -1;
    uint32_t flags = irq_save();
    struct ring3_process *p = &processes[(uint32_t)current_process];
    p->sig_handler[sig] = handler;
    irq_restore(flags);
    return 0;
}

/* 1 if the current process has a fatal signal pending that is not
 * blocked and not ignored. Blocking syscalls poll this to abort. */
uint32_t ring3_signal_pending_fatal(void) {
    if (current_process < 0) return 0U;
    struct ring3_process *p = &processes[(uint32_t)current_process];
    uint32_t handled;
    return sig_actionable(p, &handled) != 0U ? 1U : 0U;
}

/* Deliver a pending handled signal into the current process's live
 * frame. Returns 1 when `frame` was redirected to the handler, which
 * abandons the in-flight syscall the same way an interrupted call would. */
int ring3_signal_deliver_current(struct irq_frame *frame) {
    if (current_process < 0) return 0;
    struct ring3_process *p = &processes[(uint32_t)current_process];
    uint32_t handled = 0U;
    if (sig_actionable(p, &handled) != 0U || handled == 0U) return 0;
    return sig_deliver(p, handled, frame);
}

/* SYS_SIGRETURN: restore the context saved when the running handler was
 * delivered. Returns the restored eax so the syscall epilogue preserves
 * the interrupted context's value. */
uint32_t ring3_signal_sigreturn(struct irq_frame *frame) {
    if (current_process < 0) return (uint32_t)-1;
    struct ring3_process *p = &processes[(uint32_t)current_process];
    if (p->sig_active == 0U) return (uint32_t)-1;
    uint32_t sig = p->sig_active;
    struct irq_frame saved = p->sig_saved_frame;
    p->sig_active = 0U;
    p->sig_blocked &= ~(1U << sig);
    *frame = saved;
    serial_print("[SIG] sigreturn pid ");
    serial_print_dec(p->pid);
    serial_print(" sig ");
    serial_print_dec(sig);
    serial_putchar('\n');
    return saved.eax;
}

/* Called from IRQ0 before context selection. Marks processes with a
 * fatal unblocked pending signal DEAD and redirects processes with a
 * pending custom-handler signal into their handler. The current process
 * is only touched when the interrupted frame is a user frame; a kernel
 * frame means the process is inside a syscall, whose wait loop calls
 * ring3_signal_deliver_current itself. */
void ring3_signal_scan(struct irq_frame *frame) {
    int current_kernel_frame =
        current_process >= 0 && (frame->cs & 3U) != 3U;
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        struct ring3_process *p = &processes[i];
        if (p->state != RING3_PROCESS_READY &&
            p->state != RING3_PROCESS_RUNNING) {
            continue;
        }
        uint32_t handled = 0U;
        uint32_t fatal = sig_actionable(p, &handled);
        int is_current = (int32_t)i == current_process;
        if (is_current && current_kernel_frame) continue;
        if (fatal != 0U) {
            p->state = RING3_PROCESS_DEAD;
            if (foreground_pid == p->pid) foreground_pid = 0U;
            if (!is_current) {
                release_process_resources(p);
            }
            continue;
        }
        if (handled == 0U) continue;
        struct irq_frame *target = is_current ? frame : p->frame;
        if (target != 0 && sig_deliver(p, handled, target)) {
            /* frame redirected; nothing else to do this tick */
        }
    }
}
