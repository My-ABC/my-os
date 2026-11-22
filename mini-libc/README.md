# mini-libc

A small freestanding C library for Ring 3 user programs on MyOS.
Licensed under the MIT License — see [LICENSE](LICENSE).
Every source file carries `SPDX-License-Identifier: MIT`.

中文文档: [README-zh.md](README-zh.md)

## Purpose

mini-libc wraps the MyOS `int 0x80` class/sub-operation syscall ABI so
user programs can be written in C instead of raw assembly. See
`docs/en/04-syscall-abi.md` for the full ABI reference.

Requires a C11-capable freestanding compiler (e.g. `i686-elf-gcc` with
`-m32 -ffreestanding -nostdlib`).

## Layout

| Path | Contents |
|------|----------|
| `include/sys/syscall.h` | ABI constants: `SYS_*` classes, all sub-operation numbers, `SYSFS_O_*` open modes, `PROC_STATE_*` values, and `syscall0`–`syscall3` declarations. Mirrors `include/os/syscall.h`. |
| `include/stdint.h` `stddef.h` `stdbool.h` `limits.h` `float.h` | Freestanding base headers: fixed-width types, `size_t`/`ptrdiff_t`/`NULL`/`offsetof`, `bool`, integer and float limits |
| `include/string.h` `ctype.h` | String/memory and character-classification declarations |
| `include/time.h` | `time`/`gmtime`/`localtime`/`mktime`/`strftime`/`difftime`/`clock`/`sleep`/`usleep` (UTC+8 local time) |
| `include/errno.h` | `errno` global plus common `E*` constants (Linux-compatible values) |
| `include/stdio.h` `stdarg.h` | `printf`/`sprintf`/`snprintf`/`puts`/`putchar`/`scanf`/`sscanf`/`getchar` plus `va_list` builtins |
| `syscall.S` | Raw `int 0x80` wrappers `syscall0`–`syscall3` (cdecl) |
| `src/crt0.S` | ELF entry point `_start`: fabricates `argc`/`argv`/`envp`, calls `main`, exits via `PROC_EXIT` |
| `src/string.c` `ctype.c` `stdio.c` `errno.c` `time.c` | Freestanding implementations of the `string.h`/`ctype.h`/`stdio.h`/`errno.h`/`time.h` facilities |
| `examples/hello.c` | Minimal Ring 3 hello-world using the syscall wrappers |

## Calling the kernel

The wrappers marshal C arguments into the ABI registers:

- `eax` = syscall class (`SYS_IO` … `SYS_SIG`)
- `ebx` = sub-operation
- `ecx` / `edx` / `esi` = arguments 1–3
- return value in `eax`

```c
#include "sys/syscall.h"

/* IO_WRITE("hi\n") */
syscall1(SYS_IO, IO_WRITE, (unsigned)"hi\n");

/* FS_READ(fd, buf, size) */
syscall3(SYS_FS, FS_READ, fd, (unsigned)buf, size);
```

## Planned modules

See the "Implement mini-libc" entry in `TODO.md`:

- `stdlib.h` — heap helpers over `SYS_MEMORY` (`malloc`/`free`/`calloc`/`realloc`)
- `FILE*` streams (`fopen`/`fread`/`fwrite`/`fgets`) over `SYS_FS` fds, if needed
- High-level wrappers (`open`/`read`/`write`/`exit` style APIs)

## Build notes

Build the static library with `make libmyc` — it produces
`build/libmyc.a` containing `syscall.S`, `crt0.S`, `string.c`, and
`ctype.c`. To build the library and a program by hand:

```sh
make libmyc

i686-elf-gcc -m32 -std=c11 -ffreestanding -fno-pie -fno-stack-protector \
    -O2 -nostdlib -I mini-libc/include -c program.c -o program.o
i686-elf-gcc -m32 -nostdlib -static -no-pie -Wl,-e,_start \
    -o program.elf program.o build/libmyc.a
```

The result is a static ELF32 whose entry point is `_start` (provided by
`crt0.o` inside the archive); `crt0` calls `main`, forwards its return
value as the `PROC_EXIT` argument, and never returns. Load it via the
shell `run` command or the `PROC_EXEC` syscall.

## Development branch

mini-libc work happens on the dedicated `mini-libc` branch — commit changes
there directly and merge into `main` at milestones or on request.
