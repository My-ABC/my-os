# MyOS System Call ABI (current state)

This document records the actual behavior of the `int 0x80` system-call interface, for Ring 3 programs and future ABI stabilization work.
Definitions live in `include/os/syscall.h`, implementation in `src/os/syscall.c`, and the assembly entry stub is `syscall` in `boot/interrupt.asm`.

中文版本: [中文版](../zh/04-syscall-abi.md)

## Calling convention

- Trigger: execute `int 0x80`. IDT gate `0x80` is a DPL=3 32-bit trap gate (`idt_set_entry(0x80, syscall, 0x08, 0xEF)` in `idt.c`); both Ring 3 and Ring 0 (CPL ≤ 3) can invoke it.
- Assembly stub `syscall`: saves registers and segment selectors in the `struct irq_frame` layout, calls `syscall_handler`, then reuses the IRQ0 restore sequence to return.
- **Register convention** — two-level numbering:
  - `eax` = system call **class** (1–7, the `SYS_*` group)
  - `ebx` = **sub-operation** number within that class
  - `ecx` = argument 1, `edx` = argument 2, `esi` = argument 3; at most 3 arguments, no stack arguments
  - `eax` also carries the return value on exit
  - All other general-purpose registers are preserved by `pushad`/`popad`
- **Interrupt state**: a trap gate does not clear IF, so the handler inherits the caller's interrupt state (Ring 3 user EFLAGS is `0x202`, i.e. IF=1). IRQ0 (PIT) and IRQ1 (keyboard) can therefore fire while the handler runs: the keyboard IRQ fills the scancode ring buffer, so the `keyboard_wait_key` blocking wait inside `IO_READ`/`IO_GETCHAR` is woken normally. IRQ0 sees a kernel-mode frame (`cs & 3 != 3`), and `ring3_timer_switch` does not switch in the middle of a kernel syscall; processes can explicitly yield at a safe syscall boundary with `PROC_YIELD`.
- **Return value**: `eax`. Undefined classes return `-1` (`0xFFFFFFFF`); unknown sub-operation numbers inside a known class return 0. **There is no errno; error semantics are not uniform.**
- **Ring 0 calls**: the kernel can invoke calls via `sys_call(klass, op, arg1, arg2, arg3)` (inline `int 0x80`) or raw `int 0x80`. In kernel context `ring3_current_pid()` returns 0, so `PROC_GETPID` returns 0 and `IO_WRITE` is silently dropped by the foreground check (which requires `pid != 0`).
- C wrappers: `sys_call` plus `sys_malloc`/`sys_calloc`/`sys_realloc`/`sys_free`/`sys_shutdown`/`sys_reboot`/`sys_getpid` (`include/os/syscall.h`).

## System call classes

| Class | Name | Sub-operations |
|-------|------|----------------|
| 1 | `SYS_IO` | `IO_WRITE`(1), `IO_GETCHAR`(2), `IO_READ`(3), `IO_CLEAR`(4), `IO_CURSOR_GET`(5), `IO_CURSOR_SET`(6), `IO_CONSOLE_INFO`(7), `IO_GETCHAR_NB`(8) |
| 2 | `SYS_TIME` | `TIME_GET`(1), `TIME_TO`(2), `TIME_SLEEP`(3) |
| 3 | `SYS_MEMORY` | `MEMORY_MALLOC`(1), `MEMORY_CALLOC`(2), `MEMORY_REALLOC`(3), `MEMORY_FREE`(4) |
| 4 | `SYS_POWER` | `PWR_SHUTDOWN`(1), `PWR_REBOOT`(2) |
| 5 | `SYS_PROCESS` | `PROC_GETPID`(1), `PROC_EXIT`(2), `PROC_YIELD`(3), `PROC_LIST`(4), `PROC_SET_FG`(5), `PROC_EXEC`(6) |
| 6 | `SYS_FS` | `FS_OPEN`(1) … `FS_READDIR`(12) |
| 7 | `SYS_SIG` | `SIG_KILL`(1), `SIG_SIGNAL`(2), `SIG_RETURN`(3) |

## `SYS_IO` (1)

| Op | Name | Arguments | Returns | Purpose |
|----|------|-----------|---------|---------|
| 1 | `IO_WRITE` | `ecx` = `const char *` (NUL-terminated string pointer) | 0 on success, -1 on error | Writes a string (max `IO_WRITE_MAX` = 4096 bytes) to the process's own virtual-terminal screen buffer and mirrors it to COM3 (`vga_print` + `serial3_print`; no length argument, NUL-terminated). The string is first copied into a bounded kernel buffer, so invalid pointers or missing NULs return -1 instead of panicking. **Output only happens when the caller's PID equals `ring3_foreground_pid_vt()` of the caller's vt**; calls from background processes or kernel context are silently dropped. COM1 carries kernel-side output only — Ring 3 output does not appear there. |
| 2 | `IO_GETCHAR` | none | char | Blocks until a keyboard character is available, looping until a nonzero char; effectively never returns 0. |
| 8 | `IO_GETCHAR_NB` | none | char or 0 | Non-blocking key poll: returns the next buffered char, or 0 when the buffer is empty or the caller is not the vt foreground process. The Ctrl+C marker byte is consumed silently (IRQ1 already delivered SIGINT). |
| 3 | `IO_READ` | `ecx` = `char *buf`, `edx` = `size` | line length in bytes (excluding NUL; 0 = empty line), -1 on error | Reads one line from the keyboard into `buf` (via `gets`, with echo). Writes at most `size-1` chars plus NUL. Returns -1 when `size <= 0` or `buf` is not wholly inside permitted user ranges. Blocks until a newline arrives. |
| 4 | `IO_CLEAR` | none | 0 | Clears the screen (`vga_clear`). |
| 5 | `IO_CURSOR_GET` | none | `(row << 16) \| col` | Returns the current cursor position via `vga_get_cursor`. |
| 6 | `IO_CURSOR_SET` | `ecx` = row, `edx` = col | 0 | Moves the cursor via `vga_set_cursor` (works in both VGA text and VBE modes). |

## `SYS_TIME` (2)

| Op | Name | Arguments | Returns | Purpose |
|----|------|-----------|---------|---------|
| 1 | `TIME_GET` | none | Unix timestamp | `rtc_read_time` + `rtc_to_unix_timestamp`; returns the current UTC time as a Unix timestamp. |
| 2 | `TIME_TO` | `ecx` = signed offset in **seconds**, `edx` = `rtc_time_t *` user buffer | 0 or -1 | Reads the RTC and applies `rtc_convert_timezone` with the given offset, then copies the resulting `rtc_time_t` (8 bytes: second, minute, hour, day, month, year) into the user buffer. The buffer must pass user-pointer validation. |
| 3 | `TIME_SLEEP` | `ecx` = duration in **microseconds** | 0, or -1 when the TSC is uncalibrated | Ring 3 callers park in `RING3_PROCESS_SLEEPING` until a PIT-tick deadline (10 ms granularity), yielding the CPU so other processes and `vt_service` run; the syscall restarts via the yield-rewind contract and returns 0 on expiry. Pending signals wake the sleeper early (deadline abandoned); `PROC_KILL` works on sleeping processes. Kernel-context callers keep the legacy TSC busy-wait. |

## `SYS_MEMORY` (3)

| Op | Name | Arguments | Returns |
|----|------|-----------|---------|
| 1 | `MEMORY_MALLOC` | `ecx` = byte count | user-heap pointer, 0 on failure; `size == 0` returns 0 |
| 2 | `MEMORY_CALLOC` | `ecx` = element count, `edx` = element size | zeroed user-heap pointer, 0 on failure; returns 0 if `ecx*edx` overflows |
| 3 | `MEMORY_REALLOC` | `ecx` = pointer, `edx` = new byte count | new pointer, 0 on failure; `ptr == 0` behaves like malloc, `size == 0` behaves like free |
| 4 | `MEMORY_FREE` | `ecx` = pointer | 0; `NULL` or out-of-range pointers are silently ignored |

User heap range: `0x01000000`–`0x40000000` (`USER_HEAP_START`/`USER_HEAP_END`), allocated and mapped page-by-page as USER pages; each block has a `struct user_block` header (magic `0x55534552`), and freed blocks go on a free list for reuse while the block's first page stays mapped.

## `SYS_POWER` (4)

| Op | Name | Behavior |
|----|------|----------|
| 1 | `PWR_SHUTDOWN` | ACPI shutdown |
| 2 | `PWR_REBOOT` | ACPI reboot |

## `SYS_PROCESS` (5)

| Op | Name | Arguments | Returns | Purpose |
|----|------|-----------|---------|---------|
| 1 | `PROC_GETPID` | none | current Ring 3 PID | Returns 0 when called from kernel context. |
| 2 | `PROC_EXIT` | `ecx` = exit status (accepted, not recorded yet) | does not return | Terminates the current Ring 3 process and reclaims its resources (see "Process exit" below). From kernel context it degenerates to a plain 0 return. |
| 3 | `PROC_YIELD` | none | 0 | Voluntarily yields the CPU, saving the current Ring 3 context and returning to the shell; the process is resumed by PIT scheduling. |
| 4 | `PROC_LIST` | `ecx` = buffer, `edx` = size | process count, or -1 | Enumerates live processes into 12-byte records `{pid, state, is_foreground}`; `state` uses `RING3_PROCESS_*` values (0 unused, 1 ready, 2 running, 3 dead, 4 sleeping). Buffer must pass user-pointer validation and hold `count * 12` bytes. |
| 5 | `PROC_SET_FG` | `ecx` = pid (`0` clears the foreground) | 0 or -1 | Sets or clears the foreground process via `ring3_set_foreground`/`ring3_set_background`. |
| 6 | `PROC_EXEC` | `ecx` = ELF32 path on FAT32 | 0 or -1 | Reads the file, validates/loads it with `elf32_load`, and schedules it as a **foreground** Ring 3 process (same path as the shell `run` command). Returns -1 on any failure; the new pid is not reported. |

## `SYS_FS` (6)

| Op | Name | Arguments | Returns | Purpose |
|----|------|-----------|---------|---------|
| 1 | `FS_OPEN` | `ecx` = path string, `edx` = mode | fd (0–7) or -1 | Opens a FAT32 file. Modes: `0`=`SYSFS_O_RDONLY` (file must exist), `1`=`SYSFS_O_RDWR` (allowed to not exist; created on first write). Directories are always rejected. |
| 2 | `FS_CLOSE` | `ecx` = fd | 0 or -1 | Closes a file descriptor. |
| 3 | `FS_READ` | `ecx` = fd, `edx` = `buf`, `esi` = `size` | bytes read, 0 at EOF, -1 on error | Reads from the current offset, which advances. |
| 4 | `FS_WRITE` | `ecx` = fd, `edx` = `buf`, `esi` = `size` | bytes written or -1 | Only `O_RDWR` descriptors may write; writes at the current offset and advances, zero-extending when the file is too small. |
| 5 | `FS_UNLINK` | `ecx` = path string | 0 or -1 | Deletes a FAT32 file (`fat32_delete_file`). |
| 6 | `FS_MKDIR` | `ecx` = path string | 0 or -1 | Creates a FAT32 directory (`fat32_make_directory`). |
| 7 | `FS_RMDIR` | `ecx` = path string | 0 or -1 | Removes an empty FAT32 directory (`fat32_remove_directory`, non-recursive). |
| 8 | `FS_STAT` | `ecx` = path string | file size; -2 for a directory; -1 on error | Queries by path, not by fd. |
| 9 | `FS_GETCWD` | `ecx` = `buf`, `edx` = `size` | path length (excluding NUL) or -1 | Copies the current working directory (NUL included) into the user buffer; `size` must be ≥ length+1. |
| 10 | `FS_CHDIR` | `ecx` = path string | 0 or -1 | Changes the working directory; the target must be an existing directory. |
| 11 | `FS_RENAME` | `ecx` = old path, `edx` = new path | 0 or -1 | Moves/renames a file or directory (`fat32_move`); both paths resolve against the cwd. |
| 12 | `FS_READDIR` | `ecx` = path, `edx` = `buf`, `esi` = `size` | bytes written, or -1 | Lists a directory into the user buffer as newline-terminated lines `d name` / `- name` (`d` = directory). Staged in a 4 KiB kernel buffer; `size` truncates the copy. |

## `SYS_SIG` (7)

| Op | Name | Arguments | Returns | Purpose |
|----|------|-----------|---------|---------|
| 1 | `SIG_KILL` | `ecx` = pid, `edx` = signal number | 0 or -1 | Sends a signal to a process (marks it pending). |
| 2 | `SIG_SIGNAL` | `ecx` = signal number, `edx` = handler (0=default, 1=ignore, else user address) | 0 or -1 | Sets the current process's disposition for a signal; `SIGKILL` cannot be caught. |
| 3 | `SIG_RETURN` | none | restored `eax` | Restores the context saved when the running signal handler was delivered. Called only by the kernel-placed restorer stub after a handler returns; returns -1 when no handler is active. |

## Filesystem calls

- **Path resolution**: every path argument is a user-mode NUL-terminated string, first resolved and normalized against the current working directory (`fat32_resolve_path`, sharing the same `g_cwd` as the shell; relative paths, `.` and `..` are supported), then passed to FAT32. Paths are limited to 127 characters.
- **File descriptors**: the kernel keeps one global fd table (`SYSFS_MAX_FDS` = 8 entries); an fd is a table index 0–7. The table is global, not per-process; exiting processes do **not** auto-close their fds (a later process may inherit offset state — a known limitation).
- **Read/write semantics**: the FAT32 layer only offers whole-file I/O, so `FS_READ` re-reads the entire file (≤ `FAT32_MAX_FILE_SIZE` = 1 MiB) into a kernel buffer on every call and copies out the data at the offset; `FS_WRITE` uses read-modify-write: read the existing content, overwrite at the offset, and write the whole file back at `max(old size, offset+length)`. Large files and frequent small writes are inefficient — a known limitation.
- **User-pointer validation**: `IO_WRITE`, `IO_READ`, the FS calls, and `TIME_TO` validate pointers — `IO_WRITE` copies via `sys_copy_user_string` (4096-byte cap), `IO_READ`/FS/`TIME_TO` check `sys_user_buffer_ok`; invalid pointers return -1 instead of panicking. Validation requires pages to be *mapped* in the current `user_mm` (`paging_mm_get_phys`), not merely inside a registered range. `IO_GETCHAR` takes no pointer and `MEMORY_*` still dereference caller pointers directly (see Known limitations).
- **Working directory**: `g_cwd` lives in `src/fs/fat32.c`; the shell's `cd`/prompt and the user-mode `FS_GETCWD`/`FS_CHDIR` share the same directory.

## Process exit (`PROC_EXIT`)

`PROC_EXIT` calls `ring3_terminate_current()`: it releases the process's user stack and ELF image pages, marks it DEAD, clears the foreground PID (if any), and stores the saved kernel `irq_frame` into the global `syscall_exit_frame`. When the `syscall` stub sees a nonzero value it skips `popad`/`iret` back to user mode, switches `esp` to that kernel frame, and replays the IRQ0 epilogue to resume the shell context — the same path ISR14 uses to kill a process. `syscall_exit_frame` is cleared at the top of every `syscall_handler` invocation.

## Signals

See [13-signals.md](13-signals.md) for the signal mechanism: numbers,
per-process pending/blocked/handler state, Ctrl+C `SIGINT` delivery,
user-mode handler invocation via `SIG_RETURN`, `SIG_KILL`/`SIG_SIGNAL`,
and current limitations.

## Known limitations (to resolve before stabilizing the ABI)

- **Partial user-pointer validation**: `IO_WRITE` and `IO_READ` now copy/validate caller pointers and return -1 on bad input, but `MEMORY_*` still dereference caller pointers directly; an illegal pointer there triggers a page fault that kills the process, and in kernel context a kernel page-fault panic (`kfault.elf` test).
- **No ABI versioning**: class/sub-operation numbers are bare integers with no version field.
- **Non-uniform error semantics**: undefined classes return `-1`, unknown sub-operations and memory-operation failures return 0, and successful calls return 0/pointer/timestamp depending on the call; callers must check each one.
- **Blocking calls do not yield the CPU**: IRQ0 still fires during `IO_READ`/`IO_GETCHAR`, but `ring3_timer_switch` sees a kernel-mode frame and returns without switching, so other Ring 3 processes cannot be scheduled until the call returns. The keyboard wait itself is woken normally by IRQ1 (the trap gate preserves IF); cooperative code can call `PROC_YIELD` at safe syscall boundaries.
- **`IO_READ`/`IO_GETCHAR` only talk to the keyboard**: no file/console abstraction, and no foreground check — a background process on the same vt can consume keystrokes intended for the foreground process. Arrow keys are delivered as the control bytes `KEYBOARD_KEY_UP`/`DOWN`/`LEFT`/`RIGHT` (0x01–0x04); `IO_GETCHAR` returns them as ordinary char values while `IO_READ` stores them raw in the buffer and echoes them — consumers needing line editing should filter them or use `IO_GETCHAR`.
- **No device-access interfaces** (serial, raw ATA, etc.) and no process create/wait calls (`PROC_EXIT` exists; `kill` is still shell-only).
- The fd table is global and fds are not reclaimed on process exit; `FS_READ`/`FS_WRITE` do whole-file read-modify-write, so I/O is expensive.
- `IO_WRITE`'s foreground/background suppression is implicit; callers cannot choose an output channel.
