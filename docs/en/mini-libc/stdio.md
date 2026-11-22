# `stdio.h` (+ `fs.h` for `FILE`/`EOF`)

## Output
`int printf(const char *fmt, ...)` `int sprintf(char *b, fmt, ...)`
`int snprintf(char *b, size_t n, fmt, ...)`
`int puts(const char *s)` `int putchar(int c)`
`int fprintf(FILE *f, fmt, ...)` `int vfprintf(FILE*, va_list)`
`int vprintf(fmt, va_list)` `int vsprintf/vsnprintf`

## Input
`int scanf(fmt, ...)` `int sscanf(const char *s, fmt, ...)`
`int vscanf/vsscanf` `int getchar(void)` `char *getline(char **lineptr, size_t *n)`

## FILE streams (unbuffered)
`extern FILE *stdin, *stdout` — console; **no `stderr`**.
`FILE *fopen(const char *path, const char *mode)` (`"r"`/`"r+"`/`"w"`/`"a"` variants)
`int fclose(FILE*)` `size_t fread/fwrite(ptr,size,n,FILE*)`
`int fgetc(FILE*)` `int fputc(int,FILE*)` `int fputs(const char*,FILE*)`
`char *fgets(char*,int,FILE*)`
`int feof(FILE*)` `int ferror(FILE*)` `void clearerr(FILE*)`
`int fflush(FILE*)` — `NULL` flushes the console line buffer.

## Formats
Conversions `%d %i %u %o %x %X %c %s %p %%`; flags `- 0 + space #`;
width/precision incl. `*`; scanf `%*` suppression, width, `%n`.
**Not supported:** `%[...]` scansets, length modifiers are
accepted but ignored, no 64-bit conversions (`%lld` unimplemented).
