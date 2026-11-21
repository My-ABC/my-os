#include "ring3.h"
#include "signal.h"
#include "paging.h"
#include "pmm.h"
#include "tss.h"

#define RING3_KERNEL_STACK_SIZE 8192U
#define USER_DATA_SELECTOR      0x23U
#define USER_CODE_SELECTOR      0x1BU
#define USER_STACK_SIZE         0x8000U
#define USER_STACK_TOP_0        0x00810000U
#define USER_STACK_TOP_1        0x00820000U

struct ring3_process {
    uint32_t pid;
    uint32_t state;
    uint32_t user_stack_top;
    uint32_t image_start;
    uint32_t image_end;
    uint32_t sig_pending;
    uint32_t sig_blocked;
    uint32_t sig_handler[NSIG];
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

static void release_user_stack(uint32_t stack_top) {
    for (uint32_t address = stack_top - USER_STACK_SIZE;
         address < stack_top; address += PAGE_SIZE) {
        uint64_t physical = paging_get_page_physical(address);
        if (physical != 0U) {
            paging_unmap_page(address);
            pmm_free_page(physical & ~(uint64_t)(PAGE_SIZE - 1U));
        }
    }
}

static void release_process_resources(struct ring3_process *process) {
    release_user_stack(process->user_stack_top);
    for (uint32_t address = process->image_start;
         address < process->image_end; address += PAGE_SIZE) {
        uint64_t physical = paging_get_page_physical(address);
        if (physical != 0U) {
            paging_unmap_page(address);
            pmm_free_page(physical & ~(uint64_t)(PAGE_SIZE - 1U));
        }
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

static int ring3_schedule_internal(void (*func)(void),
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

    uint32_t stack_top = (i == 0U) ? USER_STACK_TOP_0 : USER_STACK_TOP_1;
    uint64_t physical = pmm_alloc_page();
    if (physical == 0U) {
        irq_restore(flags);
        return -1;
    }
    paging_map_page(stack_top - PAGE_SIZE, physical,
                    PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (paging_get_page_physical(stack_top - PAGE_SIZE) == 0U) {
        pmm_free_page(physical);
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
    processes[i].sig_pending = 0U;
    processes[i].sig_blocked = 0U;
    for (uint32_t s = 0; s < NSIG; ++s) {
        processes[i].sig_handler[s] = SIG_DFL;
    }
    processes[i].frame = frame;
    if (foreground) foreground_pid = processes[i].pid;

    next_process = i;
    irq_restore(flags);
    return 0;
}

int ring3_schedule(void (*func)(void)) {
    return ring3_schedule_internal(func, 0U, 0U, 0);
}

int ring3_schedule_foreground(void (*func)(void),
                             uint32_t image_start, uint32_t image_end) {
    return ring3_schedule_internal(func, image_start, image_end, 1);
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
            release_process_resources(process);
        }
        current_process = -1;
        tss_set_kernel_stack((uint32_t)stack_top);
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
    /* Keep IRQs disabled until the assembly stub switches back to the shell. */
    return kernel_frame;
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
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        struct ring3_process *process = &processes[i];
        if (process->state != RING3_PROCESS_READY &&
            process->state != RING3_PROCESS_RUNNING) {
            continue;
        }
        if (process->image_start != 0U &&
            addr >= process->image_start && addr < process->image_end) {
            return 1;
        }
        if (addr >= process->user_stack_top - USER_STACK_SIZE &&
            addr < process->user_stack_top) {
            return 1;
        }
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

/* Custom handlers are stored but user-mode delivery is not implemented
 * yet, so their pending bits stay set; SIG_IGN clears the bit and
 * SIG_DFL-on-ignore-by-default signals are dropped. */
static uint32_t sig_deliverable_mask(struct ring3_process *p) {
    uint32_t mask = p->sig_pending & ~p->sig_blocked;
    uint32_t out = 0;
    for (uint32_t s = 1; s < NSIG; ++s) {
        uint32_t bit = 1U << s;
        if ((mask & bit) == 0U) continue;
        if (p->sig_handler[s] == SIG_IGN) {
            p->sig_pending &= ~bit;
        } else if (p->sig_handler[s] == SIG_DFL &&
                   sig_fatal_default(s)) {
            out |= bit;
        } else if (p->sig_handler[s] > SIG_IGN) {
            /* keep pending until handler delivery exists */
        } else if (!sig_fatal_default(s)) {
            p->sig_pending &= ~bit;
        }
    }
    return out;
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
    return sig_deliverable_mask(p) != 0U ? 1U : 0U;
}

/* Called from IRQ0 before context selection. Marks processes with a
 * fatal unblocked pending signal DEAD. The current process is only
 * marked when the interrupted frame is a user frame; a kernel frame
 * means the process is inside a syscall, whose wait loop checks
 * ring3_signal_pending_fatal itself. */
void ring3_signal_scan(struct irq_frame *frame) {
    int current_kernel_frame =
        current_process >= 0 && (frame->cs & 3U) != 3U;
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        struct ring3_process *p = &processes[i];
        if (p->state != RING3_PROCESS_READY &&
            p->state != RING3_PROCESS_RUNNING) {
            continue;
        }
        if (sig_deliverable_mask(p) == 0U) continue;
        if ((int32_t)i == current_process && current_kernel_frame) continue;
        p->state = RING3_PROCESS_DEAD;
        if (foreground_pid == p->pid) foreground_pid = 0U;
        if ((int32_t)i != current_process) {
            release_process_resources(p);
        }
    }
}
