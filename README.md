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
- **GDT** — global descriptor table, supports code segment, data segment, user mode segment, TSS
- **TSS** — impl esp0, ss0
- **Keyboard** — IRQ1, supports scancode set 1 and 2 (including the numeric keypad and `0xE0` extended codes), 32-byte ring buffer
- **RTC** — CMOS real-time clock, reads year/month/day and hour/minute/second, supports 4-digit year to avoid Y2K bugs, supports timezone conversion and Unix timestamps
- **Blue screen of death** — INT3 dumps all registers on a blue screen, then reboots
- **ACPI reboot** — resets through the FADT reset register, falling back to `0xCF9`, the keyboard controller, and finally a triple fault
- **ACPI shutdown** — shuts down via ACPI S5 state, with fallback to common ACPI I/O ports
- **Ring 3** — impl Ring3
- **syscall** — impl syscall for the Ring3
- **shell(kernel)** — impl a shell on kernel mode 
- **Memory paging** — partial memory mapping support.

## Building

Requirements: `nasm`, an i686 toolchain, `qemu-system-x86` (for running and testing), `grub-pc-bin`, `xorriso`, `grub-common`.

```bash
make build          # -> myos.bin
make run            # QEMU with a graphical window
make run-nographic  # QEMU with the serial console on the terminal
make run-q35        # QEMU with q35 machine (better ACPI support)
make run-q35-nographic  # QEMU with q35 machine, serial console on terminal
make run-iso        # QEMU with a graphical window use iso
make iso            # gen iso file
```

The Makefile defaults to `i686-elf-gcc` / `i686-elf-ld`. Without a cross toolchain you can use the host compiler in 32-bit mode:

```bash
make CC=gcc LD=ld build   # needs gcc-multilib
```

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
shutdown use ACPI to shutdown
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
scripts/  CI/CD files
linker.ld 1 MB load address and section layout
```

## Notes

The default QEMU `pc` machine (SeaBIOS) only exposes an ACPI 1.0 FADT, which has no reset register, so reboots there go through the `0xCF9` fallback. With `-machine q35` the firmware provides an ACPI 2.0+ FADT and the reset register is used.

## License

[MIT](LICENSE)
