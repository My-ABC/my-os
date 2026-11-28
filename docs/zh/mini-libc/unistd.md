# `unistd.h` — 进程控制与 fd 级 I/O

基于 MyOS syscall ABI 的类 POSIX 进程控制、fd 级 I/O 和路径操
作。只实现内核支持的功能；无对应系统调用的函数返回 `-1` 且
`errno = ENOSYS`。`sleep()`/`usleep()` 在 `time.h` 中。

类型来自 `sys/types.h`：`pid_t`（int）、`ssize_t`（int）、
`off_t`（int32_t）。

## 进程控制

### `_Noreturn void _exit(int status)`

- **参数**：`status` — 退出状态。
- **返回值**：永不返回。
- **详细说明**：立即 `PROC_EXIT`；与 `stdlib.h` 的 `_Exit`
  相同——不运行 `atexit` 处理函数，不刷新 stdio。

### `pid_t getpid(void)`

- **参数**：无。
- **返回值**：当前进程 id（正数）。
- **详细说明**：`PROC_GETPID` 封装。

### `pid_t getppid(void)`

- **参数**：无。
- **返回值**：总是 `-1`，`errno = ENOSYS`。
- **详细说明**：内核不跟踪父进程 pid。

### `int sched_yield(void)`

- **参数**：无。
- **返回值**：`0` 或 `< 0`。
- **详细说明**：`PROC_YIELD`——把 CPU 让给另一个就绪的 Ring 3
  进程。该系统调用会做信号检查，挂起的致命信号会在此投递。

### `pid_t fork(void)`

- **参数**：无。
- **返回值**：总是 `-1`，`errno = ENOSYS`。
- **详细说明**：ABI 中没有 fork——用 `execve`/`run` 创建进程。

### `int execve(const char *path, char *const argv[], char *const envp[])`

- **参数**：`path` — ELF32 可执行文件的 FAT32 路径；`argv`、
  `envp` — 仅为签名兼容而接受，**被忽略**（内核 ABI 只取路径）。
- **返回值**：仅失败时返回（`< 0`）；成功时替换当前进程映像。
- **详细说明**：`PROC_EXEC`——加载 ELF32 映像并替换地址空间。

## fd 级 I/O

语义与 `fs.h` 的 `fs_read`/`fs_write`/`fs_close` 一致，但 fd 0–2
在未被当作文件打开时会路由到控制台（见下）。

### `ssize_t read(int fd, void *buf, size_t count)`

- **参数**：`fd`；`buf` — 目标；`count` — 最多字节数。
- **返回值**：读取字节数（EOF 处短读），或 `-1`。
- **详细说明**：fd 0 未被当作文件打开时映射到控制台：`IO_READ`
  返回**整行**（无 `'\n'`、NUL 结尾、至多 `count-1` 字符）；返回
  `0` 表示提交了空行而非 EOF。勿与 `fgets(stdin)`/`getchar()` 混用
  ——它们共享同一内核行缓冲。其它 fd 走 `fs_read`。

### `ssize_t write(int fd, const void *buf, size_t count)`

- **参数**：`fd`；`buf` — 源；`count` — 字节数。
- **返回值**：写入字节数，或 `-1`。
- **详细说明**：fd 1/2 未被当作文件打开时映射到控制台：输出经
  `IO_WRITE`（VGA + COM3），按 4096 字节分块；内嵌 NUL 会提前截
  断一块（控制台仅文本），fd 2 与 fd 1 共用同一流（无独立
  stderr）。其它 fd 走 `fs_write`。

### `int close(int fd)`

- **参数**：`fd` — 描述符。
- **返回值**：`0` 或 `-1`（`EBADF`）。
- **详细说明**：—

## 路径操作

### `int unlink(const char *path)`

- **参数**：`path` — 要删除的文件。
- **返回值**：`0` 或 `-1`。
- **详细说明**：同 `fs_unlink`。

### `int rmdir(const char *path)`

- **参数**：`path` — 要删除的空目录。
- **返回值**：`0` 或 `-1`。
- **详细说明**：同 `fs_rmdir`。

### `int chdir(const char *path)`

- **参数**：`path` — 目标目录。
- **返回值**：`0` 或 `-1`。
- **详细说明**：同 `fs_chdir`；改变共享的 cwd。

### `char *getcwd(char *buf, size_t size)`

- **参数**：`buf` — 目标；`size` — 容量。
- **返回值**：成功返回 `buf`，错误/溢出返回 `NULL`。
- **详细说明**：NUL 结尾的 cwd；数据同 `fs_getcwd`。

### `int access(const char *path, int mode)`

- **参数**：`path`；`mode` — 除存在性外被忽略。
- **返回值**：路径存在返回 `0`，否则 `-1`。
- **详细说明**：仅存在性检查（`F_OK` 语义）；接受任意
  `mode` 值。

### `off_t lseek(int fd, off_t offset, int whence)`

- **参数**：`fd`、`offset`、`whence` — 标准 seek 参数。
- **返回值**：总是 `-1`，`errno = ENOSYS`。
- **详细说明**：内核 ABI 不支持 seek。
