# `assert.h`

`assert(e)` — evaluates `e`; on failure calls
`__assert_fail(#e, __FILE__, __LINE__, __func__)` which prints
`Assertion failed: <expr>` + file/func/line to **stdout** (no
stderr exists) then `abort()`. Compiled out entirely under
`-DNDEBUG`.
