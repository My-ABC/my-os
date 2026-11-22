/* SPDX-License-Identifier: MIT */
#include <process.h>
#include <sys/syscall.h>

int proc_list(proc_info_t *buf, int max) {
    if (buf == 0 || max <= 0) return -1;
    return (int)syscall2(SYS_PROCESS, PROC_LIST, (uint32_t)buf,
                         (uint32_t)(max * (int)sizeof(proc_info_t)));
}

int proc_set_foreground(pid_t pid) {
    return syscall1(SYS_PROCESS, PROC_SET_FG, (uint32_t)pid) == 0
           ? 0 : -1;
}

pid_t proc_foreground(void) {
    proc_info_t buf[16];
    int n = proc_list(buf, 16);
    for (int i = 0; i < n; ++i)
        if (buf[i].foreground) return buf[i].pid;
    return -1;
}

int proc_run_demo(void) {
    return syscall0(SYS_PROCESS, PROC_RUN_DEMO) == 0 ? 0 : -1;
}

int proc_run_usercode(void) {
    return syscall0(SYS_PROCESS, PROC_RUN_USERCODE) == 0 ? 0 : -1;
}
