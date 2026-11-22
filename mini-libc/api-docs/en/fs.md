# `fs.h` — FAT32 fd API + `FILE`

## fd API
`int fs_open(const char *path, int mode)` — `FS_O_RDONLY`,
`FS_O_RDWR` (RDWR creates missing files; no `O_CREAT`/`O_TRUNC`).
`int fs_close(int fd)` `int fs_read(int fd, void *buf, unsigned size)`
`int fs_write(int fd, const void *buf, unsigned size)`
`int fs_unlink(const char *path)` `int fs_mkdir(const char *path)`
`int fs_rmdir(const char *path)` `int fs_rename(const char *old, const char *new)`
`int fs_stat(const char *path)` — size, `-2` directory, `<0` error.
`int fs_getcwd(char *buf, unsigned size)` `int fs_chdir(const char *path)`
`int fs_readdir(const char *path, void *buf, unsigned size)` —
records `'-'`/`'d'` + `' '` + name + `'\n'`; returns byte count.

## FILE
`typedef struct { int fd, eof, err, kind, ungot; } FILE` —
shared with stdio; `EOF` is defined here (`-1`).
