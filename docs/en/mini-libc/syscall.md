# `sys/syscall.h` — raw syscall ABI

Escape hatch for calling the kernel directly. ABI convention:
`eax` = syscall class, `ebx` = sub-operation, `ecx`/`edx`/`esi` =
arguments, `int 0x80`, result in `eax`.

Prefer the typed wrappers in the other headers — raw ops bypass
`errno` and library-level validation.

## Raw wrappers

Implemented in `syscall.S` (cdecl):

### `int syscall0(unsigned klass, unsigned op)`

- **Parameters**: `klass` — syscall class (`SYS_*`); `op` —
  sub-operation.
- **Returns**: raw kernel return value.
- **Details**: No argument registers set.

### `int syscall1(unsigned klass, unsigned op, unsigned a1)`

- **Parameters**: `klass`, `op`, `a1` — first argument.
- **Returns**: raw kernel return value.
- **Details**: —

### `int syscall2(unsigned klass, unsigned op, unsigned a1, unsigned a2)`

- **Parameters**: `klass`, `op`, `a1`, `a2`.
- **Returns**: raw kernel return value.
- **Details**: —

### `int syscall3(unsigned klass, unsigned op, unsigned a1, unsigned a2, unsigned a3)`

- **Parameters**: `klass`, `op`, `a1`–`a3`.
- **Returns**: raw kernel return value.
- **Details**: —

## Syscall classes

| Constant | Value | Sub-operations |
|---|---|---|
| `SYS_IO` | 1 | console I/O |
| `SYS_TIME` | 2 | time/sleep |
| `SYS_MEMORY` | 3 | user heap |
| `SYS_POWER` | 4 | shutdown/reboot |
| `SYS_PROCESS` | 5 | process control |
| `SYS_FS` | 6 | FAT32 filesystem |
| `SYS_SIG` | 7 | signals |

## Sub-operation constants

- `SYS_IO`: `IO_WRITE` 1, `IO_GETCHAR` 2, `IO_READ` 3, `IO_CLEAR` 4,
  `IO_CURSOR_GET` 5, `IO_CURSOR_SET` 6, `IO_CONSOLE_INFO` 7.
- `SYS_TIME`: `TIME_GET` 1, `TIME_TO` 2, `TIME_SLEEP` 3.
- `SYS_MEMORY`: `MEMORY_MALLOC` 1, `MEMORY_CALLOC` 2,
  `MEMORY_REALLOC` 3, `MEMORY_FREE` 4.
- `SYS_POWER`: `PWR_SHUTDOWN` 1, `PWR_REBOOT` 2.
- `SYS_PROCESS`: `PROC_GETPID` 1, `PROC_EXIT` 2, `PROC_YIELD` 3,
  `PROC_LIST` 4, `PROC_SET_FG` 5, `PROC_EXEC` 6,
  `PROC_RUN_DEMO` 7, `PROC_RUN_USERCODE` 8.
- `SYS_FS`: `FS_OPEN` 1, `FS_CLOSE` 2, `FS_READ` 3, `FS_WRITE` 4,
  `FS_UNLINK` 5, `FS_MKDIR` 6, `FS_RMDIR` 7, `FS_STAT` 8,
  `FS_GETCWD` 9, `FS_CHDIR` 10, `FS_RENAME` 11, `FS_READDIR` 12.
- `SYS_SIG`: `SIG_KILL` 1, `SIG_SIGNAL` 2, `SIG_RETURN` 3.

## Supporting constants

- Open modes: `SYSFS_O_RDONLY` 0, `SYSFS_O_RDWR` 1.
- `PROC_LIST` record: 12 bytes `{pid, state, is_foreground}`;
  states `PROC_STATE_UNUSED` 0, `PROC_STATE_READY` 1,
  `PROC_STATE_RUNNING` 2, `PROC_STATE_DEAD` 3.
