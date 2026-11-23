# `ctype.h` — character classification

C locale only. The `is*` predicates take an `int` (a character
value or `EOF`) and return nonzero for true, `0` for false. The
converters return the converted `int` value.

| Function | Parameters | Returns | Details |
|---|---|---|---|
| `int isalnum(int c)` | character | nonzero/0 | alphanumeric (`0-9`, `A-Z`, `a-z`) |
| `int isalpha(int c)` | character | nonzero/0 | alphabetic |
| `int isblank(int c)` | character | nonzero/0 | space or horizontal tab |
| `int iscntrl(int c)` | character | nonzero/0 | control character |
| `int isdigit(int c)` | character | nonzero/0 | `0`–`9` |
| `int isgraph(int c)` | character | nonzero/0 | printable except space |
| `int islower(int c)` | character | nonzero/0 | `a`–`z` |
| `int isprint(int c)` | character | nonzero/0 | printable including space |
| `int ispunct(int c)` | character | nonzero/0 | punctuation |
| `int isspace(int c)` | character | nonzero/0 | whitespace: `' ' \t \n \v \f \r` |
| `int isupper(int c)` | character | nonzero/0 | `A`–`Z` |
| `int isxdigit(int c)` | character | nonzero/0 | `0-9`, `A-F`, `a-f` |
| `int toupper(int c)` | character | converted `int` | upper-case conversion; unchanged if not lowercase |
| `int tolower(int c)` | character | converted `int` | lower-case conversion; unchanged if not uppercase |
