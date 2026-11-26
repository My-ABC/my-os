# `setjmp.h` — non-local jumps

Over callee-saved registers only; no signal mask is saved.

## Types

```c
typedef int jmp_buf[6];   /* ebx esi edi ebp esp eip */
```

## Functions

### `int setjmp(jmp_buf env)`

- **Parameters**: `env` — buffer receiving the saved context.
- **Returns**: `0` when called directly; `val` (the `longjmp`
  argument) when the context is restored.
- **Details**: Saves `ebx/esi/edi/ebp/esp/eip`. Non-`volatile`
  locals modified between `setjmp` and `longjmp` have indeterminate
  values on the second return (C11 7.13.2.1); `volatile` locals are
  safe — the compiler must reload them from memory.

### `_Noreturn void longjmp(jmp_buf env, int val)`

- **Parameters**: `env` — buffer previously filled by `setjmp`;
  `val` — value `setjmp` will return.
- **Returns**: never returns.
- **Details**: Restores the saved context; `val == 0` is converted
  to `1` (so `setjmp` never appears to return `0` twice).
