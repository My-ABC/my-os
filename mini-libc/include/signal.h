/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_SIGNAL_H
#define _MINI_LIBC_SIGNAL_H

#include <sys/types.h>

/* mini-libc signal interface over the SYS_SIG ABI.
 *
 * The kernel implements exactly two signals: SIGINT (catchable,
 * delivered on Ctrl+C) and SIGKILL (uncatchable, always fatal).
 * There is no sigprocmask/sigaction mask support — signal() is the
 * whole interface.
 *
 * A caught handler runs on the user stack with a cdecl frame
 * ([esp]=kernel restorer, [esp+4]=sig), so a plain
 * `void handler(int sig)` works; returning from it resumes the
 * interrupted context automatically.
 */

typedef int sig_atomic_t;
typedef void (*sig_t)(int);

#define SIG_ERR ((sig_t)-1)
#define SIG_DFL ((sig_t)0)
#define SIG_IGN ((sig_t)1)

#define SIGINT  2
#define SIGKILL 9
#define NSIG    32

sig_t signal(int sig, sig_t func);
int kill(pid_t pid, int sig);
int raise(int sig);

#endif /* _MINI_LIBC_SIGNAL_H */
