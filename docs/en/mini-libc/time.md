# `time.h`

`time_t time(time_t *t)` — Unix timestamp (SYS_TIME/TIME_GET).
`struct tm *gmtime(const time_t*)` `struct tm *localtime(const time_t*)`
— civil-from-days; local time fixed UTC+8 (RTC stores UTC).
`struct tm` fields: `tm_sec tm_min tm_hour tm_mday tm_mon tm_year
tm_wday tm_yday tm_isdst`.
`time_t mktime(struct tm*)` — fields interpreted as local (UTC+8).
`size_t strftime(char *b, size_t n, const char *fmt, const struct tm*)`
— `%Y %y %m %d %e %H %M %S %I %p %j %w %a %A %b %B %%`.
`double difftime(time_t a, time_t b)` `clock_t clock(void)`
(wall-clock approximation).
`sleep(unsigned)`/`usleep(useconds_t)` — via TIME_SLEEP.
