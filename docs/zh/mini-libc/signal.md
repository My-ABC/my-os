# `signal.h`

`typedef int sig_atomic_t` `typedef void (*sig_t)(int)`
`SIG_ERR SIG_DFL SIG_IGN` `SIGINT=2` `SIGKILL=9` `NSIG=32`。

`sig_t signal(int sig, sig_t func)` —— 安装处理器，返回旧
disposition（失败 `SIG_ERR`）。仅 **SIGINT** 可捕获
（Ctrl+C 递送）；SIGKILL 及其他编号一律拒绝。
用户态表记录 disposition。
`int kill(pid_t pid, int sig)` `int raise(int sig)`。

处理器 ABI：以 cdecl 帧进入用户栈
（`[esp]=内核 restorer, [esp+4]=sig`），普通
`void handler(int sig)` 即可；`return` 后由内核
restorer 恢复被中断上下文。
无 `sigprocmask`/`sigaction`/`sigaltstack`——ABI 无
掩码支持。
