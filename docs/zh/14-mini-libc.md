# mini-libc —— 用户态 C 库

`mini-libc/` 是面向 Ring 3 程序的轻量 freestanding C 库，采用 MIT 协议（`mini-libc/LICENSE`；每个源文件都带 `SPDX-License-Identifier: MIT` 标记）。
它封装 `int 0x80` 大类/子操作 syscall ABI，使用户程序可以用 C 而非纯汇编编写。

English: [English version](../en/14-mini-libc.md)

## 目录结构

| 文件 | 内容 |
|------|------|
| `include/sys/syscall.h` | ABI 常量：`SYS_*` 大类、全部子操作号、`SYSFS_O_*` 模式、`PROC_STATE_*` 状态值 |
| `syscall.S` | 原始 `int 0x80` 包装 `syscall0`–`syscall3`（cdecl） |

更多模块（`string.h`、`stdlib.h`、`stdio.h`、`_start`）计划中——见 TODO "实现 mini-libc" 条目。

## 调用内核

包装函数把 C 参数装入 ABI 寄存器：

- `eax` = 系统调用大类（`SYS_IO`…`SYS_SIG`）
- `ebx` = 子操作号
- `ecx`/`edx`/`esi` = 参数 1–3
- 返回值在 `eax`

```c
extern int syscall3(unsigned klass, unsigned op,
                    unsigned a1, unsigned a2, unsigned a3);

/* IO_WRITE("hi\n") */
syscall1(SYS_IO, IO_WRITE, (unsigned)"hi\n");
/* FS_READ(fd, buf, size) */
syscall3(SYS_FS, FS_READ, fd, (unsigned)buf, size);
```

大类与子操作常量在 `mini-libc/include/sys/syscall.h` 中，与 `include/os/syscall.h` 保持一致；用户程序直接包含它即可，无需自备副本。

## 构建说明

使用与内核相同的 freestanding 工具链，例如：

```sh
i686-elf-gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
    -nostdlib -c mini-libc/syscall.S -o syscall.o
```

将产物链接进入口为 `_start` 的静态 ELF32，用 Shell 的 `run` 命令或 `PROC_EXEC` 装载运行。
