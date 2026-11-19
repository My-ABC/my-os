#include "ring3.h"
#include "paging.h"
#include "pmm.h"
#include "tss.h"

#define RING3_KERNEL_STACK_SIZE 8192U
#define USER_DATA_SELECTOR      0x23U
#define USER_CODE_SELECTOR      0x1BU
#define USER_STACK_TOP_0        0x00802000U
#define USER_STACK_TOP_1        0x00804000U

struct ring3_process {
    uint32_t pid;
    uint32_t state;
    uint32_t user_stack_top;
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
    uint32_t address = stack_top - PAGE_SIZE;
    uint32_t physical = paging_get_page_physical(address);
    if (physical != 0U) {
        paging_unmap_page(address);
        pmm_free_page((void *)(physical & ~(PAGE_SIZE - 1U)));
    }
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

int ring3_schedule(void (*func)(void)) {
    uint32_t flags = irq_save();
    uint32_t i;

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
    void *physical = pmm_alloc_page();
    if (physical == 0) {
        irq_restore(flags);
        return -1;
    }
    paging_map_page(stack_top - PAGE_SIZE, (uint32_t)physical,
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
    processes[i].frame = frame;

    next_process = i;
    irq_restore(flags);
    return 0;
}

struct irq_frame *ring3_timer_switch(struct irq_frame *frame) {
    irq_handler(32);

    if (current_process >= 0 && (frame->cs & 3U) != 3U) {
        return frame;
    }

    if (current_process >= 0) {
        struct ring3_process *process = &processes[(uint32_t)current_process];
        if (process->state != RING3_PROCESS_DEAD) {
            process->frame = frame;
            process->state = RING3_PROCESS_READY;
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

int ring3_kill(uint32_t pid) {
    uint32_t flags = irq_save();
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        if (processes[i].pid == pid &&
            (processes[i].state == RING3_PROCESS_READY ||
             processes[i].state == RING3_PROCESS_RUNNING)) {
            processes[i].state = RING3_PROCESS_DEAD;
            if (foreground_pid == pid) foreground_pid = 0U;
            release_user_stack(processes[i].user_stack_top);
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}
