English | [中文](README-zh.md)

# MyOS

A small 32-bit x86 kernel: boots via Multiboot/GRUB, uses a GRUB-provided framebuffer console and COM1, and handles CPU exceptions and hardware IRQs.

## Features

- **Multiboot/GRUB video boot** — GRUB requests a 640x480x32 framebuffer and passes its address to the kernel
- **Framebuffer console** — 16x16 scaled ASCII font, upper/lowercase glyphs, punctuation, scrolling, and a blinking underline cursor
- **VGA text fallback** — direct `0xB8000` text output remains available when no GRUB framebuffer is provided
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
- **Ring 3 processes** — software context switching preempted by PIT IRQ0; run up to two user processes with separate kernel and user stacks
- **Process control** — `ps`, `fg <pid>`, `bg <pid>`, and `kill <pid>` shell commands; Ring 3 tasks run in the background by default
- **System calls** — Ring 3 can write output and query its process ID with `SYS_GETPID`
- **ELF32** — validates and loads in-memory i386 ELF loadable segments with user permissions and BSS zeroing; the `elf` command schedules it as a foreground Ring 3 task and returns to the shell
- **shell(kernel)** — impl a shell on kernel mode 
- **SVGA** — `svga_blue` fills the GRUB framebuffer with blue
- **Memory paging** — implemented full memory paging.
- **PMM** — impl PMM
- **heap allocator(kernel)** — impl kmalloc, kfree, kcalloc, krealloc
- **heap allocator(user)** — impl heap allocator in the syscall

## Download

The latest tagged release includes a GRUB-bootable ISO built by GitHub Actions:

[Download myos.iso](https://github.com/My-ABC/my-os/releases/latest/download/myos.iso) · [All releases](https://github.com/My-ABC/my-os/releases)

## Building

Requirements: `nasm`, an i686 toolchain, Python 3 and a host `cc` (for tests), `qemu-system-x86` (for running), `grub-pc-bin`, `xorriso`, `grub-common`.

```bash
make build          # -> myos.bin
make run            # boot the GRUB ISO with a graphical window
make run-kernel     # direct -kernel boot, without GRUB framebuffer
make run-nographic  # QEMU with the serial console on the terminal
make run-q35        # QEMU with q35 machine (better ACPI support)
make run-q35-nographic  # QEMU with q35 machine, serial console on terminal
make run-iso        # boot the ISO with q35 and a graphical window
make iso            # gen iso file
make test           # run host-side tests for kernel routines
```

`make test` uses Python `unittest` to discover the Python cases and every shell
suite in `test/`. C test suites compile with the host `cc` directly, independent
of the kernel's `CC` cross-compiler. Host-executed checks cover string routines,
RTC date/time calculations, ELF32 input validation, GDT/IDT descriptor
encoding, and stubbed keyboard, PIC, PIT, and serial behavior. Static source and
layout checks cover ACPI, heap, I/O, paging, panic, PMM, shell, syscall, traps,
TSS, VBE, VGA, and the IRQ0 frame contract. These checks do not execute
privileged instructions, access physical addresses, or boot QEMU; hardware
behavior still requires emulator or device validation. C checks and named
Python cases print clear case names and PASS/FAIL results.

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

Building with `make PANIC_DEMO=1` makes the kernel trigger `int $0x03` right after initialization.

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
ring3    start one Ring 3 process in the background
ps       list Ring 3 processes
fg       show a Ring 3 process's output (fg <pid>)
bg       hide a Ring 3 process's output (bg <pid>)
kill     terminate a Ring 3 process by PID (kill <pid>)
elf      load and run the built-in ELF32 test program
svga_blue fill the framebuffer with blue
```

Each `ring3` command starts one Ring 3 demo process in the background. Processes
are preemptively scheduled by the PIT; their output is hidden until `fg <pid>`
is used. `bg <pid>` hides it again, `ps` lists process states, and
`kill <pid>` terminates a process.

The `elf` command starts the built-in ELF32 demo in the foreground, so its
`Hello from ELF32` system-call output is visible while the shell remains
responsive. Use `bg <pid>` to hide the output and `kill <pid>` to stop the
demo; killing it releases its ELF user mappings so `elf` can be run again.

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

[GPLv2](LICENSE)
