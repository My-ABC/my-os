# `string.h`

`void *memcpy(void *d, const void *s, size_t n)` —— 复制 n 字节。
`void *memmove(...)` —— 可重叠复制。
`void *memset(void *d, int c, size_t n)` —— 填充字节。
`int memcmp(const void *a, const void *b, size_t n)` —— 比较。
`size_t strlen(const char *s)`
`int strcmp(const char *a, const char *b)` / `int strncmp(a,b,n)`
`char *strcpy(char *d, const char *s)` / `strncpy(d,s,n)`
`char *strcat(char *d, const char *s)` / `strncat(d,s,n)`
`char *strchr(const char *s, int c)` / `char *strrchr(s,c)`
`char *strstr(const char *haystack, const char *needle)`

标准 C 语义；尚无 `strndup`/`strtok`/`strspn` 系列。
