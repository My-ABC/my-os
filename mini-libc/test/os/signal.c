/* SPDX-License-Identifier: MIT */
/* Signal test: installs a SIGINT handler, prints ARMED, then yields
 * until Ctrl+C (sent by the runner via the QEMU monitor) delivers
 * SIGINT. The handler prints HANDLED with the delivered sig number;
 * after sigreturn the main loop verifies a clobbered register was
 * restored and prints DONE-signal fails=N. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails;
static volatile int handled;
static volatile int got_sig;

static void on_sigint(int sig) {
    handled = 1;
    got_sig = sig;
    printf("ok sigint-handled sig=%d\n", sig);
    /* clobber a caller-saved register; sigreturn must restore it */
    __asm__ volatile("movl $0xDEADBEEF, %%ecx" ::: "ecx");
}

int main(void) {
    sig_t prev = signal(SIGINT, on_sigint);
    if (prev != SIG_DFL) { printf("FAIL signal-prev\n"); fails++; }

    /* invalid registrations must fail */
    if (signal(SIGKILL, on_sigint) != SIG_ERR) {
        printf("FAIL sigkill-catchable\n"); fails++;
    }
    if (signal(99, on_sigint) != SIG_ERR) {
        printf("FAIL sig99\n"); fails++;
    }
    if (signal(SIGINT, SIG_IGN) != on_sigint) {
        printf("FAIL signal-return\n"); fails++;
    }
    signal(SIGINT, on_sigint);   /* restore */

    /* raise() delivers to self: handler runs at next scheduling point */
    handled = 0;
    if (raise(SIGINT) == 0) {
        for (int i = 0; i < 100000 && !handled; ++i)
            sched_yield();
    }
    if (!handled || got_sig != SIGINT) {
        printf("FAIL raise-deliver\n"); fails++;
    } else {
        printf("ok raise-deliver\n");
    }

    /* kill(getpid(), SIGINT) path */
    handled = 0;
    if (kill(getpid(), SIGINT) == 0) {
        for (int i = 0; i < 100000 && !handled; ++i)
            sched_yield();
    }
    if (!handled) { printf("FAIL kill-deliver\n"); fails++; }
    else printf("ok kill-deliver\n");

    printf("DONE-signal fails=%d\n", fails);
    exit(fails);
}
