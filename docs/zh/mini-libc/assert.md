# `assert.h` — 断言

## `assert(e)`

- **参数**：`e` — 要测试的表达式。
- **返回值**：无（void 表达式）。
- **详细说明**：`e` 为假时，把表达式文本、文件、函数和行号打印
  到 stdout，然后调用 `abort()`。编译时定义 `NDEBUG` 则宏展开为
  `((void)0)`，不生成任何代码。

## `_Noreturn void __assert_fail(const char *expr, const char *file, unsigned line, const char *func)`

- **参数**：`expr` — 字符串化的表达式；`file` — `__FILE__`；
  `line` — `__LINE__`；`func` — `__func__`。
- **返回值**：永不返回。
- **详细说明**：`assert` 调用的底层 noreturn 辅助函数；通常不
  直接调用。
