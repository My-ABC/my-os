# 05 — Ring 3 Processes

English | [中文](../zh/05-processes.md)

## Process table (`src/os/ring3.c`)

Up to `RING3_PROCESS_COUNT = 4` concurrent Ring 3 processes. States:
`UNUSED`, `READY`, `RUNNING`, `DEAD` (`include/os/ring3.h`). Each process
carries a `vt` field (0 or 1) binding it to a virtual terminal, owns a
user stack (32 KiB, top at `0x00810000` or `0x00820000`), an entry
point, an optional `parent_pid`, and recorded ELF image ranges used by
the page-fault handler. The foreground PID is tracked **per vt**
(`foreground_pid[VT_COUNT]`).

## Lifecycle

- `ring3_schedule(entry)` / `ring3_schedule_mm(...)` /
  `elf_run_vt(image, size, vt)` — map the entry page
  `USER|WRITABLE|PRESENT`, register the process on the given vt
  (children inherit the parent's vt), and let the scheduler run it.
  `ring3_schedule_mm` takes ownership of the caller's `mm` on entry:
  on success the process slot owns it, on any failure it is released,
  so callers must not free it afterwards.
- `ps` / `fg <pid>` / `bg <pid>` / `kill <pid>` — shell commands wrapping
  `ring3_process_list`, `ring3_set_foreground`,
  `ring3_set_background`, `ring3_kill`.
- Foreground/background only suppresses a process's console output; both
  keep running. Foreground lookup, Ctrl+C delivery, and `IO_WRITE`
  gating are all scoped to the process's own vt.
- When a foreground child exits, `ring3_foreground_release()` restores
  `parent_pid` as that vt's foreground if the parent is still alive on
  the same vt — this prevents a doubled prompt when a `run` child
  finishes.
- While a vt's foreground process runs, that vt's shell hides its
  prompt and stops consuming the keyboard, so typed keys stay buffered
  for the child or for the shell after the child exits. Ctrl+C is
  handled inside the keyboard ISR, which delivers `SIGINT` to the
  *active* vt's foreground process directly.
- `ring3_terminate_current()` is called from `isr14_handler`, `PROC_EXIT`,
  and fatal-signal paths: it marks the process `DEAD`, frees its
  resources (stack pages, heap, image mappings), releases foreground
  ownership, restores `esp0`, and hands the kernel `irq_frame` back so
  the shell continues.
- `shell3_supervisor()` (kernel.c) keeps one `shell3.elf` alive per vt:
  a dead or missing shell is respawned by `vt_service()` — see
  [15-virtual-terminals.md](15-virtual-terminals.md).

## ELF loading (`src/os/elf.c`)

`elf_run_vt(image, size, vt)` parses a 32-bit ELF, maps its PT_LOAD
segments into the user range, sets up the initial stack frame, and
schedules the process on the given vt (`elf_run` defaults to the
caller's vt). Shell paths that use it:

- `run <file>` — reads an ELF32 file from FAT32 into a 1 MiB kernel
  buffer (`FAT32_MAX_FILE_SIZE`) and schedules it foreground on the
  caller's vt (same path as the kernel `run` command and `PROC_EXEC`).
- `shell3_supervisor`'s vt spawner — loads `/shell3.elf` from FAT32
  onto each vt.
- `elf` / `PROC_RUN_USERCODE` — the built-in ELF32 demo linked into the
  kernel image (`boot/elf_demo.asm`).

## Memory isolation

Each Ring 3 process owns a private address space with its own CR3 —
see [06-memory-isolation.md](06-memory-isolation.md).

## Limitations

- No `wait`/exit-status reporting, more signal numbers, or IPC.
- The syscall interface is documented in
  [04-syscall-abi.md](04-syscall-abi.md).
