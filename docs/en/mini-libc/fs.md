# `fs.h` — FAT32 filesystem API

Thin wrappers over the kernel's FAT32 fd table via `SYS_FS`. File
descriptors are small non-negative integers owned by the kernel;
there is **no per-process fd table**, so fds are global across all
Ring 3 tasks.

## Constants

| Constant | Value | Meaning |
|---|---|---|
| `FS_O_RDONLY` | 0 | read-only open mode |
| `FS_O_RDWR` | 1 | read-write open mode (creates missing files; no separate `O_CREAT`) |
| `FS_FILE_REG` | 0 | `FILE.kind` — regular FAT32 file |
| `FS_FILE_STDIN` | 1 | `FILE.kind` — console stdin stream |
| `FS_FILE_STDOUT` | 2 | `FILE.kind` — console stdout stream |
| `EOF` | -1 | end-of-file indicator |

## fd API

### `int fs_open(const char *path, int mode)`

- **Parameters**: `path` — FAT32 path (relative to cwd or absolute);
  `mode` — `FS_O_RDONLY` or `FS_O_RDWR`.
- **Returns**: fd `>= 0`, or `< 0` on error (`ENOENT`, `EACCES`,
  `EMFILE`, ...).
- **Details**: `FS_O_RDWR` creates the file if it does not exist;
  there is no `O_TRUNC` — opening an existing file does not shrink
  it.

### `int fs_close(int fd)`

- **Parameters**: `fd` — descriptor from `fs_open`.
- **Returns**: `0` on success, `< 0` (`EBADF`) on bad fd.
- **Details**: —

### `int fs_read(int fd, void *buf, unsigned size)`

- **Parameters**: `fd`; `buf` — destination buffer; `size` —
  maximum bytes.
- **Returns**: bytes actually read (short read at EOF), or `< 0` on
  error.
- **Details**: —

### `int fs_write(int fd, const void *buf, unsigned size)`

- **Parameters**: `fd`; `buf` — source buffer; `size` — bytes.
- **Returns**: bytes written, or `< 0` on error.
- **Details**: —

### `int fs_unlink(const char *path)`

- **Parameters**: `path` — file to delete.
- **Returns**: `0` or `< 0`.
- **Details**: Deletes a file; directories must use `fs_rmdir`.

### `int fs_mkdir(const char *path)`

- **Parameters**: `path` — directory to create.
- **Returns**: `0` or `< 0` (`EEXIST` if present).
- **Details**: Single level only — parents must exist.

### `int fs_rmdir(const char *path)`

- **Parameters**: `path` — directory to remove.
- **Returns**: `0` or `< 0`.
- **Details**: Only removes **empty** directories (`ENOTEMPTY`
  otherwise).

### `int fs_stat(const char *path)`

- **Parameters**: `path` — file or directory.
- **Returns**: encoded in the return value — `>= 0` regular file
  size in bytes; `-2` path is a directory; `< 0` (other) error /
  not found.
- **Details**: No `struct stat` — the result is deliberately packed
  into the return code.

### `int fs_getcwd(char *buf, unsigned size)`

- **Parameters**: `buf` — destination buffer; `size` — capacity.
- **Returns**: bytes written (NUL-terminated), or `< 0`.
- **Details**: Copies the current working directory into `buf`.

### `int fs_chdir(const char *path)`

- **Parameters**: `path` — target directory.
- **Returns**: `0` or `< 0` (`ENOENT`/`ENOTDIR`).
- **Details**: Changes the process-wide cwd.

### `int fs_rename(const char *old_path, const char *new_path)`

- **Parameters**: `old_path`, `new_path` — source and destination.
- **Returns**: `0` or `< 0`.
- **Details**: Moves/renames a file or directory.

### `int fs_readdir(const char *path, void *buf, unsigned size)`

- **Parameters**: `path` — directory; `buf` — output buffer;
  `size` — capacity.
- **Returns**: byte count written, or `< 0`.
- **Details**: Format: one record per line — kind byte `'-'`
  (file) or `'d'` (dir), a space, the name, then `'\n'`. Entries
  include `.`/`..`? (kernel-dependent — check `ls` behavior).

## FILE layer

`FILE` is shared with `stdio.h`:

```c
typedef struct {
    int fd;         /* kernel fd; unused for console kinds */
    int eof;        /* set after a short/EOF read */
    int err;        /* set on kernel errors */
    int kind;       /* FS_FILE_* */
    int ungot;      /* one-char pushback for scanf (-1 = none) */
} FILE;
```

These are the fd-layer stream functions (`fs_f*`); they have the
same signatures and semantics as the `stdio.h` `f*` functions but
only handle `FS_FILE_REG` files (no console streams — those live in
`stdio.h`).

### `FILE *fs_fopen(const char *path, const char *mode)`

- **Parameters**: `path` — FAT32 path; `mode` — `"r"`/`"w"`/`"a"`/
  `"r+"` etc.
- **Returns**: `FILE *` or `NULL` on error.
- **Details**: Unbuffered — every operation is a syscall.
  `"a"` positions the fd offset at EOF once at open time (not
  POSIX per-write append; see `stdio.h` `fopen` for the caveat).

### `int fs_fclose(FILE *f)`

- **Parameters**: `f` — stream from `fs_fopen`.
- **Returns**: `0` or `< 0`.
- **Details**: Closes the underlying fd.

### `size_t fs_fread(void *buf, size_t size, size_t count, FILE *f)`

- **Parameters**: `buf` — destination; `size` — element size;
  `count` — element count; `f` — stream.
- **Returns**: number of complete elements read (short at EOF).
- **Details**: —

### `size_t fs_fwrite(const void *buf, size_t size, size_t count, FILE *f)`

- **Parameters**: `buf` — source; `size`, `count` — element
  geometry; `f` — stream.
- **Returns**: number of elements written.
- **Details**: —

### `int fs_fgetc(FILE *f)`

- **Parameters**: `f` — stream.
- **Returns**: next byte as `int`, or `EOF`.
- **Details**: Sets `eof`/`err` flags accordingly.

### `int fs_fputc(int c, FILE *f)`

- **Parameters**: `c` — character; `f` — stream.
- **Returns**: `c` on success, `EOF` on error.
- **Details**: —

### `int fs_feof(FILE *f)` / `int fs_ferror(FILE *f)`

- **Parameters**: `f` — stream.
- **Returns**: nonzero if the eof/error flag is set.
- **Details**: Flags are sticky until `fs_clearerr`.

### `void fs_clearerr(FILE *f)`

- **Parameters**: `f` — stream.
- **Returns**: nothing.
- **Details**: Clears `eof` and `err` flags.
