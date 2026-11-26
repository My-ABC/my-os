# `errno.h` — 错误码

## `extern int errno`

- **参数**：无——它是一个全局左值。
- **返回值**：不适用。
- **详细说明**：单一全局 `errno`（无 TLS——Ring 3 单线程）。
  仅在库代码显式设置的地方才有意义；裸 `syscallN` 封装直接返回
  内核错误码，不会写 `errno`。

## 错误常量

mini-libc 定义的 POSIX 风格取值：

| 常量 | 值 | 典型含义 |
|---|---|---|
| `EPERM` | 1 | 操作不允许 |
| `ENOENT` | 2 | 文件或目录不存在 |
| `ESRCH` | 3 | 进程不存在 |
| `EINTR` | 4 | 调用被中断 |
| `EIO` | 5 | I/O 错误 |
| `ENXIO` | 6 | 设备或地址不存在 |
| `E2BIG` | 7 | 参数列表过长 |
| `ENOEXEC` | 8 | 可执行格式错误 |
| `EBADF` | 9 | 非法文件描述符 |
| `ECHILD` | 10 | 无子进程 |
| `EAGAIN` | 11 | 资源暂时不可用 |
| `ENOMEM` | 12 | 内存不足 |
| `EACCES` | 13 | 权限不足 |
| `EFAULT` | 14 | 非法地址 |
| `ENOTBLK` | 15 | 非块设备 |
| `EBUSY` | 16 | 设备或资源忙 |
| `EEXIST` | 17 | 文件已存在 |
| `EXDEV` | 18 | 跨设备链接非法 |
| `ENODEV` | 19 | 设备不存在 |
| `ENOTDIR` | 20 | 不是目录 |
| `EISDIR` | 21 | 是目录 |
| `EINVAL` | 22 | 参数非法 |
| `ENFILE` | 23 | 系统打开文件数超限 |
| `EMFILE` | 24 | 进程打开文件数超限 |
| `ENOTTY` | 25 | 不当的 I/O 控制操作 |
| `ETXTBSY` | 26 | 文本文件忙 |
| `EFBIG` | 27 | 文件过大 |
| `ENOSPC` | 28 | 设备空间不足 |
| `ESPIPE` | 29 | 非法 seek |
| `EROFS` | 30 | 只读文件系统 |
| `EMLINK` | 31 | 链接数过多 |
| `EPIPE` | 32 | 管道断开 |
| `EDOM` | 33 | 数学参数超出定义域 |
| `ERANGE` | 34 | 结果超出范围 |
| `EDEADLK` | 35 | 资源死锁 |
| `ENAMETOOLONG` | 36 | 文件名过长 |
| `ENOLCK` | 37 | 无可用锁 |
| `ENOSYS` | 38 | 功能未实现 |
| `ENOTEMPTY` | 39 | 目录非空 |
