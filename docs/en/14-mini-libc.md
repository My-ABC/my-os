# mini-libc — user-space C library

`mini-libc/` is a small freestanding C library for Ring 3 programs, licensed under MIT (`mini-libc/LICENSE`; every source file carries `SPDX-License-Identifier: MIT`).
It wraps the `int 0x80` class/sub-operation syscall ABI so user programs can be written in C instead of raw assembly.

中文版本: [中文版](../zh/14-mini-libc.md)

## Layout

| File | Contents |
|------|----------|
| `syscall.S` | Raw `int 0x80` wrappers `syscall0`–`syscall3` (cdecl) |

More modules (`string.h`, `stdlib.h`, `stdio.h`, `_start`) are planned — see the TODO entry "Implement mini-libc".

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

Class and sub-operation constants mirror `include/os/syscall.h`; user programs should keep their own copy or a generated header since kernel headers are not part of the user ABI surface.

## Build notes

Compile with the same freestanding toolchain used for the kernel, e.g.:

```sh
i686-elf-gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
    -nostdlib -c mini-libc/syscall.S -o syscall.o
```

Link the result into a static ELF32 whose entry point is `_start`; load it with the shell `run` command or `PROC_EXEC`.
