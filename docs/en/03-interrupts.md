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
per-line masking; during `kmain` only IRQ0 (PIT) and IRQ1 (keyboard)
end up unmasked — everything else stays masked.

## PIT (`src/drivers/pit.c`)

8253 PIT channel 0 at `PIT_FREQUENCY` = 100 Hz. IRQ0 increments the tick
counter (`pit_ticks()`) used by `tick`, scheduling, and tests.

## Keyboard (`src/drivers/keyboard.c`)

PS/2 keyboard driver on IRQ1: scancode→ASCII translation with
configurable scancode set (`SCANCODE_SET` build flag). Input goes into
per-virtual-terminal 256-byte ring buffers (`keyboard_getchar_vt(vt)`);
the ISR only enqueues keys for the *active* vt, so hidden terminals
never see keystrokes. F1/F2 make codes (scan set 1 `0x3B`/`0x3C`) are
consumed by the ISR and turned into deferred `vt_request_switch()`
calls executed later by `vt_service()` — they never enter the input
stream. Ctrl+C delivers SIGINT to the active vt's foreground process.
`keyboard_wait_key()` remains for demo paths.

## Virtual terminals (`src/drivers/vt.c`)

F1 selects vt 0, F2 selects vt 1. Each vt owns a private screen backing
store (80x25 VGA text cells, or an offscreen VBE framebuffer stride in
VBE mode) plus its own foreground PID slot. `vt_service()` — polled by
the kernel idle loop (`shell3_supervisor` or `shell_read_line`,
mutually exclusive) — performs deferred switches and respawns dead
`/shell3.elf` instances. See
[15-virtual-terminals.md](15-virtual-terminals.md).

## RTC (`src/drivers/rtc.c`)

MC146818 RTC read via port I/O; `rtc_read_time()` + `rtc_to_beijing_time()`
feed the `time` command and boot banner. CMOS/BCD handling and the
Unix-timestamp helper live here.

## Serial (`src/drivers/serial.c`)

COM1 polled output (`serial_print`, `serial_print_hex`,
`serial_printf`) carries kernel-side output — diagnostics plus kernel
shell commands such as `echo` — plus `serial_getchar()` input. COM3
(`serial3_putchar`/`serial3_print`) is a TX-only port that mirrors
Ring 3 `IO_WRITE` output from both virtual terminals (interleaved).
Both TX paths bound their LSR wait (~1M polls) so a missing UART
cannot hang the kernel; on timeout the pending character is dropped
and COM3 logs one warning on COM1.
Most kernel diagnostics dual-write to VGA and COM1 so QEMU sessions
can be scripted over a TCP socket.
