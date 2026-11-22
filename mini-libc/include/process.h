/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_PROCESS_H
#define _MINI_LIBC_PROCESS_H

#include <sys/types.h>

/* mini-libc process enumeration and foreground control over the
 * SYS_PROCESS ABI (PROC_LIST / PROC_SET_FG).
 *
 * The kernel has no fork/wait — each `run` spawns an independent
 * Ring 3 process; this header only exposes what the ABI offers. */

enum {
    PROC_ST_UNUSED  = 0,
    PROC_ST_READY   = 1,
    PROC_ST_RUNNING = 2,
    PROC_ST_DEAD    = 3
};

typedef struct {
    pid_t pid;
    int   state;        /* PROC_ST_* */
    int   foreground;   /* nonzero if this is the foreground process */
} proc_info_t;

/* fill buf with up to max process records; returns count or <0.
 * The kernel writes 12-byte {pid,state,fg} records — proc_info_t
 * is layout-compatible. */
int proc_list(proc_info_t *buf, int max);

/* make pid the foreground process (its console output is shown);
 * pid 0 backgrounds the current foreground process instead.
 * Returns 0 or <0. */
int proc_set_foreground(pid_t pid);

/* pid of the current foreground process, or -1 if none/unknown */
pid_t proc_foreground(void);

/* run the kernel's built-in ELF32 demo image in the foreground
 * (equivalent of the shell `elf` command). Returns 0 or <0. */
int proc_run_demo(void);

/* start the kernel's built-in Ring 3 user-code demo in the
 * background (equivalent of the shell `ring3` command).
 * Returns 0 or <0. */
int proc_run_usercode(void);

#endif /* _MINI_LIBC_PROCESS_H */
