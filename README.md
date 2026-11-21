English | [中文](README-zh.md) | [Documentation](docs/en/)

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
- **System calls** — Ring 3 can write output, query its process ID with `SYS_GETPID`, and cooperatively yield with `SYS_YIELD`
- **Signals** — `SIGINT`/`SIGKILL` with per-process pending state; Ctrl+C delivers `SIGINT` to the foreground process, and user-registered handlers run via a restorer trampoline and `SYS_SIGRETURN`
- **ELF32** — validates and loads in-memory i386 ELF loadable segments with user permissions and BSS zeroing; foreground output hides the Shell prompt until the process exits or is backgrounded, while Shell input remains available
- **Page-fault handling** — ISR14 demand-maps only registered user ranges (ELF image, 32 KiB user stack, user heap) with zeroed pages, kills processes on illegal user access or protection violations, and panics on illegal kernel faults
- **shell(kernel)** — impl a shell on kernel mode 
- **SVGA** — `svga_blue` fills the GRUB framebuffer with blue
- **Memory paging** — implemented full memory paging.
- **PAE paging** — 64-bit PAE entries, 32-byte PDPT/32-bit CR3, 4 KiB pages, and a mapping window for accessing high frames and page tables. The PMM supports physical addresses below 64 GiB (36-bit PAE limit); available RAM is determined by the Multiboot map. The boot-time Multiboot info and map buffers must currently be in the first 16 MiB. The optional `make test-qemu-highmem` target boots with QEMU `-m 6G`, allocates a real page at or above 4 GiB, verifies PAE mapping and read/write access, and frees it. The standard QEMU integration suite uses 128 MiB.
- **PMM** — impl PMM
- **heap allocator(kernel)** — impl kmalloc, kfree, kcalloc, krealloc
- **heap allocator(user)** — impl heap allocator in the syscall
- **ATA PIO** — primary-master LBA28 sector read/write support
- **FAT32** — file and directory operations with ASCII long filenames (LFN,
  up to FAT32's 255 UTF-16 code-unit limit) and 8.3 aliases; current Shell
  buffers impose a lower path limit, and UTF-16LE names outside ASCII display
  as `?`

## Download

The latest tagged release includes a GRUB-bootable ISO built by GitHub Actions:

[Download myos.iso](https://github.com/My-ABC/my-os/releases/latest/download/myos.iso) · [All releases](https://github.com/My-ABC/my-os/releases)

## Building

Requirements by target: kernel builds need `nasm` and an i686 toolchain; host
tests need Python 3 and a host `cc`; QEMU runs and integration tests need
`qemu-system-i386`; ISO targets additionally need `grub-pc-bin`, `xorriso`, and
`grub-common`. The `make test-qemu-highmem` target also requires those GRUB/ISO
tools to create its temporary test image.

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
make test-qemu      # build and run the bounded headless QEMU integration suite
make test-qemu-highmem  # QEMU -m 6G: allocate/map/test/free a page above 4 GiB
make disk           # create disk.img as a 64 MB FAT32 image
make run            # attach disk.img when it exists; otherwise boot without a disk
make run-disk       # create disk.img if needed and boot -kernel with it attached
```

At startup the kernel probes the primary-master ATA device. If a disk is
present, it automatically runs the Shell disk initialization, which mounts a
supported FAT32 volume. `make run` does not create a disk image; use `make disk`
first if you want one.

Tests are grouped by purpose under `test/`: `python/` contains the host Python
tests and small source-contract suites, `c/` contains host C test programs,
`shell/` contains the separate shell launchers for those C suites, and
`support/` contains their shared compile helper and assertions. QEMU-only
checks live under `integration/`; the runner, QEMU session, FAT32 test image,
and behavior scenarios are separate modules. `make test` discovers Python
tests, which in turn run each shell suite. C suites use host `cc` directly,
independent of the kernel's `CC` cross-compiler. PMM host tests also exercise
allocation and release of a physical frame above 4 GiB. These checks do not
execute privileged instructions, access physical addresses, or boot QEMU; hardware
behavior still requires emulator or device validation. C checks and named
Python cases print clear case names and PASS/FAIL results.

`make test` intentionally remains host-only, so it works without QEMU or the
kernel cross-toolchain (it needs Python 3 and host `cc`). `make test-qemu`
requires the kernel build tools (`nasm` and an i686 compiler/linker), Python 3,
`mkfs.fat`, and `qemu-system-i386`; it builds `myos.bin`, starts
`qemu-system-i386` in headless TCG mode, and drives the PS/2 keyboard through
QEMU's monitor while checking COM1 output. It attaches a generated FAT32 IDE disk
and exercises startup, shell input, ATA/FAT32 write/read commands, and runs an ELF demo assembled from
`boot/elf_demo.asm` after placing it in the test disk image. It also covers Ring 3
scheduling/listing/foreground/background/termination and ELF32 loading,
execution, cleanup, and reload. The harness has an overall timeout, bounded
startup/serial/monitor waits, reports each named PASS/FAIL check, and terminates
QEMU on completion or failure. QEMU is a separate required prerequisite for
this target; override its executable with `make test-qemu QEMU=/path/to/qemu`.
It uses Python's built-in socket support, so host `timeout`, `socat`, and
netcat are not required.
This emulates one QEMU PC/TCG configuration and is not a substitute for testing
other firmware, hardware, or booting the GRUB ISO.

`make test-qemu-highmem` is a separate, bounded GRUB/Multiboot boot under QEMU TCG
with 6 GiB of RAM. It verifies an allocated physical page at or above 4 GiB by
mapping the page, checking a written pattern, unmapping it, freeing it, and
confirming the frame can be allocated again. It uses a separate
`build/highmem-test/` directory and does not change the standard QEMU test's
128 MiB memory configuration.

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
echo     print text on screen and COM1 serial
reboot   use ACPI to reboot
shutdown use ACPI to shutdown
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
disk     initialize ATA and detect FAT32
ls       list the current FAT32 directory, or `ls <path>` for another directory
cat      print a FAT32 file
write    write text to a FAT32 file
cp       copy a FAT32 file; `cp -r <dir> <dst>` recursively copies a directory tree
mv       move or rename a FAT32 file or directory; an existing directory destination
        receives the source inside it, an existing file destination is replaced
rm       delete a FAT32 file
cd       change the FAT32 directory
mkdir    create a FAT32 directory
rmdir    remove an empty FAT32 directory; `rmdir -f <dir>` removes recursively
run      load and run a disk ELF32 file
```

FAT32 long filenames support ASCII names such as `help_aaaaa.1234`. Every file
and directory created through the Shell gets LFN entries, so names keep their
original case (`write testtest` lists as `testtest`, not `TESTTEST`). LFN
entries are stored as UTF-16LE and linked to a generated 8.3 alias; name
lookups match the LFN case-sensitively, while entries without LFN still match
through the uppercase 8.3 short name. The shell currently accepts ASCII
filenames; non-ASCII UTF-16 code units display as `?`. Shell path buffers
limit typed paths to less than the on-disk FAT32 LFN maximum.

When a FAT32 disk is mounted, the shell prompt shows its current path (for
example, `myos / >` or `myos /folder >`). Use `cd ..` to move to the parent
directory. Without a mounted FAT32 disk, the prompt remains `myos >`.

Each `ring3` command starts one Ring 3 demo process in the background. Processes
are preemptively scheduled by the PIT; their output is hidden until `fg <pid>`
is used. The demo yields while waiting between clock updates so shell input
remains responsive. `bg <pid>` hides output again, `ps` lists process states, and
`kill <pid>` terminates a process.

While a process is foregrounded, the shell hides its prompt to keep process
output from appearing on the prompt line. The shell still polls keyboard input,
so `bg <pid>` and `kill <pid>` remain available; the prompt returns when the
process exits or is backgrounded.

The `elf` command starts the built-in ELF32 demo in the foreground, so its
`Hello from ELF32` system-call output is visible while the shell remains
responsive. The demo installs a `SIGINT` handler and then waits; pressing
Ctrl+C makes the handler print `Bye` and exit via `SYS_EXIT`, which releases
its ELF user mappings so `elf` can be run again.

## Layout

```
boot/             Multiboot header, startup, and interrupt stubs (NASM)
src/kernel.c      kernel initialization and top-level startup
src/arch/x86/     x86 descriptors, interrupts, and port I/O
src/mm/           physical/virtual memory and heap
src/os/           ELF loading, Ring 3, and system calls
src/shell/        interactive shell
src/drivers/      ATA, console, keyboard, timer, RTC, and display drivers
src/fs/           FAT32 filesystem
src/lib/          freestanding C library routines
include/          headers grouped to mirror src/ subsystems
docs/             bilingual architecture docs and references (en/ + zh/)
scripts/          CI/CD files
linker.ld         1 MB load address and section layout
[TODO.md](./TODO.md) English project roadmap | [TODO-zh.md](./TODO-zh.md) Chinese project roadmap
```

## Notes

The default QEMU `pc` machine (SeaBIOS) only exposes an ACPI 1.0 FADT, which has no reset register, so reboots there go through the `0xCF9` fallback. With `-machine q35` the firmware provides an ACPI 2.0+ FADT and the reset register is used.

## License

[GPLv2](LICENSE)
