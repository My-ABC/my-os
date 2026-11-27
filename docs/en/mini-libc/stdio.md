# `stdio.h` — formatted and stream I/O

Formatted console I/O and `FILE` streams over the MyOS syscall ABI.
There is no real buffering yet — console output goes to
`SYS_IO/IO_WRITE` (VGA + serial mirror) and console input comes from
`SYS_IO/IO_READ` lines.

## Format support

- `printf` family: `%d %i %u %o %x %X %c %s %p %%` with flags
  `- 0 + space #`, field width and precision (`*` allowed).
  Length modifiers `h/hh/l/ll` are accepted and ignored — `int` is
  assumed. No floating-point conversions.
- `scanf` family: `%d %i %u %o %x %X %c %s %p %%` with optional
  assignment suppression `*` and field width; length modifiers
  ignored.

## Console output

### `int printf(const char *fmt, ...)`

- **Parameters**: `fmt` — format string; varargs per conversion.
- **Returns**: number of characters written, or `< 0` on error.
- **Details**: Writes to the console via `IO_WRITE` (VGA text/VBE +
  COM1 serial mirror).

### `int sprintf(char *buf, const char *fmt, ...)`

- **Parameters**: `buf` — destination buffer (caller-sized);
  `fmt`/varargs as `printf`.
- **Returns**: characters written excluding NUL, or `< 0`.
- **Details**: **No bounds checking** — use `snprintf` when the
  size is known.

### `int snprintf(char *buf, size_t n, const char *fmt, ...)`

- **Parameters**: `buf`, `n` — buffer and capacity (including NUL);
  `fmt`/varargs as `printf`.
- **Returns**: characters that *would* have been written excluding
  NUL, or `< 0`.
- **Details**: Output is truncated to `n-1` chars + NUL.

### `int puts(const char *s)`

- **Parameters**: `s` — NUL-terminated string.
- **Returns**: nonnegative on success, `< 0` on error.
- **Details**: Writes `s` followed by a newline.

### `int putchar(int c)`

- **Parameters**: `c` — character.
- **Returns**: `c` on success, `EOF`/`< 0` on error.
- **Details**: Single-character console write.

## Console input

### `int scanf(const char *fmt, ...)`

- **Parameters**: `fmt` — format string; varargs are pointers that
  receive converted values.
- **Returns**: number of successful assignments, or `EOF` if input
  fails before the first assignment.
- **Details**: Reads a console line via `IO_READ`. Do **not** mix
  with `getchar`/`getline` on the same stream — they consume
  different kernel paths.

### `int sscanf(const char *s, const char *fmt, ...)`

- **Parameters**: `s` — input string; `fmt`/varargs as `scanf`.
- **Returns**: number of assignments, or `EOF`.
- **Details**: Parses from memory — no console I/O involved.

### `int getchar(void)`

- **Parameters**: none.
- **Returns**: next character as `int`, or `EOF` on error/signal
  abort.
- **Details**: Reads one byte via `IO_GETCHAR`; blocks when the
  buffer is empty. Never returns `'\n'`: stdin is line-buffered and
  the kernel `IO_READ` strips the newline, so `in_getch`
  synthesizes a virtual `'\n'` at each line end that `getchar`
  filters back out — this keeps `fgets`/`scanf` line semantics
  working while preserving the "no newline" contract.

### `int getline(char *buf, int size)`

- **Parameters**: `buf` — destination; `size` — capacity including
  NUL.
- **Returns**: line length excluding NUL, or `-1` on error/overflow.
- **Details**: Reads one console line via `IO_READ` directly. Must
  not be mixed with `getchar`/`scanf` (same reason as `scanf`).

## FILE streams

`stdin` and `stdout` are console streams (`stderr` is deferred).
`fopen()` returns **unbuffered** `FILE*` streams over the `fs.h`
fd layer — every call is a syscall.

```c
extern FILE *stdin;
extern FILE *stdout;
```

### `FILE *fopen(const char *path, const char *mode)`

- **Parameters**: `path` — FAT32 path; `mode` — `"r"`, `"w"`,
  `"a"`, `"r+"`, ...
- **Returns**: `FILE *` on success, `NULL` on error.
- **Details**: No buffering; see `fs.h` for mode semantics.

### `int fclose(FILE *f)`

- **Parameters**: `f` — stream from `fopen`.
- **Returns**: `0` or `< 0`.
- **Details**: Flushes (no-op when unbuffered) and closes the fd.

### `size_t fread(void *buf, size_t size, size_t count, FILE *f)`

- **Parameters**: `buf` — destination; `size` — element size;
  `count` — element count; `f` — stream.
- **Returns**: number of complete elements read (short at EOF).
- **Details**: —

### `size_t fwrite(const void *buf, size_t size, size_t count, FILE *f)`

- **Parameters**: `buf` — source; `size`, `count` — element
  geometry; `f` — stream.
- **Returns**: number of elements written.
- **Details**: —

### `int fgetc(FILE *f)` / `int fputc(int c, FILE *f)`

- **Parameters**: `f` — stream; `c` — character for `fputc`.
- **Returns**: `fgetc` — byte as `int` or `EOF`; `fputc` — `c` or
  `EOF`.
- **Details**: —

### `int fputs(const char *s, FILE *f)`

- **Parameters**: `s` — string; `f` — stream.
- **Returns**: nonnegative on success, `EOF` on error.
- **Details**: —

### `char *fgets(char *buf, int size, FILE *f)`

- **Parameters**: `buf` — destination; `size` — capacity;
  `f` — stream.
- **Returns**: `buf` on success, `NULL` on EOF/error before any
  char.
- **Details**: Reads at most `size-1` chars, keeps the trailing
  newline. On `stdin` the kernel `IO_READ` consumes the real
  newline without storing it, so mini-libc appends a virtual `'\n'`
  per line — `fgets` sees normal line-buffered semantics.

### `int fprintf(FILE *f, const char *fmt, ...)` / `int fscanf(FILE *f, const char *fmt, ...)`

- **Parameters**: `f` — stream; `fmt`/varargs as `printf`/`scanf`.
- **Returns**: same conventions as `printf`/`scanf`.
- **Details**: —

### `int feof(FILE *f)` / `int ferror(FILE *f)` / `void clearerr(FILE *f)` / `int fflush(FILE *f)`

- **Parameters**: `f` — stream.
- **Returns**: `feof`/`ferror` — nonzero flag; `fflush` — `0`
  (no-op, unbuffered); `clearerr` — nothing.
- **Details**: eof/error flags are sticky until `clearerr`.
