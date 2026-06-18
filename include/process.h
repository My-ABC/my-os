#ifndef _PROCESS_H
#define _PROCESS_H

#include "stdint.h"

#define PROC_MAX       16
#define PROC_RUNNING   1
#define PROC_READY     2
#define PROC_BLOCKED   3
#define PROC_TERMINATED 4

struct process {
    uint32_t pid;
    uint32_t state;
    void (*entry)(void);
    char name[32];
    uint32_t stack;
    struct process* next;
};

void process_init(void);
int process_create(void (*entry)(void), const char* name);
void process_schedule(void);
void process_list(void);
struct process* process_get_current(void);
void process_yield(void);
void process_exit(int code);
int process_kill(int pid);
void process_cleanup(void);
const char* process_get_name(int pid);

#endif