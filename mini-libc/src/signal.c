/* SPDX-License-Identifier: MIT */
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>

/* Userspace copy of installed handlers so signal() can return the
 * previous disposition like POSIX. Zero-initialised == SIG_DFL. */
static sig_t handlers[NSIG];

sig_t signal(int sig, sig_t func) {
    if (sig <= 0 || sig >= NSIG || func == SIG_ERR)
        return SIG_ERR;
    /* kernel: only SIGINT catchable; SIGKILL always fatal */
    if (sig == SIGKILL || sig != SIGINT)
        return SIG_ERR;

    sig_t old = handlers[sig];
    if (syscall2(SYS_SIG, SIG_SIGNAL, (uint32_t)sig,
                 (uint32_t)func) != 0) {
        errno = EINVAL;
        return SIG_ERR;
    }
    handlers[sig] = func;
    return old;
}

int kill(pid_t pid, int sig) {
    if (sig <= 0 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    int rc = (int)syscall2(SYS_SIG, SIG_KILL, (uint32_t)pid,
                           (uint32_t)sig);
    if (rc != 0) {
        errno = (sig == SIGINT || sig == SIGKILL) ? ESRCH : EINVAL;
        return -1;
    }
    return 0;
}

int raise(int sig) {
    return kill(getpid(), sig);
}
