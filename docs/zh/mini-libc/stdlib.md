# `stdlib.h`

## 堆
`void *malloc(size_t)` —— 经 16 字节头层保证 8 字节对齐；
`malloc(0)` → `NULL`。
`void *calloc(n,size)` `void *realloc(p,size)`（alloc-copy-free；
失败保留旧数据）`void free(p)`。
`void *aligned_alloc(size_t alignment, size_t size)` —— C11 规则：
alignment 须为 2 的幂且是 `sizeof(void*)` 倍数，size 须为
alignment 的非零倍数；违例 → `NULL`/`errno=EINVAL`。
`int atexit(void (*fn)(void))` —— 16 槽、LIFO、仅 `exit()` 执行。

## 退出
`_Noreturn void exit(int)`（atexit + fflush + PROC_EXIT）、
`_Noreturn void _Exit(int)`（立即）、
`_Noreturn void abort(void)`（退出码 134）。

## 转换
`int atoi(const char*)` `long atol(const char*)`
`long strtol(nptr,&end,base)` `unsigned long strtoul(...)`
base 0 自动识别 `0x`/前导 0；溢出 → `ERANGE`/`LONG_MAX`。
无 `atof`/`strtod`（无浮点格式化）。

## 随机
`int rand(void)` `void srand(unsigned)` —— xorshift32，
`RAND_MAX = 0x7FFFFFFF`。

## 排序/查找
`void qsort(base,n,size,cmp)` `void *bsearch(key,base,n,size,cmp)`。

## 整数/杂项
`int abs(int)` `long labs(long)` `div_t div(int,int)`
`ldiv_t ldiv(long,long)`（`{quot,rem}`）。
`char *getenv(const char*)` —— 恒 `NULL`（无环境变量）。
