# mini-libc API 参考（中文）

面向 MyOS Ring 3 程序的 freestanding C11 库。链接
`build/libmyc.a`（链接方法见 `examples/hello.c`）。
所有函数都是 freestanding：无 libc，除 `int 0x80`
syscall ABI 外不依赖任何 OS 服务。

## 基础头文件

| 头文件 | 内容 |
|---|---|
| `stdint.h` | `int8_t`–`int64_t`、`uint*_t`、`intptr_t`/`uintptr_t`、`intmax_t`/`uintmax_t`、least/fast 变体、`INT*_MAX/MIN` |
| `stddef.h` | `size_t`、`ptrdiff_t`、`NULL`、`offsetof` |
| `stdbool.h` | `bool`、`true`、`false` |
| `limits.h` | `CHAR_BIT`、`SCHAR_MIN/MAX`、`UCHAR_MAX`、`SHRT_*`、`INT_*`、`UINT_*`、`LONG_*`、`ULONG_*`、`LLONG_*`、`ULLONG_*` |
| `float.h` | `FLT_*`、`DBL_*`（宿主值） |
| `stdarg.h` | `va_list`、`va_start`、`va_arg`、`va_end`、`va_copy` |
| `stdnoreturn.h` | `noreturn` → `_Noreturn` |

## `string.h` / `ctype.h`

`memcpy memmove memset memcmp strlen strcmp strncmp strcpy strncpy
strcat strncat strchr strrchr strstr` —— 标准语义。
`isalnum isalpha isblank iscntrl isdigit isgraph islower isprint
ispunct isspace isupper isxdigit toupper tolower` —— C locale ASCII。

## `stdio.h`（`FILE`/`EOF` 见 `fs.h`）

控制台 + 字符串输出：
`printf sprintf snprintf puts putchar fprintf vfprintf`
输入：`scanf sscanf vscanf vfscanf getchar getline`
FILE 流（无缓冲）：
`stdin stdout`（无 `stderr`）、
`fopen fclose fread fwrite fgetc fputc fputs fgets feof ferror
clearerr fflush`
格式：`%d %i %u %o %x %X %c %s %p %%`，支持 `- 0 + 空格 #`
标志、宽度、精度、`*`；scanf 额外支持抑制/宽度；
**不支持 `%[...]` scanset**；长度修饰符接受但忽略；
无 64 位转换（`%lld` 未实现）。

## `fs.h` —— FAT32 fd + FILE 层

打开模式 `FS_O_RDONLY` `FS_O_RDWR`（无 O_CREAT——RDONLY
要求文件存在，RDWR 可创建）。
`fs_open fs_close fs_read fs_write fs_unlink fs_mkdir fs_rmdir
fs_stat fs_getcwd fs_chdir fs_rename fs_readdir`
`fs_stat` 返回大小、目录为 `-2`、错误 `<0`。
`fs_readdir` 记录格式：`'-'`/`'d'` + `' '` + 名字 + `'\n'`。
FILE 字段：`fd eof err kind ungot`；`EOF` 定义于此。

## `unistd.h` + `sys/types.h`

`pid_t ssize_t off_t`。
`_exit getpid sched_yield execve`（PROC_EXEC）、
`read write close`（fd 级）、
`unlink rmdir chdir getcwd access`。
`fork getppid lseek` → `-1`/`errno=ENOSYS`（内核不支持）。

## `stdlib.h`

堆：`malloc calloc realloc free aligned_alloc` —— 16 字节
头层，`malloc` 保证 8 字节对齐；`aligned_alloc` 执行 C11
规则（2 的幂对齐、`sizeof(void*)` 倍数、size % alignment
== 0），违例 `EINVAL`；`malloc(0)` → `NULL`。
`atexit`（16 槽、LIFO、仅 `exit` 执行）、
`exit _Exit abort`（`_Noreturn`）、
`atoi atol strtol strtoul`（base 0 自动识别、`ERANGE`）、
`rand srand`（xorshift32，`RAND_MAX=0x7FFFFFFF`）、
`qsort bsearch abs labs div ldiv getenv`（恒 `NULL`）。

## `errno.h`

`extern int errno` + `E*` 常量（与 Linux 取值一致）。
库封装在失败时设置；裸 `syscallN` 不设置。

## `time.h`

`time gmtime localtime mktime strftime difftime sleep usleep
clock` —— civil-from-days 换算，本地时间固定 UTC+8。

## `assert.h`

`assert(e)` → `__assert_fail(#e,__FILE__,__LINE__,__func__)`
向 stdout 打印后 `abort()`；`NDEBUG` 编译为空。

## `setjmp.h`

`jmp_buf` = 6 个 int（ebx/esi/edi/ebp/esp/eip）；
`setjmp`/`longjmp`（`_Noreturn`），`longjmp(env,0)` 返回 1。
longjmp 跨越的 volatile 局部变量值不确定（C 规则）。

## `signal.h`

`sig_t sig_atomic_t SIG_ERR SIG_DFL SIG_IGN SIGINT=2 SIGKILL=9
NSIG=32`；`signal kill raise`。
仅 SIGINT 可捕获（Ctrl+C），SIGKILL 必杀；无
sigprocmask/sigaction。处理器就是普通 `void h(int)`；
返回后由内核 restorer 恢复现场。

## `process.h` + `sys/types.h`

`proc_info_t{pid,state,foreground}`、`PROC_ST_*`；
`proc_list proc_set_foreground proc_foreground`。
无 fork/wait——每个 `run` 是独立进程。

## `inttypes.h`

全 stdint 宽度的 `PRId*/PRIi*/PRIo*/PRIu*/PRIx*/PRIX*`
+ `SCN*`（LP32：`int64_t`→`ll`，`intmax_t`=int）；
`imaxabs imaxdiv strtoimax strtoumax`。

## `sys/syscall.h`

仅 ABI 常量——请优先使用上面的封装。
类 `SYS_IO/TIME/MEMORY/POWER/PROCESS/FS/SIG`、子操作、
`SYSFS_O_*`、`PROC_STATE_*`。
