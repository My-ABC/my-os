# `assert.h` — assertions

## `assert(e)`

- **Parameters**: `e` — expression to test.
- **Returns**: nothing (void expression).
- **Details**: When `e` evaluates to false, prints the expression
  text, file, function, and line to stdout, then calls `abort()`.
  When `NDEBUG` is defined at compile time the macro expands to
  `((void)0)` and generates no code.

## `_Noreturn void __assert_fail(const char *expr, const char *file, unsigned line, const char *func)`

- **Parameters**: `expr` — stringized expression; `file` —
  `__FILE__`; `line` — `__LINE__`; `func` — `__func__`.
- **Returns**: never returns.
- **Details**: The underlying noreturn helper invoked by `assert`;
  normally not called directly.
