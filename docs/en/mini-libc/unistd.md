# `unistd.h` — process control and fd-level I/O

POSIX-ish process control, fd-level I/O, and path operations over
the MyOS syscall ABI. Only what the kernel supports is implemented;
functions with no backing syscall return `-1` with `errno = ENOSYS`.
`sleep()`/`usleep()` live in `time.h`.

Types come from `sys/types.h`: `pid_t` (int), `ssize_t` (int),
`off_t` (int32_t).

## Process control

### `_Noreturn void _exit(int status)`

- **Parameters**: `status` — exit status.
- **Returns**: never returns.
- **Details**: Immediate `PROC_EXIT`; same as `_Exit` in
  `stdlib.h` — no `atexit` handlers, no stdio flush.

### `pid_t getpid(void)`

- **Parameters**: none.
- **Returns**: current process id (positive).
- **Details**: `PROC_GETPID` wrapper.

### `pid_t getppid(void)`

- **Parameters**: none.
- **Returns**: always `-1`, `errno = ENOSYS`.
- **Details**: The kernel tracks no parent pid.

### `int sched_yield(void)`

- **Parameters**: none.
- **Returns**: `0` or `< 0`.
- **Details**: `PROC_YIELD` — yields the CPU to another ready Ring 3
  process. The syscall performs a signal check, so pending fatal
  signals are delivered here.

### `pid_t fork(void)`

- **Parameters**: none.
- **Returns**: always `-1`, `errno = ENOSYS`.
- **Details**: No fork in the ABI — spawn via `execve`/`run`.

### `int execve(const char *path, char *const argv[], char *const envp[])`

- **Parameters**: `path` — FAT32 path of an ELF32 executable;
  `argv`, `envp` — accepted for signature compatibility but
  **ignored** (the kernel ABI takes only the path).
- **Returns**: only on failure (`< 0`); on success the current
  process image is replaced.
- **Details**: `PROC_EXEC` — loads the ELF32 image and replaces the
  address space.

## fd-level I/O

Same as `fs_read`/`fs_write`/`fs_close` in `fs.h`, except fds 0–2
route to the console when they were not opened as files (see below).

### `ssize_t read(int fd, void *buf, size_t count)`

- **Parameters**: `fd`; `buf` — destination; `count` — max bytes.
- **Returns**: bytes read (short at EOF), or `-1` on error.
- **Details**: `fd == 0` maps to the console when fd 0 was not opened
  as a file: `IO_READ` returns **one whole line** (no `'\n'`,
  NUL-terminated, up to `count-1` chars); a return of `0` means an
  empty line was submitted, not EOF. Do not mix with
  `fgets(stdin)`/`getchar()` — they share the same kernel line
  buffer. Other fds go to `fs_read`.

### `ssize_t write(int fd, const void *buf, size_t count)`

- **Parameters**: `fd`; `buf` — source; `count` — bytes.
- **Returns**: bytes written, or `-1` on error.
- **Details**: `fd == 1 || fd == 2` maps to the console when that fd
  was not opened as a file: output goes through `IO_WRITE` (VGA +
  COM3), chunked at 4096 bytes; embedded NUL bytes truncate a chunk
  early (console is text-only), and `fd == 2` shares the same stream
  (no separate stderr). Other fds go to `fs_write`.

### `int close(int fd)`

- **Parameters**: `fd` — descriptor.
- **Returns**: `0` or `-1` (`EBADF`).
- **Details**: —

## Path operations

### `int unlink(const char *path)`

- **Parameters**: `path` — file to delete.
- **Returns**: `0` or `-1`.
- **Details**: Same as `fs_unlink`.

### `int rmdir(const char *path)`

- **Parameters**: `path` — empty directory to remove.
- **Returns**: `0` or `-1`.
- **Details**: Same as `fs_rmdir`.

### `int chdir(const char *path)`

- **Parameters**: `path` — target directory.
- **Returns**: `0` or `-1`.
- **Details**: Same as `fs_chdir`; changes the shared cwd.

### `char *getcwd(char *buf, size_t size)`

- **Parameters**: `buf` — destination; `size` — capacity.
- **Returns**: `buf` on success, `NULL` on error/overflow.
- **Details**: NUL-terminated cwd; same data as `fs_getcwd`.

### `int access(const char *path, int mode)`

- **Parameters**: `path`; `mode` — ignored beyond existence.
- **Returns**: `0` if the path exists, `-1` otherwise.
- **Details**: Existence check only (`F_OK` semantics); any `mode`
  value is accepted.

### `off_t lseek(int fd, off_t offset, int whence)`

- **Parameters**: `fd`, `offset`, `whence` — standard seek args.
- **Returns**: always `-1`, `errno = ENOSYS`.
- **Details**: The kernel ABI has no seek support.
