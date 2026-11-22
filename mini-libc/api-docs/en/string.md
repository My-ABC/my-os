# `string.h`

`void *memcpy(void *d, const void *s, size_t n)` — copy n bytes.
`void *memmove(...)` — copy, overlap-safe.
`void *memset(void *d, int c, size_t n)` — fill byte.
`int memcmp(const void *a, const void *b, size_t n)` — compare.
`size_t strlen(const char *s)`
`int strcmp(const char *a, const char *b)` / `int strncmp(a,b,n)`
`char *strcpy(char *d, const char *s)` / `strncpy(d,s,n)`
`char *strcat(char *d, const char *s)` / `strncat(d,s,n)`
`char *strchr(const char *s, int c)` / `char *strrchr(s,c)`
`char *strstr(const char *haystack, const char *needle)`

Standard C semantics; no `strndup`/`strtok`/`strspn` family yet.
