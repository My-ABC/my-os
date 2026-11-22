# mini-libc — user-space C library

`mini-libc/` is a small freestanding C library for Ring 3 programs, licensed under MIT (`mini-libc/LICENSE`; every source file carries `SPDX-License-Identifier: MIT`).
It wraps the `int 0x80` class/sub-operation syscall ABI so user programs can be written in C instead of raw assembly.

中文版本: [中文版](../zh/14-mini-libc.md)

## Layout

| File | Contents |
|------|----------|
| `include/sys/syscall.h` | ABI constants: `SYS_*` classes, all sub-operation numbers, `SYSFS_O_*` modes, `PROC_STATE_*` values |
| `syscall.S` | Raw `int 0x80` wrappers `syscall0`–`syscall3` (cdecl) |
| `src/crt0.S` | `_start` entry point: builds `argc`/`argv`/`envp`, calls `main`, exits via `PROC_EXIT` |
| `examples/hello.c` | Minimal Ring 3 hello-world |

More modules (`string.h`, `stdlib.h`, `stdio.h`, high-level wrappers) are planned — see the TODO entry "Implement mini-libc".

## Calling the kernel

The wrappers marshal C arguments into the ABI registers:

- `eax` = syscall class (`SYS_IO`…`SYS_SIG`)
- `ebx` = sub-operation
- `ecx`/`edx`/`esi` = arguments 1–3
- return value in `eax`

```c
extern int syscall3(unsigned klass, unsigned op,
                    unsigned a1, unsigned a2, unsigned a3);

/* IO_WRITE("hi\n") */
syscall1(SYS_IO, IO_WRITE, (unsigned)"hi\n");
/* FS_READ(fd, buf, size) */
syscall3(SYS_FS, FS_READ, fd, (unsigned)buf, size);
```

Class and sub-operation constants live in `mini-libc/include/sys/syscall.h`, which mirrors `include/os/syscall.h`; user programs include it directly instead of keeping their own copy.

## Build notes

Compile `crt0.S`, `syscall.S`, and the program sources together into a
static ELF32 with entry point `_start`, e.g.:

```sh
i686-elf-gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
    -nostdlib -I mini-libc/include \
    mini-libc/src/crt0.S mini-libc/syscall.S program.c -o program.elf
```

`crt0` hands `main` a fabricated `argc`/`argv`/`envp` frame (the kernel
starts user processes with an empty stack), forwards `main`'s return value
to `PROC_EXIT`, and never returns. Load the resulting ELF with the shell
`run` command or `PROC_EXEC`.
