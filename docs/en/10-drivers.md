# 10 — Drivers

English | [中文](../zh/10-drivers.md)

All drivers live in `src/drivers/` with headers in `include/drivers/`.
They are polled or IRQ-driven, whichever the hardware allows; there is no
device-registration bus layer yet.

| Driver | File(s) | Notes |
|---|---|---|
| ATA PIO | `ata.c` | Primary master, LBA28, 512-byte sectors; see [09-filesystem.md](09-filesystem.md) |
| Keyboard | `keyboard.c` | PS/2 scancodes → ASCII, IRQ1, configurable scancode set via `SCANCODE_SET`; arrow keys emit `KEYBOARD_KEY_*` control bytes (0x01–0x04) |
| PIC | `pic.c` | 8259 master/slave remap and per-line mask helpers |
| PIT | `pit.c` | 8253 channel 0, `PIT_FREQUENCY` = 100 Hz, tick counter |
| RTC | `rtc.c` | MC146818 CMOS time, BCD decode, 12/24-hour handling (PM bit extracted before BCD decode; decoded hours >12 are treated as 24-hour values because some RTCs report unreliable mode bits), Beijing-time conversion assuming the RTC holds UTC, Unix timestamp |
| Serial | `serial.c` | COM1 polled TX/RX for kernel diagnostics (`serial_printf` family, plus `serial_flush()` to drain the UART before power-cutting writes); COM3 (`serial3_putchar`/`serial3_print`) mirrors Ring 3 `IO_WRITE` output |
| VGA | `vga.c` | 80x25 text mode, per-vt cursor/color state via `vt_render_index()` |
| VBE | `vbe.c` | Linear framebuffer from the Multiboot/BIOS info; per-vt render target, blue-screen demo |
| VT | `vt.c` | Two virtual terminals (F1/F2): per-vt screen backing stores, deferred switch + shell respawn in `vt_service()` — see [15-virtual-terminals.md](15-virtual-terminals.md) |
| ACPI | `acpi.c` | Power-off and reboot via ACPI PM registers; shutdown prints a `powering off via ...` marker and drains COM1 (`serial_flush`) before every write that can cut power, so diagnostics are not truncated mid-line |
| Font | `ascii_font.c` | Bitmap font data for VBE text rendering |

## Cross-cutting notes

- Console output is duplicated to VGA and serial for most subsystems;
  headless QEMU sessions drive everything through the serial socket.
- Driver init happens in `kmain` (`src/kernel.c`); IRQ unmasking is
  staged so PIT/keyboard come up only after the IDT exists.
- ACPI table parsing beyond PM power registers (MADT, HPET, AML) is not
  implemented yet — tracked in `TODO.md`.
- For the capability inventory, missing-driver list, priorities, and
  QEMU test devices, see [12-driver-gaps.md](12-driver-gaps.md).
