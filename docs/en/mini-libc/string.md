# `string.h` — memory and string functions

Standard C semantics; no error returns. All lengths are `size_t`,
strings are NUL-terminated.

## `void *memcpy(void *dst, const void *src, size_t n)`

- **Parameters**: `dst` — destination buffer; `src` — source buffer;
  `n` — bytes to copy.
- **Returns**: `dst`.
- **Details**: Undefined behavior if the regions overlap — use
  `memmove` for overlapping copies.

## `void *memmove(void *dst, const void *src, size_t n)`

- **Parameters**: same as `memcpy`.
- **Returns**: `dst`.
- **Details**: Safe when source and destination overlap.

## `void *memset(void *dst, int c, size_t n)`

- **Parameters**: `dst` — buffer; `c` — fill byte (converted to
  `unsigned char`); `n` — count.
- **Returns**: `dst`.
- **Details**: Fills the first `n` bytes with `c`.

## `int memcmp(const void *a, const void *b, size_t n)`

- **Parameters**: `a`, `b` — buffers; `n` — bytes to compare.
- **Returns**: `<0`, `0`, or `>0` — lexicographic byte comparison.
- **Details**: Comparison stops at the first differing byte or after
  `n` bytes.

## `size_t strlen(const char *s)`

- **Parameters**: `s` — NUL-terminated string.
- **Returns**: length in bytes, excluding the NUL.
- **Details**: —

## `int strcmp(const char *a, const char *b)`

- **Parameters**: `a`, `b` — strings.
- **Returns**: `<0`/`0`/`>0` — `a` less/equal/greater than `b`.
- **Details**: Compares the full strings including the terminating
  NUL.

## `int strncmp(const char *a, const char *b, size_t n)`

- **Parameters**: `a`, `b` — strings; `n` — maximum characters.
- **Returns**: `<0`/`0`/`>0`.
- **Details**: Stops at the first NUL or after `n` characters,
  whichever comes first.

## `char *strcpy(char *dst, const char *src)`

- **Parameters**: `dst` — destination buffer (must be large enough);
  `src` — source string.
- **Returns**: `dst`.
- **Details**: No bounds checking — prefer `strncpy` when the size
  is known.

## `char *strncpy(char *dst, const char *src, size_t n)`

- **Parameters**: `dst`, `src`; `n` — maximum bytes.
- **Returns**: `dst`.
- **Details**: Copies at most `n` bytes and pads the rest with NULs
  up to `n`; the result may lack a terminator if `src` is `n` bytes
  or longer.

## `char *strcat(char *dst, const char *src)`

- **Parameters**: `dst` — NUL-terminated destination with room;
  `src` — string to append.
- **Returns**: `dst`.
- **Details**: No bounds checking.

## `char *strncat(char *dst, const char *src, size_t n)`

- **Parameters**: `dst`, `src`; `n` — maximum appended chars.
- **Returns**: `dst`.
- **Details**: Appends at most `n` characters plus a terminating
  NUL.

## `char *strchr(const char *s, int c)`

- **Parameters**: `s` — string; `c` — character (converted to
  `char`).
- **Returns**: pointer to the first occurrence, or `NULL`.
- **Details**: When `c == 0` (`'\0'`), returns a pointer to the
  terminating NUL.

## `char *strrchr(const char *s, int c)`

- **Parameters**: `s` — string; `c` — character.
- **Returns**: pointer to the **last** occurrence, or `NULL`.
- **Details**: Same NUL rule as `strchr`.

## `char *strstr(const char *haystack, const char *needle)`

- **Parameters**: `haystack` — string to search; `needle` —
  substring.
- **Returns**: pointer to the first match, or `NULL`.
- **Details**: An empty `needle` (`""`) returns `haystack`.
