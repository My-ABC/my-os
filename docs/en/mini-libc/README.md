# mini-libc API Reference

mini-libc is a freestanding C library for Ring 3 programs on MyOS.
All public declarations are reachable through the single umbrella
header:

```c
#include <myos.h>
```

`myos.h` aggregates every module header; each header can still be
included individually. Header changes must be mirrored in these
docs.

- [Base headers](base.md) — `stdint.h`, `stddef.h`, `stdbool.h`,
  `limits.h`, `float.h`, `stdarg.h`, `stdnoreturn.h`
- [`string.h`](string.md)
- [`ctype.h`](ctype.h)
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
