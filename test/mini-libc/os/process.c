/* SPDX-License-Identifier: MIT */
#include <process.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    pid_t me = getpid();
    CHK(me > 0, "getpid");

    proc_info_t buf[16];
    int n = proc_list(buf, 16);
    CHK(n > 0, "proc-list-count");

    int saw_self = 0, saw_fg = 0;
    for (int i = 0; i < n; ++i) {
        if (buf[i].pid == me) {
            saw_self = 1;
            CHK(buf[i].state == PROC_ST_RUNNING ||
                buf[i].state == PROC_ST_READY, "self-state");
        }
        if (buf[i].foreground) {
            saw_fg = 1;
            CHK(buf[i].pid == me, "self-is-fg");
        }
    }
    CHK(saw_self, "list-has-self");
    CHK(saw_fg, "list-has-fg");
    printf("  nproc=%d self=%d fg=%d\n", n, me, proc_foreground());
    CHK(proc_foreground() == me, "proc-foreground");

    /* foreground control: setting self keeps working; bogus pid fails */
    CHK(proc_set_foreground(me) == 0, "set-fg-self");
    CHK(proc_set_foreground(9999) != 0, "set-fg-bogus");
    CHK(proc_set_foreground(0) == 0, "bg-self");   /* backgrounds us */
    /* we are background now: still alive, just quieter */
    proc_set_foreground(me);                      /* restore fg */

    /* truncated buffer must fail cleanly */
    proc_info_t tiny[1];
    CHK(proc_list(tiny, 0) < 0, "list-max0");

    printf("DONE-process fails=%d\n", fails);
    exit(fails);
}
