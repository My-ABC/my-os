# mini-libc —— 用户态 C 库

`mini-libc/` 是面向 Ring 3 程序的轻量 freestanding C 库，采用 MIT 协议（`mini-libc/LICENSE`；每个源文件都带 `SPDX-License-Identifier: MIT` 标记）。
它封装 `int 0x80` 大类/子操作 syscall ABI，使用户程序可以用 C 而非纯汇编编写。
mini-libc 面向 **C11**，使用 freestanding 编译器构建（`i686-elf-gcc -m32 -ffreestanding -nostdlib`）。

English: [English version](../en/14-mini-libc.md)

## 目录结构

| 文件 | 内容 |
|------|------|
| `include/sys/syscall.h` | ABI 常量：`SYS_*` 大类、全部子操作号、`SYSFS_O_*` 模式、`PROC_STATE_*` 状态值 |
| `include/stdint.h` `stddef.h` `stdbool.h` `limits.h` `float.h` | freestanding 基础头文件 |
| `include/string.h` `ctype.h` | 字符串/内存与字符分类函数声明 |
| `include/errno.h` | `errno` 全局量与常用 `E*` 常量（与 Linux 取值一致） |
| `include/stdio.h` `stdarg.h` | `printf`/`sprintf`/`snprintf`/`puts`/`putchar`/`scanf`/`sscanf`/`getchar` 与 `va_list` 内建封装 |
| `syscall.S` | 原始 `int 0x80` 包装 `syscall0`–`syscall3`（cdecl） |
| `src/crt0.S` | `_start` 入口点：构造 `argc`/`argv`/`envp`，调用 `main`，经 `PROC_EXIT` 退出 |
| `src/string.c` `ctype.c` `stdio.c` `errno.c` | `string.h`/`ctype.h`/`stdio.h`/`errno.h` 的 freestanding 实现 |
| `examples/hello.c` | 最小 Ring 3 hello-world 示例 |

更多模块（`stdlib.h`、基于 `SYS_FS` fd 的 `FILE*` 流、高层封装）计划中——见 TODO "实现 mini-libc" 条目。

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

## stdio

`stdio.h` 覆盖控制台/字符串接口（暂无 `FILE*` 流）：

- 输出：`printf`、`sprintf`、`snprintf`、`puts`、`putchar`。支持 `%d` `%i` `%u` `%o` `%x` `%X` `%c` `%s` `%p` `%%`，以及 `-` `0` `+` 空格 `#` 标志、字段宽度和精度（可用 `*`）；长度修饰符接受但忽略。控制台输出先缓冲再经 `IO_WRITE` 冲刷。
- 输入：`scanf`、`sscanf`、`getchar`。支持 `%d` `%i` `%u` `%o` `%x` `%X` `%c` `%s` `%p` `%%`，以及 `*` 抑制赋值和字段宽度；控制台输入经 `IO_READ` 按行读入静态小缓冲区，带一个字符的 pushback。

`stdarg.h` 基于 GCC 内建实现 `va_list`/`va_start`/`va_arg`/`va_end`/`va_copy`。

## 构建说明

用 `make libmyc` 构建静态库，产物为 `build/libmyc.a`（包含
`syscall.S`、`crt0.S`、`string.c`、`ctype.c`）。然后编译程序并链接
该归档：

```sh
make libmyc

i686-elf-gcc -m32 -std=c11 -ffreestanding -fno-pie -fno-stack-protector \
    -O2 -nostdlib -I mini-libc/include -c program.c -o program.o
i686-elf-gcc -m32 -nostdlib -static -no-pie -Wl,-e,_start \
    -o program.elf program.o build/libmyc.a
```

归档中的 `crt0` 为 `main` 构造 `argc`/`argv`/`envp` 调用帧（内核启动
用户进程时栈为空），把 `main` 返回值作为 `PROC_EXIT` 参数传出，随后
不再返回。用 Shell 的 `run` 命令或 `PROC_EXEC` 装载生成的 ELF 运行。
