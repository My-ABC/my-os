# mini-libc API 参考（中文）

mini-libc 是面向 MyOS Ring 3 程序的 freestanding C 库。所有公
开声明都可以通过单一的总头文件获取：

```c
#include <myos.h>
```

`myos.h` 聚合了所有模块头文件；每个头文件仍可单独包含。头文件
变更时需同步更新本文档。

- [基础头文件](base.md) — `stdint.h`、`stddef.h`、`stdbool.h`、
  `limits.h`、`float.h`、`stdarg.h`、`stdnoreturn.h`
- [`string.h`](string.md)
- [`ctype.h`](ctype.md)
- [`stdio.h`](stdio.md)
- [`fs.h`](fs.md)
- [`unistd.h`](unistd.md)
- [`stdlib.h`](stdlib.md)
- [`errno.h`](errno.md)
- [`time.h`](time.md)
- [`assert.h`](assert.md)
- [`setjmp.h`](setjmp.md)
- [`signal.h`](signal.md)
- [`process.h`](process.md)
- [`console.h`](console.md)
- [`power.h`](power.md)
- [`inttypes.h`](inttypes.md)
- [`sys/syscall.h`](syscall.md)
