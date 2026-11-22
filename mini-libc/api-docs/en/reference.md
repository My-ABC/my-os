# mini-libc API Reference (English)

Freestanding C11 library for MyOS Ring 3 programs. Link with
`build/libmyc.a` (see `examples/hello.c` for the link recipe).
All functions are freestanding: no libc, no OS services beyond the
`int 0x80` syscall ABI.

## Base headers

| Header | Contents |
|---|---|
| `stdint.h` | `int8_t`–`int64_t`, `uint*_t`, `intptr_t`/`uintptr_t`, `intmax_t`/`uintmax_t`, least/fast variants, `INT*_MAX/MIN` |
| `stddef.h` | `size_t`, `ptrdiff_t`, `NULL`, `offsetof` |
| `stdbool.h` | `bool`, `true`, `false` |
| `limits.h` | `CHAR_BIT`, `SCHAR_MIN/MAX`, `UCHAR_MAX`, `SHRT_*`, `INT_*`, `UINT_*`, `LONG_*`, `ULONG_*`, `LLONG_*`, `ULLONG_*` |
| `float.h` | `FLT_*`, `DBL_*` (host values) |
| `stdarg.h` | `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy` |
| `stdnoreturn.h` | `noreturn` → `_Noreturn` |

## `string.h` / `ctype.h`

`memcpy memmove memset memcmp strlen strcmp strncmp strcpy strncpy
strcat strncat strchr strrchr strstr` — standard semantics.
`isalnum isalpha isblank iscntrl isdigit isgraph islower isprint
ispunct isspace isupper isxdigit toupper tolower` — C locale ASCII.

## `stdio.h` (+ `fs.h` for `FILE`/`EOF`)

Console + string output:
`printf sprintf snprintf puts putchar fprintf vfprintf`
Input: `scanf sscanf vscanf vfscanf getchar getline`
FILE streams (unbuffered):
`stdin stdout` (no `stderr`),
`fopen fclose fread fwrite fgetc fputc fputs fgets feof ferror
clearerr fflush`
Formats: `%d %i %u %o %x %X %c %s %p %%` with `- 0 + space #`
flags, width, precision, `*`; scanf adds `%[...]`? — **not
implemented**; length modifiers accepted but ignored; no 64-bit
conversion (`%lld` unsupported).

## `fs.h` — FAT32 fd + FILE layer

Open modes `FS_O_RDONLY` `FS_O_RDWR` (no O_CREAT — file must exist
for RDONLY; RDWR creates).
`fs_open fs_close fs_read fs_write fs_unlink fs_mkdir fs_rmdir
fs_stat fs_getcwd fs_chdir fs_rename fs_readdir`
`fs_stat` returns size, `-2` for directory, `<0` error.
`fs_readdir` records: `'-'`/`'d'` + `' '` + name + `'\n'`.
FILE: `fd eof err kind ungot`; `EOF` defined here.

## `unistd.h` + `sys/types.h`

`pid_t ssize_t off_t`.
`_exit getpid sched_yield execve` (PROC_EXEC),
`read write close` (fd level),
`unlink rmdir chdir getcwd access`.
`fork getppid lseek` → `-1`/`errno=ENOSYS` (no kernel support).

## `stdlib.h`

Heap: `malloc calloc realloc free aligned_alloc` — 16-byte header
layer, `malloc` is 8-byte aligned; `aligned_alloc` enforces C11
rules (power-of-two alignment, multiple of `sizeof(void*)`,
size % alignment == 0) else `EINVAL`; `malloc(0)` → `NULL`.
`atexit` (16 slots, LIFO, run by `exit` only),
`exit _Exit abort` (`_Noreturn`),
`atoi atol strtol strtoul` (base 0 auto-detect, `ERANGE`),
`rand srand` (xorshift32, `RAND_MAX=0x7FFFFFFF`),
`qsort bsearch abs labs div ldiv getenv` (always `NULL`).

## `errno.h`

`extern int errno` + `E*` constants (Linux-compatible values).
Set by library wrappers on failure; raw `syscallN` never sets it.

## `time.h`

`time gmtime localtime mktime strftime difftime sleep usleep
clock` — civil-from-days math, local time fixed UTC+8.

## `assert.h`

`assert(e)` → `__assert_fail(#e,__FILE__,__LINE__,__func__)`
prints to stdout then `abort()`; `NDEBUG` compiles it out.

## `setjmp.h`

`jmp_buf` = 6 ints (ebx/esi/edi/ebp/esp/eip);
`setjmp`/`longjmp` (`_Noreturn`), `longjmp(env,0)` returns 1.
Volatile locals across longjmp are indeterminate (C rule).

## `signal.h`

`sig_t sig_atomic_t SIG_ERR SIG_DFL SIG_IGN SIGINT=2 SIGKILL=9
NSIG=32`; `signal kill raise`.
Only SIGINT catchable (Ctrl+C), SIGKILL always fatal; no
sigprocmask/sigaction. Handler is plain `void h(int)`; return
resumes via kernel restorer.

## `process.h` + `sys/types.h`

`proc_info_t{pid,state,foreground}`, `PROC_ST_*`;
`proc_list proc_set_foreground proc_foreground`.
No fork/wait — each `run` is an independent process.

## `inttypes.h`

`PRId*/PRIi*/PRIo*/PRIu*/PRIx*/PRIX*` + `SCN*` for all stdint
widths (LP32: `int64_t`→`ll`, `intmax_t`=int);
`imaxabs imaxdiv strtoimax strtoumax`.

## `sys/syscall.h`

ABI constants only — prefer the wrappers above.
Classes `SYS_IO/TIME/MEMORY/POWER/PROCESS/FS/SIG`, sub-ops,
`SYSFS_O_*`, `PROC_STATE_*`.
