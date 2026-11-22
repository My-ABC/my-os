# `time.h`

`time_t time(time_t *t)` —— Unix 时间戳（SYS_TIME/TIME_GET）。
`struct tm *gmtime(const time_t*)` `struct tm *localtime(const time_t*)`
—— civil-from-days 换算；本地时间固定 UTC+8（RTC 存 UTC）。
`struct tm` 字段：`tm_sec tm_min tm_hour tm_mday tm_mon tm_year
tm_wday tm_yday tm_isdst`。
`time_t mktime(struct tm*)` —— 字段按本地时间（UTC+8）解释。
`size_t strftime(char *b, size_t n, const char *fmt, const struct tm*)`
—— `%Y %y %m %d %e %H %M %S %I %p %j %w %a %A %b %B %%`。
`double difftime(time_t a, time_t b)` `clock_t clock(void)`
（墙钟近似）。
`sleep(unsigned)`/`usleep(useconds_t)` —— 经 TIME_SLEEP。
