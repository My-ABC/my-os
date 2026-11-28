# `stdio.h` — 格式化与流式 I/O

基于 MyOS syscall ABI 的格式化控制台 I/O 和 `FILE` 流。目前没有
真正的缓冲——控制台输出走 `SYS_IO/IO_WRITE`（VGA + 串口镜像），
控制台输入来自 `SYS_IO/IO_READ` 行读取。

## 格式支持

- `printf` 系列：`%d %i %u %o %x %X %c %s %p %%`，支持标志
  `- 0 + space #`、域宽和精度（可用 `*`）。长度修饰符
  `h/hh/l/ll` 被接受但忽略——按 `int` 处理。无浮点转换。
- `scanf` 系列：`%d %i %u %o %x %X %c %s %p %%`，支持可选的
  `*` 抑制赋值和域宽；长度修饰符忽略。

## 控制台输出

### `int printf(const char *fmt, ...)`

- **参数**：`fmt` — 格式字符串；可变参数按转换符对应。
- **返回值**：写入字符数，或 `< 0`。
- **详细说明**：经 `IO_WRITE` 写控制台（VGA 文本/VBE + COM1
  串口镜像）。

### `int sprintf(char *buf, const char *fmt, ...)`

- **参数**：`buf` — 目标缓冲区（调用者保证足够大）；`fmt` 及
  可变参数同 `printf`。
- **返回值**：写入字符数（不含 NUL），或 `< 0`。
- **详细说明**：**无边界检查**——已知大小时请用 `snprintf`。

### `int snprintf(char *buf, size_t n, const char *fmt, ...)`

- **参数**：`buf`、`n` — 缓冲区及容量（含 NUL）；`fmt` 及可变
  参数同 `printf`。
- **返回值**：本应写入的字符数（不含 NUL），或 `< 0`。
- **详细说明**：输出截断为 `n-1` 字符 + NUL。`buf == NULL` 或
  `n == 0` 时只计数——不写入也不打印。

### `int puts(const char *s)`

- **参数**：`s` — NUL 结尾字符串。
- **返回值**：成功非负，失败 `< 0`。
- **详细说明**：输出 `s` 后追加换行。

### `int putchar(int c)`

- **参数**：`c` — 字符。
- **返回值**：成功返回 `c`，失败返回 `EOF`/`< 0`。
- **详细说明**：单字符控制台写。

## 控制台输入

### `int scanf(const char *fmt, ...)`

- **参数**：`fmt` — 格式字符串；可变参数为接收转换结果的指针。
- **返回值**：成功赋值次数；首次赋值前输入失败返回 `EOF`。
- **详细说明**：经 `IO_READ` 读一行控制台输入。**不要**与
  `getchar`/`getline` 混用——它们走不同的内核路径。

### `int sscanf(const char *s, const char *fmt, ...)`

- **参数**：`s` — 输入字符串；`fmt` 及可变参数同 `scanf`。
- **返回值**：赋值次数或 `EOF`。
- **详细说明**：从内存解析，不涉及控制台 I/O。

### `int getchar(void)`

- **参数**：无。
- **返回值**：下一个字符（`int`），错误/信号中止返回 `EOF`。
- **详细说明**：经 `IO_GETCHAR` 读一个字节；缓冲区空时阻塞。
  从不返回 `'\n'`：stdin 是行缓冲，内核 `IO_READ` 剥掉换行，
  `in_getch` 在每行末尾合成虚拟 `'\n'`、由 `getchar` 滤掉——
  `fgets`/`scanf` 得到正常行语义，"不返回换行"契约保持不变。

### `int getline(char *buf, int size)`

- **参数**：`buf` — 目标缓冲区；`size` — 容量（含 NUL）。
- **返回值**：行长度（不含 NUL），错误/溢出返回 `-1`。
- **详细说明**：直接经 `IO_READ` 读一行。同样不要与
  `getchar`/`scanf` 混用。

## FILE 流

`stdin` 和 `stdout` 是控制台流（`stderr` 暂缓）。`fopen()` 返回
基于 `fs.h` fd 层的**无缓冲** `FILE*` 流——每次调用都是一次
系统调用。

```c
extern FILE *stdin;
extern FILE *stdout;
```

### `FILE *fopen(const char *path, const char *mode)`

- **参数**：`path` — FAT32 路径；`mode` — `"r"`、`"w"`、`"a"`、
  `"r+"` 等。
- **返回值**：成功返回 `FILE *`，失败 `NULL`。
- **详细说明**：无缓冲；模式语义见 `fs.h`。`"a"` 在**打开时**把
  fd 偏移定位到文件末尾一次——并非 POSIX 的每次写都追加（目前
  没有 `lseek`，偏移本就只能前进，但未来若有 `lseek` 仍可能从头
  覆盖）。`"w"` 同样不会截断已有尾部。

### `int fclose(FILE *f)`

- **参数**：`f` — `fopen` 返回的流。
- **返回值**：`0` 或 `< 0`。
- **详细说明**：刷新（无缓冲时为空操作）并关闭 fd。

### `size_t fread(void *buf, size_t size, size_t count, FILE *f)`

- **参数**：`buf` — 目标；`size` — 元素大小；`count` — 元素个数；
  `f` — 流。
- **返回值**：完整读入的元素数（EOF 处可短读）。
- **详细说明**：—

### `size_t fwrite(const void *buf, size_t size, size_t count, FILE *f)`

- **参数**：`buf` — 源；`size`、`count` — 元素几何；`f` — 流。
- **返回值**：写入的元素数。
- **详细说明**：—

### `int fgetc(FILE *f)` / `int fputc(int c, FILE *f)`

- **参数**：`f` — 流；`c` — `fputc` 的字符。
- **返回值**：`fgetc` — 字节（`int`）或 `EOF`；`fputc` — `c` 或
  `EOF`。
- **详细说明**：—

### `int fputs(const char *s, FILE *f)`

- **参数**：`s` — 字符串；`f` — 流。
- **返回值**：成功非负，失败 `EOF`。
- **详细说明**：—

### `char *fgets(char *buf, int size, FILE *f)`

- **参数**：`buf` — 目标；`size` — 容量；`f` — 流。
- **返回值**：成功返回 `buf`；任何字符之前 EOF/错误返回 `NULL`。
- **详细说明**：最多读 `size-1` 个字符，保留行尾换行。
  `stdin` 上内核 `IO_READ` 消费真实换行但不存入，mini-libc
  为每行补一个虚拟 `'\n'`——`fgets` 看到的是正常的行缓冲语义。

### `int fprintf(FILE *f, const char *fmt, ...)` / `int fscanf(FILE *f, const char *fmt, ...)`

- **参数**：`f` — 流；`fmt` 及可变参数同 `printf`/`scanf`。
- **返回值**：约定同 `printf`/`scanf`。
- **详细说明**：—

### `int feof(FILE *f)` / `int ferror(FILE *f)` / `void clearerr(FILE *f)` / `int fflush(FILE *f)`

- **参数**：`f` — 流。
- **返回值**：`feof`/`ferror` — 非零标志；`fflush` — `0`
  （无操作，无缓冲）；`clearerr` — 无返回值。
- **详细说明**：eof/error 标志为粘性，直到 `clearerr`。
