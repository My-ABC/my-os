#ifndef _RING3_H
#define _RING3_H

#include "stdint.h"
#include "regs.h"

extern void user_entry(void);

void ring0_to_ring3(uint32_t *ustack_top, void (*func)(void));
int ring3_schedule(void (*func)(void));
int ring3_schedule_foreground(void (*func)(void),
                             uint32_t image_start, uint32_t image_end);
struct irq_frame *ring3_timer_switch(struct irq_frame *frame);
uint32_t ring3_current_pid(void);
uint32_t ring3_foreground_pid(void);
uint32_t ring3_process_list(uint32_t *pids, uint32_t *states, uint32_t capacity);
int ring3_set_foreground(uint32_t pid);
int ring3_set_background(uint32_t pid);
int ring3_kill(uint32_t pid);
int ring3_user_range_permitted(uint32_t addr);
struct irq_frame *ring3_terminate_current(void);

#define RING3_PROCESS_UNUSED 0U
#define RING3_PROCESS_READY  1U
#define RING3_PROCESS_RUNNING 2U
#define RING3_PROCESS_DEAD   3U
#define RING3_PROCESS_COUNT  2U

#endif