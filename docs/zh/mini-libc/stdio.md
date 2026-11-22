# `stdio.h`（`FILE`/`EOF` 见 `fs.h`）

## 输出
`int printf(const char *fmt, ...)` `int sprintf(char *b, fmt, ...)`
`int snprintf(char *b, size_t n, fmt, ...)`
`int puts(const char *s)` `int putchar(int c)`
`int fprintf(FILE *f, fmt, ...)` `int vfprintf(FILE*, va_list)`
`int vprintf(fmt, va_list)` `int vsprintf/vsnprintf`

## 输入
`int scanf(fmt, ...)` `int sscanf(const char *s, fmt, ...)`
`int vscanf/vsscanf` `int getchar(void)` `char *getline(char **lineptr, size_t *n)`

## FILE 流（无缓冲）
`extern FILE *stdin, *stdout` —— 控制台；**无 `stderr`**。
`FILE *fopen(const char *path, const char *mode)`（`"r"`/`"r+"`/`"w"`/`"a"` 变体）
`int fclose(FILE*)` `size_t fread/fwrite(ptr,size,n,FILE*)`
`int fgetc(FILE*)` `int fputc(int,FILE*)` `int fputs(const char*,FILE*)`
`char *fgets(char*,int,FILE*)`
`int feof(FILE*)` `int ferror(FILE*)` `void clearerr(FILE*)`
`int fflush(FILE*)` —— `NULL` 刷新控制台行缓冲。

## 格式
转换 `%d %i %u %o %x %X %c %s %p %%`；标志 `- 0 + 空格 #`；
宽度/精度含 `*`；scanf 支持 `%*` 抑制、宽度、`%n`。
**不支持：** `%[...]` scanset、长度修饰符接受但忽略、
无 64 位转换（`%lld` 未实现）。
