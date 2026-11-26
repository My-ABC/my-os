# `inttypes.h` — printf/scanf 长度宏 + imax 系列

LP32 i386：`int32_t` 是 `int`，`int64_t` 是 `long long`，
`intmax_t` 是 `int`（`int32_t` 宽度）。

## PRI*/SCN* 长度宏

`PRI*` 宏把固定宽度类型展开为正确的 `printf` 长度修饰符；
`SCN*` 对 `scanf` 同理。模式：`PRI{d,i,o,u,x,X}{8,16,32,64,
LEAST*,FAST*,MAX,PTR}` 及 `SCN{d,i,o,u,x}{...}`。

示例：`PRId32` → `"d"`，`PRId64` → `"lld"`，`PRIx16` → `"hx"`，
`SCNu32` → `"u"`，`PRIuMAX` → `"u"`。

- **参数**：不适用（预处理宏）。
- **返回值**：不适用——格式字符串字面量。
- **详细说明**：用法 `printf("%" PRId64, val)`。**注意**：stdio
  格式化器目前只处理 `int`/`unsigned` 宽度——`ll` 长度宏能正确
  展开，但 `%lld` 尚未被解析。

## 类型与函数

```c
typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;
```

### `intmax_t imaxabs(intmax_t j)`

- **参数**：`j` — 值。
- **返回值**：绝对值。
- **详细说明**：—

### `imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom)`

- **参数**：`numer`、`denom` — 被除数与除数。
- **返回值**：`{quot, rem}`——商向零截断，余数符号同 `numer`。
  除零行为未定义。
- **详细说明**：—

### `intmax_t strtoimax(const char *nptr, char **endptr, int base)` / `uintmax_t strtoumax(const char *nptr, char **endptr, int base)`

- **参数**：`nptr` — 输入字符串；`endptr` — 输出参数，指向首个
  未解析字符（可为 `NULL`）；`base` — 2–36 或 `0` 自动识别。
- **返回值**：转换值；溢出时 `errno = ERANGE`。
- **详细说明**：语义同 `strtol`/`strtoul`。
