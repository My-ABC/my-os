# 05 — Ring 3 Processes

English | [中文](../zh/05-processes.md)

## Process table (`src/os/ring3.c`)

Up to `RING3_PROCESS_COUNT = 2` concurrent Ring 3 processes. States:
`UNUSED`, `READY`, `RUNNING`, `DEAD` (`include/os/ring3.h`). Each process
owns a user stack (32 KiB at `0x810000` or `0x820000`), an entry point,
and recorded ELF image ranges used by the page-fault handler.

## Lifecycle

- `ring3_schedule(entry)` — map the entry page `USER|WRITABLE|PRESENT`,
  register the process, and let the scheduler run it.
- `ps` / `fg <pid>` / `bg <pid>` / `kill <pid>` — shell commands wrapping
  `ring3_process_list`, `ring3_set_foreground`,
  `ring3_set_background`, `ring3_kill`.
- Foreground/background only suppresses a process's console output; both
  keep running.
- While a foreground process runs, the shell hides its prompt and stops
  consuming the keyboard (`sti; hlt`), so typed keys stay buffered for
  the child or for the shell after the child exits. Ctrl+C is handled
  earlier, inside the keyboard ISR, which delivers `SIGINT` to the
  foreground process directly — the shell never needs to read the
  control byte for that to work.
- The Ring 3 demo calls `PROC_YIELD` while waiting for the RTC second to change,
  avoiding a tight syscall-polling loop that would slow shell input.
- `ring3_terminate_current()` is called from `isr14_handler` when a user
  process faults illegally: it marks the process `DEAD`, frees its
  resources (stack pages, heap, image mappings), restores `esp0`, and
  hands the kernel `irq_frame` back so the shell continues.

## ELF loading (`src/os/elf.c`)

`elf_run(image, size)` parses a 32-bit ELF, maps its PT_LOAD segments into
the user range, sets up the initial stack frame, and schedules the
process. Two shell paths use it:

- `elf` — runs the ELF32 demo linked into the kernel image
  (`boot/elf_demo.asm`).
- `run <file>` — reads an ELF32 file from FAT32 into a 1 MiB kernel
  buffer (`FAT32_MAX_FILE_SIZE`) and calls `elf_run`.

## Memory isolation

Each Ring 3 process owns a private address space with its own CR3 —
see [06-memory-isolation.md](06-memory-isolation.md).

## Limitations

- No `wait`/exit-status reporting, more signal numbers, or IPC.
- The syscall interface is documented in
  [04-syscall-abi.md](04-syscall-abi.md).
