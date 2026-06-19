#include "process.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "memory.h"

static struct process processes[PROC_MAX];
static int process_count = 0;
static int current_pid = -1;
static int next_pid = 1;

void process_init(void) {
    print_info("Process manager initialized\n");
    
    // 创建内核进程 (PID=1)
    struct process* p = &processes[process_count++];
    p->pid = 1;
    p->state = PROC_RUNNING;
    p->entry = NULL;
    strcpy(p->name, "kernel");
    p->stack = 0;
    p->next = NULL;
    
    current_pid = 1;
    next_pid = 2;
}

int process_create(void (*entry)(void), const char* name) {
    if (process_count >= PROC_MAX) return -1;
    
    struct process* p = &processes[process_count++];
    p->pid = next_pid++;
    p->state = PROC_READY;
    p->entry = entry;
    strcpy(p->name, name);
    p->stack = 0;
    p->next = NULL;
    
    // 如果当前没有运行进程，让这个进程运行
    if (current_pid == -1) {
        current_pid = p->pid;
        p->state = PROC_RUNNING;
    }
    
    printf("Process created: %s (PID=%d)\n", name, p->pid);
    return p->pid;
}

void process_exit(int code) {
    printf("Process %d exited with code %d\n", current_pid, code);
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == current_pid) {
            processes[i].state = PROC_TERMINATED;
            break;
        }
    }
}

void process_cleanup(void) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i].state == PROC_TERMINATED) {
            processes[i] = processes[process_count - 1];
            process_count--;
            i--;
        }
    }
}

void process_schedule(void) {
    if (process_count == 0) return;
    
    process_cleanup();
    if (process_count == 0) return;
    
    // 如果当前进程已终止，切换到第一个就绪进程
    struct process* cur = process_get_current();
    if (cur == NULL || cur->state == PROC_TERMINATED) {
        current_pid = -1;
    }
    
    int current_idx = -1;
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == current_pid) {
            current_idx = i;
            break;
        }
    }
    
    if (current_idx >= 0 && processes[current_idx].state == PROC_RUNNING) {
        processes[current_idx].state = PROC_READY;
    }
    
    int next_idx = -1;
    int start = (current_idx + 1) % process_count;
    for (int i = 0; i < process_count; i++) {
        int idx = (start + i) % process_count;
        if (processes[idx].state == PROC_READY) {
            next_idx = idx;
            break;
        }
    }
    
    if (next_idx >= 0) {
        current_pid = processes[next_idx].pid;
        processes[next_idx].state = PROC_RUNNING;
    }
}

void process_yield(void) {
    struct process* cur = process_get_current();
    if (cur && cur->state == PROC_TERMINATED) {
        return;
    }
    process_schedule();
}

struct process* process_get_current(void) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == current_pid) {
            return &processes[i];
        }
    }
    return NULL;
}

const char* process_get_name(int pid) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == pid) {
            return processes[i].name;
        }
    }
    return "unknown";
}

void process_list(void) {
    process_cleanup();
    printf("PID  State  Name\n");
    printf("---  -----  ----\n");
    for (int i = 0; i < process_count; i++) {
        char* state_str;
        switch (processes[i].state) {
            case PROC_RUNNING: state_str = "RUN"; break;
            case PROC_READY: state_str = "RDY"; break;
            case PROC_BLOCKED: state_str = "BLK"; break;
            case PROC_TERMINATED: state_str = "TER"; break;
            default: state_str = "???"; break;
        }
        printf("%3d  %s     %s\n", processes[i].pid, state_str, processes[i].name);
    }
}

int process_kill(int pid) {
    if (pid == 1) {
        return -3;
    }
    
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == pid) {
            // 检查进程状态，而不是 current_pid
            if (processes[i].state == PROC_RUNNING) {
                return -2;  // 正在运行
            }
            processes[i].state = PROC_TERMINATED;
            return 0;
        }
    }
    return -1;
}