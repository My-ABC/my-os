#ifndef _RING3_H
#define _RING3_H

#include "stdint.h"

extern void user_entry(void);

void ring0_to_ring3(uint32_t *ustack_top, void (*func)(void));

#endif