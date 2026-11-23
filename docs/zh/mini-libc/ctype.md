# `ctype.h` — 字符分类

仅 C locale。`is*` 谓词接收一个 `int`（字符值或 `EOF`），为真返回
非零、为假返回 `0`；转换函数返回转换后的 `int` 值。

| 函数 | 参数 | 返回值 | 详细说明 |
|---|---|---|---|
| `int isalnum(int c)` | 字符 | 非零/0 | 字母数字（`0-9`、`A-Z`、`a-z`） |
| `int isalpha(int c)` | 字符 | 非零/0 | 字母 |
| `int isblank(int c)` | 字符 | 非零/0 | 空格或水平制表符 |
| `int iscntrl(int c)` | 字符 | 非零/0 | 控制字符 |
| `int isdigit(int c)` | 字符 | 非零/0 | `0`–`9` |
| `int isgraph(int c)` | 字符 | 非零/0 | 可打印字符（不含空格） |
| `int islower(int c)` | 字符 | 非零/0 | `a`–`z` |
| `int isprint(int c)` | 字符 | 非零/0 | 可打印字符（含空格） |
| `int ispunct(int c)` | 字符 | 非零/0 | 标点符号 |
| `int isspace(int c)` | 字符 | 非零/0 | 空白：`' ' \t \n \v \f \r` |
| `int isupper(int c)` | 字符 | 非零/0 | `A`–`Z` |
| `int isxdigit(int c)` | 字符 | 非零/0 | `0-9`、`A-F`、`a-f` |
| `int toupper(int c)` | 字符 | 转换后 `int` | 转大写；非小写字母则原样返回 |
| `int tolower(int c)` | 字符 | 转换后 `int` | 转小写；非大写字母则原样返回 |
