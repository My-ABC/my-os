# 09 — Drivers

English | [中文](../zh/09-drivers.md)

All drivers live in `src/drivers/` with headers in `include/drivers/`.
They are polled or IRQ-driven, whichever the hardware allows; there is no
device-registration bus layer yet.

| Driver | File(s) | Notes |
|---|---|---|
| ATA PIO | `ata.c` | Primary master, LBA28, 512-byte sectors; see [08-filesystem.md](08-filesystem.md) |
| Keyboard | `keyboard.c` | PS/2 scancodes → ASCII, IRQ1, configurable scancode set via `SCANCODE_SET` |
| PIC | `pic.c` | 8259 master/slave remap and per-line mask helpers |
| PIT | `pit.c` | 8253 channel 0, `PIT_FREQUENCY` = 100 Hz, tick counter |
| RTC | `rtc.c` | MC146818 CMOS time, BCD decode, Beijing-time conversion, Unix timestamp |
| Serial | `serial.c` | COM1 polled TX/RX, `serial_printf` family |
| VGA | `vga.c` | 80x25 text mode, colors, clear/print helpers |
| VBE | `vbe.c` | Linear framebuffer from the Multiboot/BIOS info; blue-screen demo |
| ACPI | `acpi.c` | Power-off and reboot via ACPI PM registers |
| Font | `ascii_font.c` | Bitmap font data for VBE text rendering |

## Cross-cutting notes

- Console output is duplicated to VGA and serial for most subsystems;
  headless QEMU sessions drive everything through the serial socket.
- Driver init happens in `kmain` (`src/kernel.c`); IRQ unmasking is
  staged so PIT/keyboard come up only after the IDT exists.
- ACPI table parsing beyond PM power registers (MADT, HPET, AML) is not
  implemented yet — tracked in `TODO.md`.
