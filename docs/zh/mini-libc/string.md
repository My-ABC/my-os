# `string.h` — 内存与字符串函数

标准 C 语义；无错误返回值。长度均为 `size_t`，字符串以 NUL 结尾。

## `void *memcpy(void *dst, const void *src, size_t n)`

- **参数**：`dst` — 目标缓冲区；`src` — 源缓冲区；`n` — 复制字节数。
- **返回值**：`dst`。
- **详细说明**：源与目标区域重叠时行为未定义——重叠拷贝请用
  `memmove`。

## `void *memmove(void *dst, const void *src, size_t n)`

- **参数**：同 `memcpy`。
- **返回值**：`dst`。
- **详细说明**：源与目标重叠时安全。

## `void *memset(void *dst, int c, size_t n)`

- **参数**：`dst` — 缓冲区；`c` — 填充字节（转为 `unsigned char`）；
  `n` — 个数。
- **返回值**：`dst`。
- **详细说明**：将前 `n` 字节填充为 `c`。

## `int memcmp(const void *a, const void *b, size_t n)`

- **参数**：`a`、`b` — 缓冲区；`n` — 比较字节数。
- **返回值**：`<0`、`0` 或 `>0` — 按字节字典序比较。
- **详细说明**：在首个不同字节或比较完 `n` 字节时停止。

## `size_t strlen(const char *s)`

- **参数**：`s` — NUL 结尾字符串。
- **返回值**：字节长度，不含 NUL。
- **详细说明**：—

## `int strcmp(const char *a, const char *b)`

- **参数**：`a`、`b` — 字符串。
- **返回值**：`<0`/`0`/`>0` — `a` 小于/等于/大于 `b`。
- **详细说明**：比较完整字符串，包含结尾 NUL。

## `int strncmp(const char *a, const char *b, size_t n)`

- **参数**：`a`、`b` — 字符串；`n` — 最多比较字符数。
- **返回值**：`<0`/`0`/`>0`。
- **详细说明**：遇到 NUL 或比较满 `n` 个字符即停止。

## `char *strcpy(char *dst, const char *src)`

- **参数**：`dst` — 目标缓冲区（必须足够大）；`src` — 源字符串。
- **返回值**：`dst`。
- **详细说明**：无边界检查——已知大小时建议用 `strncpy`。

## `char *strncpy(char *dst, const char *src, size_t n)`

- **参数**：`dst`、`src`；`n` — 最多字节数。
- **返回值**：`dst`。
- **详细说明**：最多复制 `n` 字节，不足部分补 NUL；若 `src`
  长度 ≥ `n`，结果可能没有结尾 NUL。

## `char *strcat(char *dst, const char *src)`

- **参数**：`dst` — 有空间的 NUL 结尾目标；`src` — 待追加字符串。
- **返回值**：`dst`。
- **详细说明**：无边界检查。

## `char *strncat(char *dst, const char *src, size_t n)`

- **参数**：`dst`、`src`；`n` — 最多追加字符数。
- **返回值**：`dst`。
- **详细说明**：最多追加 `n` 个字符并补结尾 NUL。

## `char *strchr(const char *s, int c)`

- **参数**：`s` — 字符串；`c` — 字符（转为 `char`）。
- **返回值**：首个匹配位置的指针，未找到返回 `NULL`。
- **详细说明**：当 `c == 0`（`'\0'`）时返回指向结尾 NUL 的
  指针。

## `char *strrchr(const char *s, int c)`

- **参数**：`s` — 字符串；`c` — 字符。
- **返回值**：**最后**一个匹配位置的指针，未找到返回 `NULL`。
- **详细说明**：NUL 规则同 `strchr`。

## `char *strstr(const char *haystack, const char *needle)`

- **参数**：`haystack` — 被搜索字符串；`needle` — 子串。
- **返回值**：首个匹配位置的指针，未找到返回 `NULL`。
- **详细说明**：`needle` 为空串（`""`）时返回 `haystack`。
