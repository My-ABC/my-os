# `setjmp.h`

`typedef int jmp_buf[6]` — saves ebx/esi/edi/ebp/esp/eip.
`int setjmp(jmp_buf env)` — returns 0 first time, then re-returns
with the `longjmp` value.
`_Noreturn void longjmp(jmp_buf env, int val)` — `val==0` coerces
to 1 per C11.

Locals that are not volatile-qualified, are local to the function containing setjmp, and are modified between setjmp and longjmp have indeterminate values after a longjmp return. volatile locals are safe — the compiler must reload them from memory.
No signal mask is saved (no sigprocmask in the ABI).
