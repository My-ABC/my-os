# `setjmp.h`

`typedef int jmp_buf[6]` — saves ebx/esi/edi/ebp/esp/eip.
`int setjmp(jmp_buf env)` — returns 0 first time, then re-returns
with the `longjmp` value.
`_Noreturn void longjmp(jmp_buf env, int val)` — `val==0` coerces
to 1 per C11.

Volatile locals modified between setjmp and longjmp have
indeterminate values on the second return (standard C rule).
No signal mask is saved (no sigprocmask in the ABI).
