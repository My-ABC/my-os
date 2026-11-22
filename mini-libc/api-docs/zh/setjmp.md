# `setjmp.h`

`typedef int jmp_buf[6]` —— 保存 ebx/esi/edi/ebp/esp/eip。
`int setjmp(jmp_buf env)` —— 首次返回 0，之后以
`longjmp` 的值再次返回。
`_Noreturn void longjmp(jmp_buf env, int val)` —— 按 C11，
`val==0` 强转为 1。

setjmp 与 longjmp 之间修改的 volatile 局部变量在第二次
返回时值不确定（C 标准规则）。不保存信号掩码
（ABI 无 sigprocmask）。
