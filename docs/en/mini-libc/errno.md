# `errno.h` — error codes

## `extern int errno`

- **Parameters**: none — it is a global lvalue.
- **Returns**: n/a.
- **Details**: Single global `errno` (no TLS — Ring 3 is
  single-threaded). It is only meaningful where library code sets it
  explicitly; raw `syscallN` wrappers return kernel codes directly
  and do not touch `errno`.

## Error constants

POSIX-style values defined by mini-libc:

| Constant | Value | Typical meaning |
|---|---|---|
| `EPERM` | 1 | operation not permitted |
| `ENOENT` | 2 | no such file or directory |
| `ESRCH` | 3 | no such process |
| `EINTR` | 4 | interrupted |
| `EIO` | 5 | I/O error |
| `ENXIO` | 6 | no such device/address |
| `E2BIG` | 7 | argument list too long |
| `ENOEXEC` | 8 | exec format error |
| `EBADF` | 9 | bad file descriptor |
| `ECHILD` | 10 | no child processes |
| `EAGAIN` | 11 | resource temporarily unavailable |
| `ENOMEM` | 12 | out of memory |
| `EACCES` | 13 | permission denied |
| `EFAULT` | 14 | bad address |
| `ENOTBLK` | 15 | not a block device |
| `EBUSY` | 16 | device or resource busy |
| `EEXIST` | 17 | file exists |
| `EXDEV` | 18 | invalid cross-device link |
| `ENODEV` | 19 | no such device |
| `ENOTDIR` | 20 | not a directory |
| `EISDIR` | 21 | is a directory |
| `EINVAL` | 22 | invalid argument |
| `ENFILE` | 23 | too many open files in system |
| `EMFILE` | 24 | too many open files |
| `ENOTTY` | 25 | inappropriate I/O control |
| `ETXTBSY` | 26 | text file busy |
| `EFBIG` | 27 | file too large |
| `ENOSPC` | 28 | no space left on device |
| `ESPIPE` | 29 | illegal seek |
| `EROFS` | 30 | read-only file system |
| `EMLINK` | 31 | too many links |
| `EPIPE` | 32 | broken pipe |
| `EDOM` | 33 | math argument out of domain |
| `ERANGE` | 34 | result out of range |
| `EDEADLK` | 35 | resource deadlock |
| `ENAMETOOLONG` | 36 | file name too long |
| `ENOLCK` | 37 | no locks available |
| `ENOSYS` | 38 | function not implemented |
| `ENOTEMPTY` | 39 | directory not empty |
