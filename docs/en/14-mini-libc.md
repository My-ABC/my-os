# mini-libc — user-space C library

`mini-libc/` is a small freestanding C library for Ring 3 programs, licensed under MIT (`mini-libc/LICENSE`; every source file carries `SPDX-License-Identifier: MIT`).
It wraps the `int 0x80` class/sub-operation syscall ABI so user programs can be written in C instead of raw assembly.
mini-libc targets **C11** and is built with a freestanding compiler (`i686-elf-gcc -m32 -ffreestanding -nostdlib`).

中文版本: [中文版](../zh/14-mini-libc.md)

## Layout

| File | Contents |
|------|----------|
| `include/sys/syscall.h` | ABI constants: `SYS_*` classes, all sub-operation numbers, `SYSFS_O_*` modes, `PROC_STATE_*` values |
| `include/stdint.h` `stddef.h` `stdbool.h` `limits.h` `float.h` | Freestanding base headers |
| `include/string.h` `ctype.h` | String/memory and character-classification declarations |
| `include/stdio.h` `stdarg.h` | Minimal `printf`/`scanf` plus `va_list` builtins |
| `syscall.S` | Raw `int 0x80` wrappers `syscall0`–`syscall3` (cdecl) |
| `src/crt0.S` | `_start` entry point: builds `argc`/`argv`/`envp`, calls `main`, exits via `PROC_EXIT` |
| `src/string.c` `ctype.c` `stdio.c` | Freestanding implementations of `string.h`/`ctype.h`/`stdio.h` |
| `examples/hello.c` | Minimal Ring 3 hello-world |

More modules (`stdlib.h`, wider `stdio.h` coverage, high-level wrappers) are planned — see the TODO entry "Implement mini-libc".

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

## stdio

`stdio.h` is intentionally minimal — only `printf` and `scanf`:

- `printf` supports `%d` `%i` `%u` `%x` `%X` `%s` `%c` `%p` and `%%`; output is buffered and flushed through `IO_WRITE`.
- `scanf` supports `%d` `%i` `%u` `%x` `%s` `%c`, literal characters, and whitespace skipping; input is read line-by-line through `IO_READ` into a small static buffer with one-character pushback.

`stdarg.h` provides `va_list`/`va_start`/`va_arg`/`va_end`/`va_copy` on top of the GCC builtins.

## Build notes

Build the static library with `make libmyc`, which produces
`build/libmyc.a` (containing `syscall.S`, `crt0.S`, `string.c`,
`ctype.c`). Then compile the program and link against the archive:

```sh
make libmyc

i686-elf-gcc -m32 -std=c11 -ffreestanding -fno-pie -fno-stack-protector \
    -O2 -nostdlib -I mini-libc/include -c program.c -o program.o
i686-elf-gcc -m32 -nostdlib -static -no-pie -Wl,-e,_start \
    -o program.elf program.o build/libmyc.a
```

`crt0` (inside the archive) hands `main` a fabricated
`argc`/`argv`/`envp` frame (the kernel starts user processes with an
empty stack), forwards `main`'s return value to `PROC_EXIT`, and never
returns. Load the resulting ELF with the shell `run` command or
`PROC_EXEC`.
