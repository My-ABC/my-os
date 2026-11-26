# `setjmp.h` — 非局部跳转

仅保存调用者保存寄存器；不保存信号掩码。

## 类型

```c
typedef int jmp_buf[6];   /* ebx esi edi ebp esp eip */
```

## 函数

### `int setjmp(jmp_buf env)`

- **参数**：`env` — 接收保存上下文的缓冲区。
- **返回值**：直接调用时返回 `0`；上下文被恢复时返回 `val`
  （`longjmp` 的参数）。
- **详细说明**：保存 `ebx/esi/edi/ebp/esp/eip`。在 `setjmp` 与
  `longjmp` 之间被修改的 volatile 局部变量在第二次返回时值不
  确定——适用标准 C 规则。

### `_Noreturn void longjmp(jmp_buf env, int val)`

- **参数**：`env` — `setjmp` 填充过的缓冲区；`val` — `setjmp`
  将返回的值。
- **返回值**：永不返回。
- **详细说明**：恢复保存的上下文；`val == 0` 会被转为 `1`（因
  此 `setjmp` 不会看起来返回两次 `0`）。
