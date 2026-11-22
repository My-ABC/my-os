# mini-libc

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/lang-C-yellow.svg)]()
[![Freestanding](https://img.shields.io/badge/freestanding-no_libc-lightgrey.svg)]()

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

## API reference

`../docs/en/mini-libc/` and `../docs/zh/mini-libc/` — bilingual
per-header API reference (one chapter per public header).

## Tests

`test/mini-libc/` (repo-level `test/`) contains a QEMU integration suite (`run_tests.py`):
per-module test ELFs under `core/` (string, ctype, stdlib,
inttypes, setjmp, assert), `stdio/`, `fs/`, `os/` (errno, time,
unistd), and `negative/` (assert-failure abort). The runner compiles
each test against `build/libmyc.a`, injects them plus fixture files
into a FAT32 image (<=6 ELFs per disk — the 64 MiB image's root
directory holds ~16 entries), boots QEMU once per chunk, and checks
serial output for `DONE-<tag> fails=0`. Run: `make test-libmyc` (or `python3
test/mini-libc/run_tests.py`).

## Layout

| Path | Contents |
|------|----------|
| `include/sys/syscall.h` | ABI constants: `SYS_*` classes, all sub-operation numbers, `SYSFS_O_*` open modes, `PROC_STATE_*` values, and `syscall0`–`syscall3` declarations. Mirrors `include/os/syscall.h`. |
| `include/stdint.h` `stddef.h` `stdbool.h` `limits.h` `float.h` | Freestanding base headers: fixed-width types, `size_t`/`ptrdiff_t`/`NULL`/`offsetof`, `bool`, integer and float limits |
| `include/string.h` `ctype.h` | String/memory and character-classification declarations |
| `include/time.h` | `time`/`gmtime`/`localtime`/`mktime`/`strftime`/`difftime`/`clock`/`sleep`/`usleep` (UTC+8 local time) |
| `include/errno.h` | `errno` global plus common `E*` constants (Linux-compatible values) |
| `include/fs.h` | FAT32 file/directory API over `SYS_FS`: `fs_open`/`fs_close`/`fs_read`/`fs_write`/`fs_unlink`/`fs_mkdir`/`fs_rmdir`/`fs_stat`/`fs_getcwd`/`fs_chdir`/`fs_rename`/`fs_readdir`, plus a minimal unbuffered `FILE` (`fs_fopen`/`fs_fclose`/`fs_fread`/`fs_fwrite`/`fs_fgetc`/`fs_fputc`/`fs_feof`/`fs_ferror`/`fs_clearerr`) |
| `include/unistd.h` `sys/types.h` | Process/fd/path API: `_exit`/`getpid`/`getppid`/`sched_yield`/`fork`/`execve`, `read`/`write`/`close`, `unlink`/`rmdir`/`chdir`/`getcwd`/`access`/`lseek`; unsupported calls return `ENOSYS` |
| `include/assert.h` | `assert(e)` — prints expr/file/func/line to stdout then `abort()`; compiled out under `NDEBUG` |
| `include/signal.h` | `signal`/`kill`/`raise`, `sig_t`, `SIGINT`/`SIGKILL`, `SIG_DFL`/`SIG_IGN` over `SYS_SIG` (SIGINT catchable, SIGKILL fatal; no mask API) |
| `include/console.h` | `con_getchar`/`con_clear`/`con_cursor_get`/`con_cursor_set` over `SYS_IO` (line-editing primitives) |
| `include/process.h` | `proc_list`/`proc_set_foreground`/`proc_foreground` over `SYS_PROCESS` (`PROC_LIST`/`PROC_SET_FG`) |
| `include/inttypes.h` | `PRId*`/`PRIi*`/`PRIo*`/`PRIu*`/`PRIx*`/`PRIX*` + `SCN*` length macros, `imaxabs`/`imaxdiv`/`strtoimax`/`strtoumax` (`intmax_t`=int on LP32) |
| `include/stdnoreturn.h` | `noreturn` macro → `_Noreturn` (header-only) |
| `include/setjmp.h` | `setjmp`/`longjmp` over callee-saved regs + esp/eip (`jmp_buf` = 6 ints); `longjmp(env,0)` returns 1 |
| `include/stdlib.h` | `exit`/`_Exit`/`abort`/`atexit`, `malloc`/`calloc`/`realloc`/`free`/`aligned_alloc` over `SYS_MEMORY` (16-byte header, 8-byte base alignment; C11 constraint checks -> EINVAL), `atoi`/`atol`/`strtol`/`strtoul`, `rand`/`srand`, `qsort`/`bsearch`, `abs`/`labs`/`div`/`ldiv`, `getenv` |
| `include/stdio.h` `stdarg.h` | `printf`/`sprintf`/`snprintf`/`puts`/`putchar`/`scanf`/`sscanf`/`getchar`/`getline`, `FILE*` streams (`stdin`/`stdout`, `fopen`/`fclose`/`fread`/`fwrite`/`fgetc`/`fputc`/`fputs`/`fgets`/`fprintf`/`fscanf`/`feof`/`ferror`/`clearerr`/`fflush`), plus `va_list` builtins |
| `syscall.S` `setjmp.S` | Raw `int 0x80` wrappers `syscall0`–`syscall3` (cdecl); `setjmp`/`longjmp` register/stack save-restore stubs |
| `src/crt0.S` | ELF entry point `_start`: fabricates `argc`/`argv`/`envp`, calls `main`, exits via `PROC_EXIT` |
| `src/string.c` `ctype.c` `stdio.c` `errno.c` `time.c` `fs.c` `unistd.c` `stdlib.c` `assert.c` `inttypes.c` `signal.c` `process.c` `console.c` | Freestanding implementations of the `string.h`/`ctype.h`/`stdio.h`/`errno.h`/`time.h`/`fs.h`/`unistd.h`/`stdlib.h` facilities |
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
