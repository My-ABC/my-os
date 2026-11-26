# `time.h` — calendar/clock functions

Over `SYS_TIME`. The kernel RTC keeps UTC; `localtime()` applies a
fixed UTC+8 (Beijing) offset — there is no TZ/DST support.

## Types and globals

```c
typedef long time_t;
typedef long clock_t;

struct tm {
    int tm_sec;    /* 0-60  (leap second allowed) */
    int tm_min;    /* 0-59 */
    int tm_hour;   /* 0-23 */
    int tm_mday;   /* 1-31 */
    int tm_mon;    /* 0-11  (January = 0) */
    int tm_year;   /* years since 1900 */
    int tm_wday;   /* 0-6   (Sunday = 0) */
    int tm_yday;   /* 0-365 */
    int tm_isdst;  /* always 0 */
};

#define CLOCKS_PER_SEC 1000000L
extern char *tzname[2];
extern long timezone;
extern int daylight;
```

`tzname`/`timezone`/`daylight` exist for compatibility; they are
not populated with meaningful values.

## Functions

### `time_t time(time_t *t)`

- **Parameters**: `t` — out-param receiving the timestamp (may be
  `NULL`).
- **Returns**: seconds since 1970-01-01 00:00:00 UTC.
- **Details**: `*t` gets the same value when non-`NULL`.

### `struct tm *gmtime(const time_t *t)` / `struct tm *localtime(const time_t *t)`

- **Parameters**: `t` — pointer to a `time_t`.
- **Returns**: pointer to a **single static `struct tm`** — UTC for
  `gmtime`, Beijing (UTC+8) for `localtime`.
- **Details**: Not reentrant — the static buffer is overwritten by
  each call.

### `time_t mktime(struct tm *tm)`

- **Parameters**: `tm` — broken-down local (UTC+8) civil time.
- **Returns**: corresponding `time_t`; out-of-range fields are
  normalized.
- **Details**: Inverse of `localtime()`.

### `double difftime(time_t a, time_t b)`

- **Parameters**: `a`, `b` — timestamps.
- **Returns**: `a - b` in seconds.
- **Details**: —

### `size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *tm)`

- **Parameters**: `buf` — output buffer; `max` — capacity;
  `fmt` — format; `tm` — broken-down time.
- **Returns**: characters written excluding NUL; `0` on overflow.
- **Details**: Supported conversions: `%Y %y %m %d %e %H %M %S %I
  %p %j %w %a %A %b %B %%`; everything else is copied verbatim.

### `clock_t clock(void)`

- **Parameters**: none.
- **Returns**: elapsed time in `CLOCKS_PER_SEC` ticks.
- **Details**: Process CPU time is approximated by the wall clock —
  effectively 1 tick per second since boot/epoch base; do not rely
  on it for fine-grained profiling.

### `unsigned int sleep(unsigned int seconds)`

- **Parameters**: `seconds` — whole seconds to sleep.
- **Returns**: unslept seconds (`0` unless interrupted).
- **Details**: Maps to `TIME_SLEEP`.

### `int usleep(unsigned long usec)`

- **Parameters**: `usec` — microseconds.
- **Returns**: `0` on success, `< 0` on error.
- **Details**: Microsecond-granularity `TIME_SLEEP`. Note: the sleep
  is a kernel-side wait — a process spinning in `usleep` does not
  starve other Ring 3 processes (timer preemption still applies).
