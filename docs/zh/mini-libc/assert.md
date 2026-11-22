# `assert.h`

`assert(e)` —— 求值 `e`；失败时调用
`__assert_fail(#e, __FILE__, __LINE__, __func__)`，向
**stdout**（无 stderr）打印 `Assertion failed: <expr>`
及文件/函数/行号，然后 `abort()`。`-DNDEBUG` 下整体
编译为空。
