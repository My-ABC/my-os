#ifndef _RING3_H
#define _RING3_H

#include "stdint.h"
#include "regs.h"
#include "paging.h"

extern void user_entry(void);

void ring0_to_ring3(uint32_t *ustack_top, void (*func)(void));
int ring3_schedule(void (*func)(void));
/* Takes ownership of `mm` on entry: on success the process slot owns it,
 * on any failure it is released. Callers must not free it afterwards. */
int ring3_schedule_mm(void (*func)(void), struct user_mm *mm,
                      uint32_t image_start, uint32_t image_end,
                      int foreground, int vt);
struct user_mm *ring3_current_mm(void);
struct irq_frame *ring3_timer_switch(struct irq_frame *frame);
struct irq_frame *ring3_yield_current(struct irq_frame *frame);
/* Sleep-aware yield: same diversion as ring3_yield_current but keeps
 * the process in RING3_PROCESS_SLEEPING so the scheduler skips it
 * until ring3_timer_switch wakes it at sleep_deadline. */
struct irq_frame *ring3_sleep_yield_current(struct irq_frame *frame);

/* Per-process sleep state used by SYS_TIME/TIME_SLEEP. sleep_deadline
 * is in PIT ticks (pit_ticks()); 0 means "not sleeping". */
uint32_t ring3_sleep_deadline(void);
void ring3_sleep_begin(uint32_t deadline_ticks);
void ring3_sleep_end(void);
uint32_t ring3_current_pid(void);
uint32_t ring3_current_vt(void);
int ring3_pid_vt(uint32_t pid);
uint32_t ring3_foreground_pid_vt(int vt);
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
#define RING3_PROCESS_SLEEPING 4U
#define RING3_PROCESS_COUNT  4U

#endif