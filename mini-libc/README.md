# mini-libc

A small freestanding C library for Ring 3 user programs on MyOS.
Licensed under the MIT License — see [LICENSE](LICENSE).
Every source file carries `SPDX-License-Identifier: MIT`.

中文文档: [README-zh.md](README-zh.md)

## Purpose

mini-libc wraps the MyOS `int 0x80` class/sub-operation syscall ABI so
user programs can be written in C instead of raw assembly. See
`docs/en/04-syscall-abi.md` for the full ABI reference.

## Layout

| Path | Contents |
|------|----------|
| `include/sys/syscall.h` | ABI constants: `SYS_*` classes, all sub-operation numbers, `SYSFS_O_*` open modes, `PROC_STATE_*` values, and `syscall0`–`syscall3` declarations. Mirrors `include/os/syscall.h`. |
| `syscall.S` | Raw `int 0x80` wrappers `syscall0`–`syscall3` (cdecl) |
| `src/crt0.S` | ELF entry point `_start`: fabricates `argc`/`argv`/`envp`, calls `main`, exits via `PROC_EXIT` |
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

- `string.h` / `stdlib.h` — pure functions and heap helpers over `SYS_MEMORY`
- `stdio.h` — minimal `printf`/`puts` over `IO_WRITE`
- High-level wrappers (`open`/`read`/`write`/`malloc` style APIs)

## Build notes

Same freestanding toolchain as the kernel. Build a program by compiling
`crt0.S`, `syscall.S`, and the program sources together:

```sh
i686-elf-gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
    -nostdlib -I mini-libc/include \
    mini-libc/src/crt0.S mini-libc/syscall.S mini-libc/examples/hello.c \
    -o hello.elf
```

The result is a static ELF32 whose entry point is `_start`; `crt0` calls
`main`, forwards its return value as the `PROC_EXIT` argument, and never
returns. Compile user programs
with `-I mini-libc/include`. Load them via the shell `run` command or the
`PROC_EXEC` syscall.

## Development branch

mini-libc work happens on the dedicated `mini-libc` branch — commit changes
there directly and merge into `main` at milestones or on request.
