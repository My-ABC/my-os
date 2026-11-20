#ifndef _PIT_H
#define _PIT_H

#include "stdint.h"

#define PIT_FREQUENCY 100

void pit_init(uint32_t hz);
uint32_t pit_ticks(void);
void pit_tick(void);

#endif
