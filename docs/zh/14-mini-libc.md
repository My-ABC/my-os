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
| `include/time.h` | 基于 `SYS_TIME` 的 `time`/`gmtime`/`localtime`/`mktime`/`strftime`/`difftime`/`clock`/`sleep`/`usleep`（本地时间固定 UTC+8） |
| `include/errno.h` | `errno` 全局量与常用 `E*` 常量（与 Linux 取值一致） |
| `include/fs.h` | 基于 `SYS_FS` 的 FAT32 文件/目录 API：`fs_open`/`fs_close`/`fs_read`/`fs_write`/`fs_unlink`/`fs_mkdir`/`fs_rmdir`/`fs_stat`/`fs_getcwd`/`fs_chdir`/`fs_rename`/`fs_readdir`，以及极简无缓冲 `FILE`（`fs_fopen`/`fs_fclose`/`fs_fread`/`fs_fwrite`/`fs_fgetc`/`fs_fputc`/`fs_feof`/`fs_ferror`/`fs_clearerr`） |
| `include/unistd.h` `sys/types.h` | 基于 `SYS_PROCESS`/`SYS_FS` 的进程/fd/路径 API：`_exit`/`getpid`/`sched_yield`/`execve`，`read`/`write`/`close`，`unlink`/`rmdir`/`chdir`/`getcwd`/`access`/`lseek`；内核不支持的调用返回 `ENOSYS` |
| `include/assert.h` | `assert(e)`——失败时向 stdout 打印表达式/文件/函数/行号后 `abort()`；`NDEBUG` 下编译为空 |
| `include/inttypes.h` | `PRId*`/`PRIi*`/`PRIo*`/`PRIu*`/`PRIx*`/`PRIX*` + `SCN*` 长度宏，`imaxabs`/`imaxdiv`/`strtoimax`/`strtoumax`（LP32 下 `intmax_t`=int） |
| `include/setjmp.h` | 基于 callee-saved 寄存器 + esp/eip 的 `setjmp`/`longjmp`（`jmp_buf` = 6 个 int）；`longjmp(env,0)` 返回 1 |
| `include/stdlib.h` | `exit`/`_Exit`/`abort`/`atexit`，基于 `SYS_MEMORY` 的 `malloc`/`calloc`/`realloc`/`free`/`aligned_alloc`（16 字节头，8 字节基础对齐；C11 约束校验，违例 EINVAL），`atoi`/`atol`/`strtol`/`strtoul`，`rand`/`srand`，`qsort`/`bsearch`，`abs`/`labs`/`div`/`ldiv`，`getenv` |
| `include/stdio.h` `stdarg.h` | `printf`/`sprintf`/`snprintf`/`puts`/`putchar`/`scanf`/`sscanf`/`getchar`、`FILE*` 流（`stdin`/`stdout`，`fopen`/`fclose`/`fread`/`fwrite`/`fgetc`/`fputc`/`fputs`/`fgets`/`fprintf`/`fscanf`/`feof`/`ferror`/`clearerr`/`fflush`）与 `va_list` 内建封装 |
| `syscall.S` `setjmp.S` | `int 0x80` 原始包装 `syscall0`–`syscall3`（cdecl）；`setjmp`/`longjmp` 寄存器/栈保存恢复桩 |
| `src/crt0.S` | `_start` 入口点：构造 `argc`/`argv`/`envp`，调用 `main`，经 `PROC_EXIT` 退出 |
| `src/string.c` `ctype.c` `stdio.c` `errno.c` `time.c` `fs.c` `unistd.c` `stdlib.c` `assert.c` `inttypes.c` | `string.h`/`ctype.h`/`stdio.h`/`errno.h`/`time.h`/`fs.h`/`unistd.h`/`stdlib.h` 的 freestanding 实现 |
| `examples/hello.c` | 最小 Ring 3 hello-world 示例 |

更多模块（`signal.h`、宽字符支持、`FILE*` 缓冲、高层封装）计划中——见 TODO "实现 mini-libc" 条目。

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
