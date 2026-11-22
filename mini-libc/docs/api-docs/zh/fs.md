# `fs.h` —— FAT32 fd API + `FILE`

## fd API
`int fs_open(const char *path, int mode)` —— `FS_O_RDONLY`、
`FS_O_RDWR`（RDWR 可创建缺失文件；无 `O_CREAT`/`O_TRUNC`）。
`int fs_close(int fd)` `int fs_read(int fd, void *buf, unsigned size)`
`int fs_write(int fd, const void *buf, unsigned size)`
`int fs_unlink(const char *path)` `int fs_mkdir(const char *path)`
`int fs_rmdir(const char *path)` `int fs_rename(const char *old, const char *new)`
`int fs_stat(const char *path)` —— 大小、目录 `-2`、错误 `<0`。
`int fs_getcwd(char *buf, unsigned size)` `int fs_chdir(const char *path)`
`int fs_readdir(const char *path, void *buf, unsigned size)` ——
记录 `'-'`/`'d'` + `' '` + 名字 + `'\n'`；返回字节数。

## FILE
`typedef struct { int fd, eof, err, kind, ungot; } FILE` ——
与 stdio 共用；`EOF` 定义于此（`-1`）。
