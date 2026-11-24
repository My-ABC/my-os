# `setjmp.h`

`typedef int jmp_buf[6]` —— 保存 ebx/esi/edi/ebp/esp/eip。
`int setjmp(jmp_buf env)` —— 首次返回 0，之后以
`longjmp` 的值再次返回。
`_Noreturn void longjmp(jmp_buf env, int val)` —— 按 C11，
`val==0` 强转为 1。

未经 `volatile` 限定、属于包含 `setjmp` 的函数、且在 `setjmp` 与
`longjmp` 之间被修改的局部变量，在 `longjmp` 返回后值不确定。
`volatile` 限定的局部变量是安全的——编译器必须从内存重新读取其值。
（ABI 无 sigprocmask）。