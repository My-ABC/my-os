# MyOS System Call ABI (current state)

This document records the actual behavior of the `int 0x80` system-call interface, for Ring 3 programs and future ABI stabilization work.
Definitions live in `include/os/syscall.h`, implementation in `src/os/syscall.c`, and the assembly entry stub is `syscall` in `boot/interrupt.asm`.

中文版本: [中文版](../zh/04-syscall-abi.md)

## Calling convention

- Trigger: user mode executes `int 0x80`. IDT gate `0x80` is a DPL=3 interrupt gate (`idt_set_entry(0x80, syscall, 0x08, 0xEE)` in `idt.c`).
- Assembly stub `syscall`: `pushad` → `call syscall_handler` → writes the return value back into the saved EAX → `popad` → `iret`.
- `syscall_handler()` reads registers via inline assembly:
  - `eax` = system call number
  - `ebx` = argument 1
  - `ecx` = argument 2
  - `edx` = argument 3
- Return value: `eax`. Undefined call numbers return `-1` (`0xFFFFFFFF`). **There is no errno; error semantics are not uniform.**
- C wrappers: `sys_call(call_number, arg1, arg2, arg3)` plus `sys_malloc`/`sys_calloc`/`sys_realloc`/`sys_free`/`sys_shutdown`/`sys_reboot`/`sys_getpid`.

## System call numbers

| Nr | Name | Arguments | Returns | Purpose |
|----|------|-----------|---------|---------|
| 1 | `SYS_WRITE` | `ebx` = `const char *` (NUL-terminated string pointer) | 0 | Writes a string to VGA and serial. **Output only happens when the caller's PID equals `ring3_foreground_pid()`**; calls from background processes are silently dropped. |
| 2 | `SYS_READ` | `ebx` = `char *buf`, `ecx` = `size` | 0 | Reads one line from the keyboard into `buf` (goes through `gets`, echoes input, at most `size-1` chars). |
| 3 | `SYS_GETCHAR` | none | char or 0 | Blocks until a key is pressed, returns one character (`keyboard_wait_key`). |
| 4 | `SYS_CLEAR` | none | 0 | Clears the screen (`vga_clear`). |
| 5 | `SYS_REBOOT` | none | does not return | ACPI reboot. |
| 6 | `SYS_TIME` | none | Unix timestamp (`rtc_to_unix_timestamp`) | Returns the current time. |
| 7 | `SYS_MEMORY` | `ebx` = operation code, see below | see below | User-heap memory operations. |
| 8 | `SYS_POWER` | `ebx` = operation code, see below | 0 | Power operations. |
| 9 | `SYS_GETPID` | none | current Ring 3 PID | Returns 0 when called from kernel context. |

## `SYS_MEMORY` (7) sub-operation codes — `ebx`

| Code | Name | Arguments | Returns |
|------|------|-----------|---------|
| 1 | `MEMORY_MALLOC` | `ecx` = byte count | user-heap pointer, 0 on failure |
| 2 | `MEMORY_CALLOC` | `ecx` = element count, `edx` = element size | zeroed user-heap pointer, 0 on failure; returns 0 if `ecx*edx` overflows |
| 3 | `MEMORY_REALLOC` | `ecx` = pointer, `edx` = new byte count | new pointer, 0 on failure |
| 4 | `MEMORY_FREE` | `ecx` = pointer | 0 |

User heap range: `0x01000000`–`0x40000000`, allocated and mapped page-by-page as USER pages; each block has a `struct user_block` header (magic `0x55534552`), and freed blocks go on a free list for reuse.

## `SYS_POWER` (8) sub-operation codes — `ebx`

| Code | Name | Behavior |
|------|------|----------|
| 1 | `POWER_SHUTDOWN` | ACPI shutdown |
| 2 | `POWER_REBOOT` | ACPI reboot |

## Known limitations (to resolve before stabilizing the ABI)

- **No user-pointer validation**: `SYS_WRITE`/`SYS_READ`/`SYS_MEMORY` dereference caller-supplied pointers directly, without range checks or kernel-address rejection.
- **No ABI versioning**: call numbers are bare integers with no version field.
- **Non-uniform error semantics**: some calls return 0, some -1, some pointers; callers must check each one.
- **`SYS_READ`/`SYS_GETCHAR` only talk to the keyboard**: no file/console abstraction.
- **No file, directory, or device-access interfaces** beyond time formatting; no process create/exit/wait calls.
- `SYS_WRITE`'s foreground/background suppression is implicit; callers cannot choose an output channel.
