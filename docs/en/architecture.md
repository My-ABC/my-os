# MyOS Architecture

English | [中文](../zh/architecture.md)

MyOS is a freestanding 32-bit x86 educational monolithic kernel written in C
and NASM assembly. It boots through GRUB/Multiboot, runs in 64-bit PAE paging
mode with a higher-half kernel mapping, and provides a Ring 3 user mode with
a small system-call interface, an interactive shell, and a FAT32 filesystem
on an ATA disk.

## Documentation index

Numbered documents cover each subsystem in detail:

| Doc | Topic |
|---|---|
| [01-boot.md](01-boot.md) | Multiboot header, early PAE page tables, `kmain` init sequence |
| [02-memory.md](02-memory.md) | PAE paging, PMM, kernel heap, user address space, ISR14 demand paging |
| [03-interrupts.md](03-interrupts.md) | IDT/PIC/PIT, `irq_frame` contract, exception and IRQ dispatch |
| [04-syscall-abi.md](04-syscall-abi.md) | `int 0x80` gate, calling convention, call table, ABI limitations |
| [05-processes.md](05-processes.md) | Ring 3 scheduler, ELF loading, foreground/background, termination |
| [06-memory-isolation.md](06-memory-isolation.md) | Per-process page tables, CR3 switching, mm teardown |
| [07-shell.md](07-shell.md) | Interactive shell loop, command dispatch, path resolution |
| [08-shell-commands.md](08-shell-commands.md) | Every shell command, arguments, and semantics |
| [09-filesystem.md](09-filesystem.md) | ATA PIO, FAT32 module layout, name encoding, directory semantics |
| [10-drivers.md](10-drivers.md) | Keyboard, RTC, serial, VGA/VBE, ACPI, PIC/PIT driver notes |
| [11-build-and-test.md](11-build-and-test.md) | Makefile targets, linker layout, host tests, QEMU integration suite |
| [12-driver-gaps.md](12-driver-gaps.md) | Driver capability inventory, missing drivers by bus, priorities, QEMU test devices |
| [13-signals.md](13-signals.md) | Signal numbers, delivery, handlers, restorer trampoline, `SIG_RETURN` |
| [14-mini-libc.md](14-mini-libc.md) | `mini-libc/` user-space C library, syscall wrappers, build notes |
| [15-virtual-terminals.md](15-virtual-terminals.md) | F1/F2 virtual terminals, per-vt shells and screen backing stores, vt lifecycle |

Chinese translations live under [../zh/](../zh/).

## Repository layout

```
boot/             Multiboot header, startup, GDT/paging asm, interrupt stubs
src/kernel.c      kernel entry (kmain) and top-level init sequence
src/arch/x86/     GDT/IDT/TSS, trap handlers, port I/O
src/mm/           PMM, PAE paging, kernel heap
src/os/           panic, ELF loader, Ring 3 scheduler, syscall layer
src/shell/        interactive shell
src/drivers/      ATA, keyboard, PIC/PIT, RTC, serial, VGA/VBE, ACPI
src/fs/           FAT32 (core/name/dir split + private fat32_int.h)
src/lib/          freestanding string/stdio
include/          public headers mirroring src/ subsystems
docs/en|zh/       this documentation
test/             host contract tests, C suites, QEMU integration tests
```

## High-level call graph

- `boot/boot.asm:start` → early PAE tables → `kmain(multiboot_info)`.
- `kmain` initializes GDT/TSS, serial, PMM, paging, VBE/VGA/VT, ATA/FAT32,
  IDT, heap self-test, PIC/PIT/keyboard, TSC/RTC, then enters
  `shell3_supervisor()` — which auto-starts `/shell3.elf` on both virtual
  terminals (F1/F2) and falls back to the kernel `start_shell()` when
  no disk is mounted, the file is missing, or vt 0's shell3 spawn
  permanently fails.
- The shell dispatches `cmd_table` entries; disk commands call the FAT32
  public API (`include/fs/fat32.h`); process commands call the Ring 3 API
  (`include/os/ring3.h`); `run`/`elf` go through `elf_run`.
- Hardware events flow: IRQ stubs in `boot/interrupt.asm` → C handlers in
  `src/arch/x86/` and `src/drivers/`; `int 0x80` → `syscall_handler`.
- Page faults go through `isr14_handler`, which may demand-map user pages
  or terminate the faulting Ring 3 process.
