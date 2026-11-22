# mini-libc

MyOS Ring 3 用户程序的轻量 freestanding C 库。
以 MIT 协议发布——见 [LICENSE](LICENSE)。
每个源文件均带有 `SPDX-License-Identifier: MIT` 标记。

English version: [README.md](README.md)

## 用途

mini-libc 封装 MyOS 的 `int 0x80`「大类 + 子操作」syscall ABI，
让用户程序可以用 C 编写而不必手写汇编。
完整 ABI 参考见 `docs/zh/04-syscall-abi.md`。

## 目录结构

| 路径 | 内容 |
|------|------|
| `include/sys/syscall.h` | ABI 常量：`SYS_*` 大类、全部子操作号、`SYSFS_O_*` 打开模式、`PROC_STATE_*` 状态值，以及 `syscall0`–`syscall3` 声明。与 `include/os/syscall.h` 对齐。 |
| `syscall.S` | `int 0x80` 原始封装 `syscall0`–`syscall3`（cdecl 约定） |

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

- `string.h` / `stdlib.h` — 纯函数与基于 `SYS_MEMORY` 的堆辅助
- `stdio.h` — 基于 `IO_WRITE` 的精简 `printf`/`puts`
- `_start` — ELF 入口点、栈初始化、`main` 分发
- 高层封装（`open`/`read`/`write`/`malloc` 风格 API）

## 构建说明

使用与内核相同的 freestanding 工具链：

```sh
i686-elf-gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
    -nostdlib -c mini-libc/syscall.S -o syscall.o
```

用户程序用 `-I mini-libc/include` 编译，链接为入口点为 `_start` 的
静态 ELF32。可通过 shell 的 `run` 命令或 `PROC_EXEC` syscall 加载。

## 开发分支

mini-libc 的开发固定在 `mini-libc` 分支——改动直接提交该分支，
在里程碑或按需时合入 `main`。
