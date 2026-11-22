# mini-libc — user-space C library

`mini-libc/` is a small freestanding C library for Ring 3 programs, licensed under MIT (`mini-libc/LICENSE`; every source file carries `SPDX-License-Identifier: MIT`).
It wraps the `int 0x80` class/sub-operation syscall ABI so user programs can be written in C instead of raw assembly.
mini-libc targets **C11** and is built with a freestanding compiler (`i686-elf-gcc -m32 -ffreestanding -nostdlib`).

中文版本: [中文版](../zh/14-mini-libc.md)

## API reference

`docs/en/mini-libc/` and `docs/zh/mini-libc/` contains the bilingual per-header
API reference (one chapter per public header); keep it
in sync with header changes.

## Tests

`test/mini-libc/` holds a QEMU integration suite
(`run_tests.py`): test ELFs grouped by subsystem — `core/`
(string, ctype, stdlib, inttypes, setjmp, assert), `stdio/`,
`fs/`, `os/` (errno, time, unistd), and `negative/` (assert
failure). The runner compiles each test against
`build/libmyc.a`, injects the ELFs plus fixture files into a
FAT32 image (max 6 ELFs per disk — the 64 MiB image root
directory holds ~16 entries), boots one QEMU session per
chunk, and verifies `DONE-<tag> fails=0` on serial. Run it via
`make test-libmyc`.

## Layout

| File | Contents |
|------|----------|
| `include/sys/syscall.h` | ABI constants: `SYS_*` classes, all sub-operation numbers, `SYSFS_O_*` modes, `PROC_STATE_*` values |
| `include/stdint.h` `stddef.h` `stdbool.h` `limits.h` `float.h` | Freestanding base headers |
| `include/string.h` `ctype.h` | String/memory and character-classification declarations |
| `include/time.h` | `time`/`gmtime`/`localtime`/`mktime`/`strftime`/`difftime`/`clock`/`sleep`/`usleep` over `SYS_TIME` (local time fixed at UTC+8) |
| `include/errno.h` | `errno` global plus common `E*` constants (Linux-compatible values) |
| `include/fs.h` | FAT32 file/directory API over `SYS_FS`: `fs_open`/`fs_close`/`fs_read`/`fs_write`/`fs_unlink`/`fs_mkdir`/`fs_rmdir`/`fs_stat`/`fs_getcwd`/`fs_chdir`/`fs_rename`/`fs_readdir`, plus a minimal unbuffered `FILE` (`fs_fopen`/`fs_fclose`/`fs_fread`/`fs_fwrite`/`fs_fgetc`/`fs_fputc`/`fs_feof`/`fs_ferror`/`fs_clearerr`) |
| `include/unistd.h` `sys/types.h` | Process/fd/path API over `SYS_PROCESS`/`SYS_FS`: `_exit`/`getpid`/`sched_yield`/`execve`, `read`/`write`/`close`, `unlink`/`rmdir`/`chdir`/`getcwd`/`access`/`lseek`; unsupported calls return `ENOSYS` |
| `include/assert.h` | `assert(e)` — prints expr/file/func/line to stdout then `abort()`; compiled out under `NDEBUG` |
| `include/signal.h` | `signal`/`kill`/`raise`, `sig_t`, `SIGINT`/`SIGKILL`, `SIG_DFL`/`SIG_IGN` over `SYS_SIG` (SIGINT catchable, SIGKILL fatal; no mask API) |
| `include/console.h` | `con_getchar`/`con_clear`/`con_cursor_get`/`con_cursor_set` over `SYS_IO` (line-editing primitives) |
| `include/process.h` | `proc_list`/`proc_set_foreground`/`proc_foreground` over `SYS_PROCESS` (`PROC_LIST`/`PROC_SET_FG`) |
| `include/inttypes.h` | `PRId*`/`PRIi*`/`PRIo*`/`PRIu*`/`PRIx*`/`PRIX*` + `SCN*` length macros, `imaxabs`/`imaxdiv`/`strtoimax`/`strtoumax` (`intmax_t`=int on LP32) |
| `include/stdnoreturn.h` | `noreturn` macro → `_Noreturn` (header-only) |
| `include/setjmp.h` | `setjmp`/`longjmp` over callee-saved regs + esp/eip (`jmp_buf` = 6 ints); `longjmp(env,0)` returns 1 |
| `include/stdlib.h` | `exit`/`_Exit`/`abort`/`atexit`, `malloc`/`calloc`/`realloc`/`free`/`aligned_alloc` over `SYS_MEMORY` (16-byte header, 8-byte base alignment; C11 constraint checks -> EINVAL), `atoi`/`atol`/`strtol`/`strtoul`, `rand`/`srand`, `qsort`/`bsearch`, `abs`/`labs`/`div`/`ldiv`, `getenv` |
| `include/stdio.h` `stdarg.h` | `printf`/`sprintf`/`snprintf`/`puts`/`putchar`/`scanf`/`sscanf`/`getchar`, `FILE*` streams (`stdin`/`stdout`, `fopen`/`fclose`/`fread`/`fwrite`/`fgetc`/`fputc`/`fputs`/`fgets`/`fprintf`/`fscanf`/`feof`/`ferror`/`clearerr`/`fflush`), plus `va_list` builtins |
| `syscall.S` `setjmp.S` | Raw `int 0x80` wrappers `syscall0`–`syscall3` (cdecl); `setjmp`/`longjmp` register/stack save-restore stubs |
| `src/crt0.S` | `_start` entry point: builds `argc`/`argv`/`envp`, calls `main`, exits via `PROC_EXIT` |
| `src/string.c` `ctype.c` `stdio.c` `errno.c` `time.c` `fs.c` `unistd.c` `stdlib.c` `assert.c` `inttypes.c` `signal.c` `process.c` `console.c` | Freestanding implementations of `string.h`/`ctype.h`/`stdio.h`/`errno.h`/`time.h`/`fs.h`/`unistd.h`/`stdlib.h` |
| `examples/hello.c` | Minimal Ring 3 hello-world |

More modules (`signal.h`, wide-char support, `FILE*` buffering, high-level wrappers) are planned — see the TODO entry "Implement mini-libc".

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

`stdio.h` covers the console/string surface (no `FILE*` streams yet):

- Output: `printf`, `sprintf`, `snprintf`, `puts`, `putchar`. Conversions `%d` `%i` `%u` `%o` `%x` `%X` `%c` `%s` `%p` `%%` with flags `-` `0` `+` space `#`, field width and precision (`*` allowed); length modifiers are accepted and ignored. Console output is buffered and flushed through `IO_WRITE`.
- Input: `scanf`, `sscanf`, `getchar`. Conversions `%d` `%i` `%u` `%o` `%x` `%X` `%c` `%s` `%p` `%%` with `*` suppression and field width; console input is read line-by-line through `IO_READ` into a small static buffer with one-character pushback.

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
