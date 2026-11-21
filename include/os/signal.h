#ifndef _SIGNAL_H
#define _SIGNAL_H

#include "stdint.h"

/* Linux-style signal numbers */
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPWR    30
#define SIGSYS    31

#define NSIG      32

#define SIG_DFL   0U   /* default action: terminate (or ignore for a few) */
#define SIG_IGN   1U   /* ignore */

/* Signals that cannot be caught, blocked, or ignored */
#define SIG_UNCATCHABLE(sig) ((sig) == SIGKILL || (sig) == SIGSTOP)

/* Signals whose default action is to be ignored */
#define SIG_DEFAULT_IGNORE(sig) \
    ((sig) == SIGCHLD || (sig) == SIGURG || (sig) == SIGWINCH || (sig) == SIGIO)

/* ring3.c signal API (implemented there because it owns process state) */
int  ring3_signal_send(uint32_t pid, uint32_t sig);
int  ring3_signal_set_handler(uint32_t sig, uint32_t handler);
uint32_t ring3_signal_pending_fatal(void);
void ring3_signal_scan(struct irq_frame *frame);

#endif
