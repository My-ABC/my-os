# `time.h` — 日历/时钟函数

基于 `SYS_TIME`。内核 RTC 保持 UTC；`localtime()` 应用固定的
UTC+8（北京时间）偏移——不支持 TZ/DST。

## 类型与全局变量

```c
typedef long time_t;
typedef long clock_t;

struct tm {
    int tm_sec;    /* 0-60  （允许闰秒） */
    int tm_min;    /* 0-59 */
    int tm_hour;   /* 0-23 */
    int tm_mday;   /* 1-31 */
    int tm_mon;    /* 0-11  （一月 = 0） */
    int tm_year;   /* 自 1900 年起的年数 */
    int tm_wday;   /* 0-6   （周日 = 0） */
    int tm_yday;   /* 0-365 */
    int tm_isdst;  /* 总是 0 */
};

#define CLOCKS_PER_SEC 1000000L
extern char *tzname[2];
extern long timezone;
extern int daylight;
```

`tzname`/`timezone`/`daylight` 仅为兼容存在，没有实际填充有
意义的值。

## 函数

### `time_t time(time_t *t)`

- **参数**：`t` — 接收时间戳的输出参数（可为 `NULL`）。
- **返回值**：自 1970-01-01 00:00:00 UTC 起的秒数。
- **详细说明**：`t` 非空时 `*t` 得到相同值。

### `struct tm *gmtime(const time_t *t)` / `struct tm *localtime(const time_t *t)`

- **参数**：`t` — 指向 `time_t` 的指针。
- **返回值**：指向**单个静态 `struct tm`**——`gmtime` 为 UTC，
  `localtime` 为北京时间（UTC+8）。
- **详细说明**：不可重入——静态缓冲区每次调用都会被覆盖。

### `time_t mktime(struct tm *tm)`

- **参数**：`tm` — 按本地（UTC+8）民用时间填写的分解时间。
- **返回值**：对应的 `time_t`；超范围字段会被规范化。
- **详细说明**：`localtime()` 的逆运算。

### `double difftime(time_t a, time_t b)`

- **参数**：`a`、`b` — 时间戳。
- **返回值**：`a - b`，单位秒。
- **详细说明**：—

### `size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *tm)`

- **参数**：`buf` — 输出缓冲区；`max` — 容量；`fmt` — 格式；
  `tm` — 分解时间。
- **返回值**：写入字符数（不含 NUL）；溢出返回 `0`。
- **详细说明**：支持的转换符：`%Y %y %m %d %e %H %M %S %I %p
  %j %w %a %A %b %B %%`；其余序列原样复制。

### `clock_t clock(void)`

- **参数**：无。
- **返回值**：以 `CLOCKS_PER_SEC` 为单位的流逝时间。
- **详细说明**：进程 CPU 时间用墙钟近似——实际上是自启动以来
  每秒 1 tick；不要用于精细性能分析。

### `unsigned int sleep(unsigned int seconds)`

- **参数**：`seconds` — 要睡眠的整秒数。
- **返回值**：未睡完的秒数（未被中断时为 `0`）。
- **详细说明**：映射到 `TIME_SLEEP`。

### `int usleep(unsigned long usec)`

- **参数**：`usec` — 微秒数。
- **返回值**：成功 `0`，失败 `< 0`。
- **详细说明**：微秒粒度的 `TIME_SLEEP`。注意：睡眠在内核侧
  等待——`usleep` 自旋的进程不会饿死其他 Ring 3 进程（定时器
  抢占仍然生效）。
