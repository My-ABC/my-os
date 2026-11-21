# MyOS System Call ABI (current state)

This document records the actual behavior of the `int 0x80` system-call interface, for Ring 3 programs and future ABI stabilization work.
Definitions live in `include/os/syscall.h`, implementation in `src/os/syscall.c`, and the assembly entry stub is `syscall` in `boot/interrupt.asm`.

中文版本: [中文版](../zh/04-syscall-abi.md)

## Calling convention

- Trigger: execute `int 0x80`. IDT gate `0x80` is a DPL=3 32-bit trap gate (`idt_set_entry(0x80, syscall, 0x08, 0xEF)` in `idt.c`); both Ring 3 and Ring 0 (CPL ≤ 3) can invoke it.
- Assembly stub `syscall`: `pushad` → `call syscall_handler` → writes the return value back into the saved EAX (`[esp+28]`) → `popad` → `iret`.
- **Register convention**:
  - `eax` = system call number (on entry), return value (on exit)
  - `ebx` = argument 1, `ecx` = argument 2, `edx` = argument 3; at most 3 arguments, no stack arguments
  - All other general-purpose registers are preserved by `pushad`/`popad`
- **Interrupt state**: a trap gate does not clear IF, so the handler inherits the caller's interrupt state (Ring 3 user EFLAGS is `0x202`, i.e. IF=1). IRQ0 (PIT) and IRQ1 (keyboard) can therefore fire while the handler runs: the keyboard IRQ fills the scancode ring buffer, so the `keyboard_wait_key` blocking wait inside `SYS_READ`/`SYS_GETCHAR` is woken normally. IRQ0 sees a kernel-mode frame (`cs & 3 != 3`), and `ring3_timer_switch` returns it unchanged without a context switch, so a system call is never descheduled mid-handler (see "Known limitations").
- **Return value**: `eax`. Undefined call numbers return `-1` (`0xFFFFFFFF`). **There is no errno; error semantics are not uniform.**
- **Ring 0 calls**: the kernel can invoke calls via `sys_call(call_number, arg1, arg2, arg3)` (inline `int 0x80`) or raw `int 0x80`. In kernel context `ring3_current_pid()` returns 0, so `SYS_GETPID` returns 0 and `SYS_WRITE` is silently dropped by the foreground check (which requires `pid != 0`).
- C wrappers: `sys_call` plus `sys_malloc`/`sys_calloc`/`sys_realloc`/`sys_free`/`sys_shutdown`/`sys_reboot`/`sys_getpid` (`include/os/syscall.h`).

## System call numbers

| Nr | Name | Arguments | Returns | Purpose |
|----|------|-----------|---------|---------|
| 1 | `SYS_WRITE` | `ebx` = `const char *` (NUL-terminated string pointer) | always 0 | Writes a string to VGA and serial (`vga_print` + `serial_print`; no length argument, NUL-terminated). **Output only happens when the caller's PID equals `ring3_foreground_pid()`**; calls from background processes or kernel context are silently dropped. |
| 2 | `SYS_READ` | `ebx` = `char *buf`, `ecx` = `size` | always 0 | Reads one line from the keyboard into `buf` (via `gets`, with echo). Writes at most `size-1` chars plus NUL; `size <= 1` produces an empty string. Blocks until a newline arrives. |
| 3 | `SYS_GETCHAR` | none | char | Blocks until a keyboard character is available (`getchar` → `keyboard_wait_key`), looping until a nonzero char; effectively never returns 0. |
| 4 | `SYS_CLEAR` | none | 0 | Clears the screen (`vga_clear`). |
| 5 | `SYS_REBOOT` | none | does not return | ACPI reboot. |
| 6 | `SYS_TIME` | none | Unix timestamp | `rtc_read_time` + `rtc_to_unix_timestamp`; returns the current time. |
| 7 | `SYS_MEMORY` | `ebx` = operation code, see below | see below | User-heap memory operations. |
| 8 | `SYS_POWER` | `ebx` = operation code, see below | 0 | Power operations. |
| 9 | `SYS_GETPID` | none | current Ring 3 PID | Returns 0 when called from kernel context. |

## `SYS_MEMORY` (7) sub-operation codes — `ebx`

| Code | Name | Arguments | Returns |
|------|------|-----------|---------|
| 1 | `MEMORY_MALLOC` | `ecx` = byte count | user-heap pointer, 0 on failure; `size == 0` returns 0 |
| 2 | `MEMORY_CALLOC` | `ecx` = element count, `edx` = element size | zeroed user-heap pointer, 0 on failure; returns 0 if `ecx*edx` overflows |
| 3 | `MEMORY_REALLOC` | `ecx` = pointer, `edx` = new byte count | new pointer, 0 on failure; `ptr == 0` behaves like malloc, `size == 0` behaves like free |
| 4 | `MEMORY_FREE` | `ecx` = pointer | 0; `NULL` or out-of-range pointers are silently ignored |

Unknown `SYS_MEMORY`/`SYS_POWER` sub-operation codes return 0.

User heap range: `0x01000000`–`0x40000000` (`USER_HEAP_START`/`USER_HEAP_END`), allocated and mapped page-by-page as USER pages; each block has a `struct user_block` header (magic `0x55534552`), and freed blocks go on a free list for reuse while the block's first page stays mapped.

## `SYS_POWER` (8) sub-operation codes — `ebx`

| Code | Name | Behavior |
|------|------|----------|
| 1 | `POWER_SHUTDOWN` | ACPI shutdown |
| 2 | `POWER_REBOOT` | ACPI reboot |

## Known limitations (to resolve before stabilizing the ABI)

- **No user-pointer validation**: `SYS_WRITE`/`SYS_READ`/`SYS_MEMORY` dereference caller-supplied pointers directly, without range checks or kernel-address rejection. An illegal pointer from Ring 3 triggers a page fault that kills the process; an illegal pointer in kernel context leads to a kernel page-fault panic (covered by the `kfault.elf` test).
- **No ABI versioning**: call numbers are bare integers with no version field.
- **Non-uniform error semantics**: undefined call numbers return `-1`, memory-operation failures return 0, and successful calls return 0/pointer/timestamp depending on the call; callers must check each one.
- **Blocking calls do not yield the CPU**: IRQ0 still fires during `SYS_READ`/`SYS_GETCHAR`, but `ring3_timer_switch` sees a kernel-mode frame and returns without switching, so other Ring 3 processes cannot be scheduled until the call returns. The keyboard wait itself is woken normally by IRQ1 (the trap gate preserves IF).
- **`SYS_READ`/`SYS_GETCHAR` only talk to the keyboard**: no file/console abstraction.
- **No file, directory, or device-access interfaces**; no process create/exit/wait calls.
- `SYS_WRITE`'s foreground/background suppression is implicit; callers cannot choose an output channel.
