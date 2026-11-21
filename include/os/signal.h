#ifndef _SIGNAL_H
#define _SIGNAL_H

#include "stdint.h"

/* Implemented signal numbers; other values are reserved */
#define SIGINT    2
#define SIGKILL   9
#define SIG_VALID(sig) ((sig) == SIGINT || (sig) == SIGKILL)

#define NSIG      32

#define SIG_DFL   0U   /* default action: terminate (or ignore for a few) */
#define SIG_IGN   1U   /* ignore */

/* Signals that cannot be caught, blocked, or ignored */
#define SIG_UNCATCHABLE(sig) ((sig) == SIGKILL)

/* Signals whose default action is to be ignored (none yet) */
#define SIG_DEFAULT_IGNORE(sig) (0)

/* ring3.c signal API (implemented there because it owns process state) */
int  ring3_signal_send(uint32_t pid, uint32_t sig);
int  ring3_signal_set_handler(uint32_t sig, uint32_t handler);
uint32_t ring3_signal_pending_fatal(void);
void ring3_signal_scan(struct irq_frame *frame);

#endif
