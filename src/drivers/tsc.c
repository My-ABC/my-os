#include "tsc.h"
#include "pit.h"
#include "serial.h"

#define TSC_CALIBRATION_TICKS 20U /* ~200 ms at PIT_FREQUENCY=100 */

static uint32_t tsc_hz;

/* 64/32 division without libgcc (__udivdi3 is unavailable freestanding). */
static uint64_t u64_div(uint64_t n, uint64_t d) {
    uint64_t q = 0;
    uint64_t rem = 0;
    for (int i = 63; i >= 0; --i) {
        rem = (rem << 1) | ((n >> i) & 1ULL);
        q <<= 1;
        if (rem >= d) {
            rem -= d;
            q |= 1;
        }
    }
    return q;
}

uint64_t tsc_cycles_for_us(uint64_t usec) {
    if (tsc_hz == 0U) return 0;
    return u64_div(usec * (uint64_t)tsc_hz, 1000000U);
}

void tsc_init(void) {
    uint32_t start_ticks = pit_ticks();
    uint64_t t0 = tsc_read();
    while (pit_ticks() - start_ticks < TSC_CALIBRATION_TICKS) {
        __asm__ volatile ("pause");
    }
    uint64_t t1 = tsc_read();
    uint64_t elapsed_ticks = pit_ticks() - start_ticks;

    if (elapsed_ticks == 0U || t1 <= t0) {
        serial_print("[TSC] calibration failed\n");
        tsc_hz = 0;
        return;
    }
    /* hz = cycles * PIT_FREQUENCY / ticks */
    uint64_t hz64 = u64_div((t1 - t0) * PIT_FREQUENCY, elapsed_ticks);
    tsc_hz = hz64 > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)hz64;
    serial_print("[TSC] calibrated freq=");
    serial_print_dec(tsc_hz);
    serial_print(" Hz\n");
}

uint64_t tsc_read(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint32_t tsc_freq_hz(void) {
    return tsc_hz;
}

void tsc_delay_us(uint32_t usec) {
    if (tsc_hz == 0U) return;
    uint64_t target = tsc_read() + tsc_cycles_for_us(usec);
    while ((int64_t)(tsc_read() - target) < 0) {
        __asm__ volatile ("pause");
    }
}
