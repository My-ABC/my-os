#include "pit.h"
#include "io.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t ticks = 0;

void pit_init(uint32_t hz) {
    uint32_t divisor = PIT_BASE_FREQ / hz;

    outb(PIT_COMMAND, 0x36);  // 通道 0, 先低后高字节, 方波模式
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

uint32_t pit_ticks(void) {
    return ticks;
}

void pit_tick(void) {
    ticks++;
}
