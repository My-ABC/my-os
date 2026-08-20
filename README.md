English | [中文](README-zh.md)

# MyOS

A small 32-bit x86 kernel: boots via Multiboot, prints to VGA text mode and COM1, and handles CPU exceptions and hardware IRQs.

## Features

- **Multiboot boot** — loaded at 1 MB by GRUB/QEMU, sets up its own stack and calls `kmain`
- **VGA text mode** — 80x25 driver at `0xB8000` with colors, scrolling, hardware cursor, hex/dec printing
- **Serial console** — COM1 (`0x3F8`) logging, used by all headless tests
- **IDT** — 256 entries, real handlers for INT3 and IRQ0/IRQ1
- **PIC** — 8259 remapped to vectors `0x20`/`0x28`, per-IRQ masking, EOI
- **PIT timer** — channel 0 at 100 Hz (IRQ0), prints one number per second on COM1
- **Keyboard** — IRQ1, supports scancode set 1 and 2 (including the numeric keypad and `0xE0` extended codes), 32-byte ring buffer
- **RTC** — CMOS real-time clock, reads year/month/day and hour/minute/second, supports 4-digit year to avoid Y2K bugs, supports timezone conversion and Unix timestamps
- **Blue screen of death** — INT3 dumps all registers on a blue screen, then reboots
- **ACPI reboot** — resets through the FADT reset register, falling back to `0xCF9`, the keyboard controller, and finally a triple fault
- **shell(kernel)** — impl a shell on kernel mode 

## Building

Requirements: `nasm`, an i686 toolchain, `qemu-system-x86` (for running and testing).

```bash
make build          # -> myos.bin
make run            # QEMU with a graphical window
make run-nographic  # QEMU with the serial console on the terminal
```

The Makefile defaults to `i686-elf-gcc` / `i686-elf-ld`. Without a cross toolchain you can use the host compiler in 32-bit mode:

```bash
make CC=gcc LD=ld build   # needs gcc-multilib
```

## Testing

All tests run headless (no graphical window) and check the COM1 output.

```bash
make test               # test all
make test-timer         # 100 Hz timer: one incrementing number per second on COM1
make test-panic         # INT3 blue screen: register dump + automatic reboot
make test-keyboard      # keyboard (scancode set 1): 'b' -> blue screen, other keys -> halt
make test-scancode-set2 # keyboard (scancode set 2): switch to scancode set 2 and test
make test-rtc           # RTC: read and display current time, Beijing time, and Unix timestamp, verify year handling
make test-rtc-time RTC_TIME="2000-01-01T00:00:01"  # RTC: test with specific time (supports QEMU time format)
```

The keyboard and panic tests drive QEMU through its monitor (`-monitor pipe:`) to inject keys and take screenshots, so no extra tools are required. Pass make flags with `MAKE_ARGS`, e.g. `MAKE_ARGS="CC=gcc LD=ld" make test-keyboard`.

## Usage

After boot the kernel waits for a key:

- `b` — trigger a blue screen: register dump, a 5 second countdown, then a reboot
- any other key — print `Halted` and stop

### Scancode Set Configuration

The kernel supports setting the keyboard scancode set via a compile-time option:

```bash
make SCANCODE_SET=1 build  # Use scancode set 1 (default)
make SCANCODE_SET=2 build  # Use scancode set 2
make run
```

Scancode set 2 is the modern standard for PS/2 keyboards, using the `0xF0` prefix to identify break codes, while scancode set 1 uses the high bit.

Building with `make PANIC_DEMO=1` makes the kernel trigger `int $0x03` right after initialization, which is what `make test-panic` uses.

## shell use
```
command list:
time     get time
panic    call the INT3
echo     print the fist arg
reboot   use ACPI to reboot
secho    print text on COM1
tick     get tick of PIT
help     show help
clear    clear screen
hello    print hello message
```

## Layout

```
boot/     Multiboot header, kernel entry, interrupt stubs (NASM)
src/      kernel, VGA, serial, IDT, PIC, PIT, keyboard, RTC, panic, ACPI, shell
include/  hardware headers and freestanding type definitions
scripts/  headless QEMU tests
linker.ld 1 MB load address and section layout
```

## Notes

The default QEMU `pc` machine (SeaBIOS) only exposes an ACPI 1.0 FADT, which has no reset register, so reboots there go through the `0xCF9` fallback. With `-machine q35` the firmware provides an ACPI 2.0+ FADT and the reset register is used.

## License

[MIT](LICENSE)
