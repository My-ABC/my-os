#ifndef _RING3_H
#define _RING3_H

#include "stdint.h"

void ring0_to_ring3(uint32_t *ustack_top, void (*func)(void));

#endif