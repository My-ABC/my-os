# 03 — Interrupts and Traps

English | [中文](../zh/03-interrupts.md)

## Stub layer (`boot/interrupt.asm`)

NASM stubs for exceptions, IRQs, and the `int 0x80` syscall gate. Each
stub pushes the interrupted context into a uniform `struct irq_frame`
(layout in `include/arch/x86/regs.h`) and tail-calls the C handler. The
`isr14` stub is special: when `isr14_handler` returns a non-zero kernel
`irq_frame *`, it switches to that frame's stack and replays the IRQ0
epilogue to abandon the faulting context (see
[02-memory.md](02-memory.md)).

## IDT (`src/arch/x86/idt.c`)

`idt_init()` populates the IDT: exception gates for CPU traps, IRQ gates
for the PIC lines, and a DPL=3 gate for `int 0x80` so Ring 3 can invoke
syscalls. Gate descriptors reference the stub addresses exported by
`boot/interrupt.asm`.

## PIC (`src/drivers/pic.c`)

The master/slave 8259 pair is remapped (`pic_remap()`) so IRQ0–15 do not
collide with CPU exceptions. `pic_set_mask`/`pic_clear_mask` control
per-line masking; `kmain` unmasks IRQ0 (PIT) and IRQ1 (keyboard) after
initialization and keeps the rest masked (`pic_set_mask(14)` leaves only
bit 1 clear at that point in the sequence — see `src/kernel.c`).

## PIT (`src/drivers/pit.c`)

8253 PIT channel 0 at `PIT_FREQUENCY` = 100 Hz. IRQ0 increments the tick
counter (`pit_ticks()`) used by `tick`, scheduling, and tests.

## Keyboard (`src/drivers/keyboard.c`)

PS/2 keyboard driver on IRQ1: scancode→ASCII translation with
configurable scancode set (`SCANCODE_SET` build flag), key events for
`gets()` in the shell, and `keyboard_wait_key()` for demo paths.

## RTC (`src/drivers/rtc.c`)

MC146818 RTC read via port I/O; `rtc_read_time()` + `rtc_to_beijing_time()`
feed the `time` command and boot banner. CMOS/BCD handling and the
Unix-timestamp helper live here.

## Serial (`src/drivers/serial.c`)

COM1 polled output (`serial_print`, `serial_print_hex`,
`serial_printf`) plus `serial_getchar()` input. Most kernel diagnostics
dual-write to VGA and serial so QEMU sessions can be scripted over a TCP
socket.
