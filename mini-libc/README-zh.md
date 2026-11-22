# mini-libc

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/lang-C-yellow.svg)]()
[![Freestanding](https://img.shields.io/badge/freestanding-no_libc-lightgrey.svg)]()

MyOS Ring 3 用户程序的轻量 freestanding C 库。
以 MIT 协议发布——见 [LICENSE](LICENSE)。
每个源文件均带有 `SPDX-License-Identifier: MIT` 标记。

English version: [README.md](README.md)

## 用途

mini-libc 封装 MyOS 的 `int 0x80`「大类 + 子操作」syscall ABI，
让用户程序可以用 C 编写而不必手写汇编。
完整 ABI 参考见 `docs/zh/04-syscall-abi.md`。

需要支持 C11 的 freestanding 编译器（如 `i686-elf-gcc`，配合
`-m32 -ffreestanding -nostdlib`）。

## 目录结构

| 路径 | 内容 |
|------|------|
| `include/sys/syscall.h` | ABI 常量：`SYS_*` 大类、全部子操作号、`SYSFS_O_*` 打开模式、`PROC_STATE_*` 状态值，以及 `syscall0`–`syscall3` 声明。与 `include/os/syscall.h` 对齐。 |
| `include/stdint.h` `stddef.h` `stdbool.h` `limits.h` `float.h` | freestanding 基础头文件：定宽整数类型、`size_t`/`ptrdiff_t`/`NULL`/`offsetof`、`bool`、整数与浮点极限值 |
| `include/string.h` `ctype.h` | 字符串/内存与字符分类函数声明 |
| `include/time.h` | `time`/`gmtime`/`localtime`/`mktime`/`strftime`/`difftime`/`clock`/`sleep`/`usleep`（本地时间 UTC+8） |
| `include/errno.h` | `errno` 全局量与常用 `E*` 常量（与 Linux 取值一致） |
| `include/fs.h` | 基于 `SYS_FS` 的 FAT32 文件/目录 API：`fs_open`/`fs_close`/`fs_read`/`fs_write`/`fs_unlink`/`fs_mkdir`/`fs_rmdir`/`fs_stat`/`fs_getcwd`/`fs_chdir`/`fs_rename`/`fs_readdir`，以及极简无缓冲 `FILE`（`fs_fopen`/`fs_fclose`/`fs_fread`/`fs_fwrite`/`fs_fgetc`/`fs_fputc`/`fs_feof`/`fs_ferror`/`fs_clearerr`） |
| `include/unistd.h` `sys/types.h` | 进程/fd/路径 API：`_exit`/`getpid`/`getppid`/`sched_yield`/`fork`/`execve`，`read`/`write`/`close`，`unlink`/`rmdir`/`chdir`/`getcwd`/`access`/`lseek`；内核不支持的调用返回 `ENOSYS` |
| `include/stdlib.h` | `exit`/`_Exit`/`abort`/`atexit`，基于 `SYS_MEMORY` 的 `malloc`/`calloc`/`realloc`/`free`/`aligned_alloc`（16 字节头，8 字节基础对齐；C11 约束校验，违例 EINVAL），`atoi`/`atol`/`strtol`/`strtoul`，`rand`/`srand`，`qsort`/`bsearch`，`abs`/`labs`/`div`/`ldiv`，`getenv` |
| `include/stdio.h` `stdarg.h` | `printf`/`sprintf`/`snprintf`/`puts`/`putchar`/`scanf`/`sscanf`/`getchar`、`FILE*` 流（`stdin`/`stdout`，`fopen`/`fclose`/`fread`/`fwrite`/`fgetc`/`fputc`/`fputs`/`fgets`/`fprintf`/`fscanf`/`feof`/`ferror`/`clearerr`/`fflush`）与 `va_list` 内建封装 |
| `syscall.S` | `int 0x80` 原始封装 `syscall0`–`syscall3`（cdecl 约定） |
| `src/crt0.S` | ELF 入口点 `_start`：构造 `argc`/`argv`/`envp`，调用 `main`，经 `PROC_EXIT` 退出 |
| `src/string.c` `ctype.c` `stdio.c` `errno.c` `time.c` `fs.c` `unistd.c` `stdlib.c` | `string.h`/`ctype.h`/`stdio.h`/`errno.h`/`time.h`/`fs.h`/`unistd.h`/`stdlib.h` 的 freestanding 实现 |
| `examples/hello.c` | 使用 syscall 封装的最小 Ring 3 hello-world |

## 调用内核

封装函数把 C 参数按 ABI 装入寄存器：

- `eax` = syscall 大类（`SYS_IO` … `SYS_SIG`）
- `ebx` = 子操作号
- `ecx` / `edx` / `esi` = 参数 1–3
- 返回值在 `eax`

```c
#include "sys/syscall.h"

/* IO_WRITE("hi\n") */
syscall1(SYS_IO, IO_WRITE, (unsigned)"hi\n");

/* FS_READ(fd, buf, size) */
syscall3(SYS_FS, FS_READ, fd, (unsigned)buf, size);
```

## 计划中的模块

见 `TODO-zh.md` 中 "Implement mini-libc / 实现 mini-libc" 条目：

- `stdlib.h` — 基于 `SYS_MEMORY` 的堆辅助（`malloc`/`free`/`calloc`/`realloc`）
- 基于 `SYS_FS` fd 的 `FILE*` 流（`fopen`/`fread`/`fwrite`/`fgets`），按需
- 高层封装（`open`/`read`/`write`/`exit` 风格 API）

## 构建说明

用 `make libmyc` 构建静态库，产物为 `build/libmyc.a`，包含
`syscall.S`、`crt0.S`、`string.c`、`ctype.c`。手动构建库和用户程序：

```sh
make libmyc

i686-elf-gcc -m32 -std=c11 -ffreestanding -fno-pie -fno-stack-protector \
    -O2 -nostdlib -I mini-libc/include -c program.c -o program.o
i686-elf-gcc -m32 -nostdlib -static -no-pie -Wl,-e,_start \
    -o program.elf program.o build/libmyc.a
```

产物是入口点为 `_start` 的静态 ELF32（`_start` 由归档中的 `crt0.o`
提供）；`crt0` 调用 `main`，把返回值作为 `PROC_EXIT` 参数传出，随后
不再返回。可通过 shell 的 `run` 命令或 `PROC_EXEC` syscall 加载。

## 开发分支

mini-libc 的开发固定在 `mini-libc` 分支——改动直接提交该分支，
在里程碑或按需时合入 `main`。
