# `fs.h` — FAT32 文件系统 API

通过 `SYS_FS` 对内核 FAT32 fd 表的薄封装。文件描述符是内核持有的
小非负整数；**没有每进程 fd 表**，因此 fd 在所有 Ring 3 任务间
全局共享。

## 常量

| 常量 | 值 | 含义 |
|---|---|---|
| `FS_O_RDONLY` | 0 | 只读打开模式 |
| `FS_O_RDWR` | 1 | 读写打开模式（不存在则创建；无独立 `O_CREAT`） |
| `FS_FILE_REG` | 0 | `FILE.kind` — 普通 FAT32 文件 |
| `FS_FILE_STDIN` | 1 | `FILE.kind` — 控制台 stdin 流 |
| `FS_FILE_STDOUT` | 2 | `FILE.kind` — 控制台 stdout 流 |
| `EOF` | -1 | 文件结束标志 |

## fd API

### `int fs_open(const char *path, int mode)`

- **参数**：`path` — FAT32 路径（相对 cwd 或绝对）；`mode` —
  `FS_O_RDONLY` 或 `FS_O_RDWR`。
- **返回值**：fd `>= 0`；失败返回 `< 0`（`ENOENT`、`EACCES`、
  `EMFILE` 等）。
- **详细说明**：`FS_O_RDWR` 在文件不存在时创建；没有 `O_TRUNC`
  ——打开已存在文件不会截断。

### `int fs_close(int fd)`

- **参数**：`fd` — `fs_open` 返回的描述符。
- **返回值**：成功 `0`；fd 非法返回 `< 0`（`EBADF`）。
- **详细说明**：—

### `int fs_read(int fd, void *buf, unsigned size)`

- **参数**：`fd`；`buf` — 目标缓冲区；`size` — 最多字节数。
- **返回值**：实际读取字节数（EOF 处短读），或 `< 0`。
- **详细说明**：—

### `int fs_write(int fd, const void *buf, unsigned size)`

- **参数**：`fd`；`buf` — 源缓冲区；`size` — 字节数。
- **返回值**：写入字节数，或 `< 0`。
- **详细说明**：—

### `int fs_unlink(const char *path)`

- **参数**：`path` — 要删除的文件。
- **返回值**：`0` 或 `< 0`。
- **详细说明**：删除文件；目录请用 `fs_rmdir`。

### `int fs_mkdir(const char *path)`

- **参数**：`path` — 要创建的目录。
- **返回值**：`0` 或 `< 0`（已存在返回 `EEXIST`）。
- **详细说明**：仅单层创建——父目录必须已存在。

### `int fs_rmdir(const char *path)`

- **参数**：`path` — 要删除的目录。
- **返回值**：`0` 或 `< 0`。
- **详细说明**：只能删除**空**目录（否则 `ENOTEMPTY`）。

### `int fs_stat(const char *path)`

- **参数**：`path` — 文件或目录。
- **返回值**：编码在返回值中——`>= 0` 为普通文件字节大小；
  `-2` 表示路径是目录；其他 `< 0` 为错误/不存在。
- **详细说明**：没有 `struct stat`——结果刻意压缩进返回码。

### `int fs_getcwd(char *buf, unsigned size)`

- **参数**：`buf` — 目标缓冲区；`size` — 容量。
- **返回值**：写入字节数（NUL 结尾），或 `< 0`。
- **详细说明**：把当前工作目录复制到 `buf`。

### `int fs_chdir(const char *path)`

- **参数**：`path` — 目标目录。
- **返回值**：`0` 或 `< 0`（`ENOENT`/`ENOTDIR`）。
- **详细说明**：改变进程级 cwd。

### `int fs_rename(const char *old_path, const char *new_path)`

- **参数**：`old_path`、`new_path` — 源与目标。
- **返回值**：`0` 或 `< 0`。
- **详细说明**：移动/重命名文件或目录。

### `int fs_readdir(const char *path, void *buf, unsigned size)`

- **参数**：`path` — 目录；`buf` — 输出缓冲区；`size` — 容量。
- **返回值**：写入字节数，或 `< 0`。
- **详细说明**：格式为每行一条记录——类型字节 `'-'`
  （文件）或 `'d'`（目录），一个空格，名称，最后 `'\n'`。

## FILE 层

`FILE` 与 `stdio.h` 共用：

```c
typedef struct {
    int fd;         /* 内核 fd；控制台类型不使用 */
    int eof;        /* 短读/EOF 后置位 */
    int err;        /* 内核错误时置位 */
    int kind;       /* FS_FILE_* */
    int ungot;      /* scanf 单字符回退（-1 = 无） */
} FILE;
```

以下是 fd 层流函数（`fs_f*`）；签名与语义同 `stdio.h` 的 `f*`
函数，但只处理 `FS_FILE_REG` 文件（控制台流在 `stdio.h` 中）。

### `FILE *fs_fopen(const char *path, const char *mode)`

- **参数**：`path` — FAT32 路径；`mode` — `"r"`/`"w"`/`"a"`/
  `"r+"` 等。
- **返回值**：`FILE *` 或失败 `NULL`。
- **详细说明**：无缓冲——每次操作都是一次系统调用。`"a"` 在打开
  时把 fd 偏移定位到文件末尾一次（非 POSIX 每次写都追加；注意事
  项见 `stdio.h` 的 `fopen`）。

### `int fs_fclose(FILE *f)`

- **参数**：`f` — `fs_fopen` 返回的流。
- **返回值**：`0` 或 `< 0`。
- **详细说明**：关闭底层 fd。

### `size_t fs_fread(void *buf, size_t size, size_t count, FILE *f)`

- **参数**：`buf` — 目标；`size` — 元素大小；`count` — 元素个数；
  `f` — 流。
- **返回值**：完整读入的元素数（EOF 处可短读）。
- **详细说明**：—

### `size_t fs_fwrite(const void *buf, size_t size, size_t count, FILE *f)`

- **参数**：`buf` — 源；`size`、`count` — 元素几何；`f` — 流。
- **返回值**：写入的元素数。
- **详细说明**：—

### `int fs_fgetc(FILE *f)`

- **参数**：`f` — 流。
- **返回值**：下一个字节（`int`），或 `EOF`。
- **详细说明**：相应置位 `eof`/`err` 标志。

### `int fs_fputc(int c, FILE *f)`

- **参数**：`c` — 字符；`f` — 流。
- **返回值**：成功返回 `c`，失败返回 `EOF`。
- **详细说明**：—

### `int fs_feof(FILE *f)` / `int fs_ferror(FILE *f)`

- **参数**：`f` — 流。
- **返回值**：对应标志置位则非零。
- **详细说明**：标志为粘性，直到 `fs_clearerr`。

### `void fs_clearerr(FILE *f)`

- **参数**：`f` — 流。
- **返回值**：无。
- **详细说明**：清除 `eof` 和 `err` 标志。
