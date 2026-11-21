#ifndef _TRAP_H
#define _TRAP_H

#include "regs.h"

void isr3_handler(struct registers* regs);
/* 返回 0 表示正常 iret; 返回非 0 时是要切换到的内核 struct irq_frame*。 */
uint32_t isr14_handler(struct registers* regs);

#endif
