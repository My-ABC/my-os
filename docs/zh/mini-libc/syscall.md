# `sys/syscall.h` — 原始系统调用 ABI

直接调用内核的逃生通道。ABI 约定：`eax` = 系统调用类，`ebx` =
子操作，`ecx`/`edx`/`esi` = 参数，`int 0x80`，结果在 `eax`。

优先使用其他头文件中的类型化封装——裸 op 绕过 `errno` 和库级
校验。

## 原始封装

实现在 `syscall.S`（cdecl）：

### `int syscall0(unsigned klass, unsigned op)`

- **参数**：`klass` — 系统调用类（`SYS_*`）；`op` — 子操作。
- **返回值**：内核原始返回值。
- **详细说明**：不设置参数寄存器。

### `int syscall1(unsigned klass, unsigned op, unsigned a1)`

- **参数**：`klass`、`op`、`a1` — 第一个参数。
- **返回值**：内核原始返回值。
- **详细说明**：—

### `int syscall2(unsigned klass, unsigned op, unsigned a1, unsigned a2)`

- **参数**：`klass`、`op`、`a1`、`a2`。
- **返回值**：内核原始返回值。
- **详细说明**：—

### `int syscall3(unsigned klass, unsigned op, unsigned a1, unsigned a2, unsigned a3)`

- **参数**：`klass`、`op`、`a1`–`a3`。
- **返回值**：内核原始返回值。
- **详细说明**：—

## 系统调用类

| 常量 | 值 | 子操作 |
|---|---|---|
| `SYS_IO` | 1 | 控制台 I/O |
| `SYS_TIME` | 2 | 时间/睡眠 |
| `SYS_MEMORY` | 3 | 用户堆 |
| `SYS_POWER` | 4 | 关机/重启 |
| `SYS_PROCESS` | 5 | 进程控制 |
| `SYS_FS` | 6 | FAT32 文件系统 |
| `SYS_SIG` | 7 | 信号 |

## 子操作常量

- `SYS_IO`：`IO_WRITE` 1、`IO_GETCHAR` 2、`IO_READ` 3、
  `IO_CLEAR` 4、`IO_CURSOR_GET` 5、`IO_CURSOR_SET` 6、
  `IO_CONSOLE_INFO` 7。
- `SYS_TIME`：`TIME_GET` 1、`TIME_TO` 2、`TIME_SLEEP` 3。
- `SYS_MEMORY`：`MEMORY_MALLOC` 1、`MEMORY_CALLOC` 2、
  `MEMORY_REALLOC` 3、`MEMORY_FREE` 4。
- `SYS_POWER`：`PWR_SHUTDOWN` 1、`PWR_REBOOT` 2。
- `SYS_PROCESS`：`PROC_GETPID` 1、`PROC_EXIT` 2、`PROC_YIELD` 3、
  `PROC_LIST` 4、`PROC_SET_FG` 5、`PROC_EXEC` 6、
  `PROC_RUN_DEMO` 7、`PROC_RUN_USERCODE` 8。
- `SYS_FS`：`FS_OPEN` 1、`FS_CLOSE` 2、`FS_READ` 3、`FS_WRITE` 4、
  `FS_UNLINK` 5、`FS_MKDIR` 6、`FS_RMDIR` 7、`FS_STAT` 8、
  `FS_GETCWD` 9、`FS_CHDIR` 10、`FS_RENAME` 11、`FS_READDIR` 12。
- `SYS_SIG`：`SIG_KILL` 1、`SIG_SIGNAL` 2、`SIG_RETURN` 3。

## 辅助常量

- 打开模式：`SYSFS_O_RDONLY` 0、`SYSFS_O_RDWR` 1。
- `PROC_LIST` 记录：12 字节 `{pid, state, is_foreground}`；
  状态 `PROC_STATE_UNUSED` 0、`PROC_STATE_READY` 1、
  `PROC_STATE_RUNNING` 2、`PROC_STATE_DEAD` 3。
