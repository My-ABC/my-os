#ifndef _TSC_H
#define _TSC_H

#include "stdint.h"

/* Calibrate the TSC against the PIT tick rate. Call once after pit_init
 * while interrupts are enabled. */
void tsc_init(void);

/* Current TSC value (may wrap; always compare deltas). */
uint64_t tsc_read(void);

/* Calibrated TSC frequency in Hz, or 0 if calibration failed. */
uint32_t tsc_freq_hz(void);

/* TSC cycles corresponding to `usec` microseconds (0 if uncalibrated). */
uint64_t tsc_cycles_for_us(uint64_t usec);

/* Busy-wait for approximately `usec` microseconds. */
void tsc_delay_us(uint32_t usec);

#endif
