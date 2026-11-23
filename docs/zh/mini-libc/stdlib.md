# `stdlib.h` — 进程退出、堆、转换、PRNG、排序

涵盖进程终止、`SYS_MEMORY` 上的堆分配、字符串↔数字转换、
PRNG、排序/查找及杂项辅助函数。

## 进程终止

### `_Noreturn void exit(int status)`

- **参数**：`status` — 退出状态（约定 `0` = 成功）。
- **返回值**：永不返回。
- **详细说明**：按 LIFO 顺序运行 `atexit()` 处理函数，刷新
  stdio，然后发出 `PROC_EXIT`。`main` 中应调用它而不是 return。

### `_Noreturn void _Exit(int status)`

- **参数**：`status` — 退出状态。
- **返回值**：永不返回。
- **详细说明**：立即 `PROC_EXIT`；跳过 `atexit` 处理函数和
  stdio 刷新。`unistd.h` 中的 `_exit()` 是同一操作。

### `_Noreturn void abort(void)`

- **参数**：无。
- **返回值**：永不返回。
- **详细说明**：经 `PROC_EXIT` 终止，不做任何清理。

## 堆分配

堆是内核的每进程用户堆，前面带 16 字节 mini-libc 头。
`malloc` 保证 8 字节对齐；`aligned_alloc` 支持更大的 2 的幂
对齐。没有 `sbrk` 或堆内省接口。

### `void *malloc(size_t size)`

- **参数**：`size` — 要分配的字节数。
- **返回值**：块指针（8 字节对齐），失败返回 `NULL` 且
  `errno = ENOMEM`。
- **详细说明**：—

### `void *calloc(size_t nmemb, size_t size)`

- **参数**：`nmemb` — 元素个数；`size` — 元素大小。
- **返回值**：清零块指针，失败/溢出返回 `NULL`。
- **详细说明**：`nmemb * size` 带溢出检查。

### `void *realloc(void *ptr, size_t size)`

- **参数**：`ptr` — 已有块或 `NULL`；`size` — 新大小。
- **返回值**：（可能移动的）块指针；失败返回 `NULL`，原块保持
  不变。
- **详细说明**：`size == 0` 释放 `ptr` 并返回 `NULL`；`ptr ==
  NULL` 等同 `malloc`。

### `void free(void *ptr)`

- **参数**：`ptr` — 来自 `malloc`/`calloc`/`realloc` 的块，或
  `NULL`。
- **返回值**：无。
- **详细说明**：`NULL` 为空操作；释放外来指针行为未定义。

### `void *aligned_alloc(size_t alignment, size_t size)`

- **参数**：`alignment` — 2 的幂且为 `sizeof(void*)` 的倍数；
  `size` — `alignment` 的非零倍数。
- **返回值**：对齐指针；违反约定返回 `NULL`（`errno = EINVAL`），
  内存耗尽返回 `ENOMEM`。
- **详细说明**：C11 语义——比 POSIX `memalign` 更严格。

### `int atexit(void (*fn)(void))`

- **参数**：`fn` — `exit()` 时运行的函数。
- **返回值**：成功 `0`；表满（最多 16 项）返回 `-1`。
- **详细说明**：处理函数按 LIFO 运行且仅在 `exit()` 时执行——
  `_Exit()`/`abort()` 或异常终止时不运行。

## 字符串 → 数字转换

### `int atoi(const char *s)` / `long atol(const char *s)`

- **参数**：`s` — 十进制字符串（允许前导空白和符号）。
- **返回值**：转换值；无数字时返回 `0`。溢出行为未定义（不设
  `errno`）。
- **详细说明**：等同 `strtol(s, NULL, 10)`。

### `long strtol(const char *s, char **endptr, int base)`

- **参数**：`s` — 输入；`endptr` — 输出参数，指向首个未解析字符
  （可为 `NULL`）；`base` — 2–36，或 `0` 自动识别（`0x` 十六进
  制、`0` 八进制、否则十进制）。
- **返回值**：转换后的 `long`；溢出时 `errno = ERANGE`。
- **详细说明**：处理可选符号和进制前缀。

### `unsigned long strtoul(const char *s, char **endptr, int base)`

- **参数/返回值/详细说明**：同 `strtol`，结果为无符号；前导 `-`
  按 `ULONG_MAX + 1` 取模取反。

## PRNG

### `int rand(void)` / `void srand(unsigned seed)`

- **参数**：`seed` — PRNG 种子。
- **返回值**：`rand` 返回 `[0, RAND_MAX]`；`RAND_MAX` 为
  `0x7FFFFFFF`。
- **详细说明**：xorshift32 生成器；调用 `srand` 前使用固定默认
  种子。

## 排序与查找

### `void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))`

- **参数**：`base` — 数组基址；`nmemb` — 元素个数；`size` — 元素
  大小（字节）；`compar` — 返回 `<0`/`0`/`>0` 的比较函数。
- **返回值**：无；原地排序。
- **详细说明**：不稳定；`nmemb == 0` 或 `size == 0` 为空操作。

### `void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))`

- **参数**：`key` — 要查找的元素；`base`/`nmemb`/`size` — 已排序
  数组；`compar` — 约定同 `qsort`。
- **返回值**：匹配元素的指针，未找到返回 `NULL`。
- **详细说明**：数组必须已按同一比较函数排序。

## 整数辅助

### `int abs(int x)` / `long labs(long x)`

- **参数**：`x` — 值。
- **返回值**：绝对值；`INT_MIN`/`LONG_MIN` 的结果未定义。

### `div_t div(int numer, int denom)` / `ldiv_t ldiv(long numer, long denom)`

- **参数**：`numer`、`denom` — 被除数与除数。
- **返回值**：`{quot, rem}` 结构——商向零截断，余数符号同
  `numer`。除零行为未定义。

### `char *getenv(const char *name)`

- **参数**：`name` — 变量名。
- **返回值**：总是 `NULL`。
- **详细说明**：`crt0` 传入 `envp=NULL`；环境为空。
