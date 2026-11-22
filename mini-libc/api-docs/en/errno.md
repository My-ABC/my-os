# `errno.h`

`extern int errno` — a single global (no TLS; Ring 3 is
single-threaded per process). Set by library wrappers on failure;
raw `syscallN` never sets it. Constants mirror Linux values:
`EPERM ENOENT ESRCH EINTR EIO ENXIO E2BIG ENOEXEC EBADF ECHILD
EAGAIN ENOMEM EACCES EFAULT EBUSY EEXIST EXDEV ENODEV ENOTDIR
EISDIR EINVAL ENFILE EMFILE ENOTTY ETXTBSY EFBIG ENOSPC ESPIPE
EROFS EMLINK EPIPE EDOM ERANGE ...` (see header for the full list).
