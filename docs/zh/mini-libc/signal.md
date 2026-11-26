# `signal.h` — 信号

基于 `SYS_SIG`。内核恰好实现两个信号：`SIGINT`（可捕获，
Ctrl+C 投递）和 `SIGKILL`（不可捕获，必然致命）。没有
`sigprocmask`/`sigaction` 屏蔽支持——`signal()` 就是全部接口。

## 类型与常量

```c
typedef int sig_atomic_t;
typedef void (*sig_t)(int);

#define SIG_ERR ((sig_t)-1)
#define SIG_DFL ((sig_t)0)
#define SIG_IGN ((sig_t)1)

#define SIGINT  2
#define SIGKILL 9
#define NSIG    32
```

## 函数

### `sig_t signal(int sig, sig_t func)`

- **参数**：`sig` — 信号编号；`func` — 处理函数、`SIG_DFL`
  （默认）或 `SIG_IGN`（忽略）。
- **返回值**：先前的处置方式；失败返回 `SIG_ERR`。
- **详细说明**：只有 `SIGINT` 可捕获；为 `SIGKILL` 安装处理函数
  或 `SIG_IGN` 会失败。捕获的处理函数在用户栈上以 cdecl 帧运行
  （`[esp]=restorer`、`[esp+4]=sig`），因此普通的
  `void handler(int sig)` 即可；处理函数返回后自动恢复被中断的
  上下文。实践中处理函数应是异步信号安全的——被中断的上下文可
  能正处于任意系统调用内部。

### `int kill(pid_t pid, int sig)`

- **参数**：`pid` — 目标进程；`sig` — `SIGINT` 或 `SIGKILL`。
- **返回值**：成功 `0`；失败 `< 0`（pid 无效/已死返回 `ESRCH`）。
- **详细说明**：`SIGKILL` 不可捕获且必然致命。信号在目标下一个
  信号检查点（阻塞类系统调用）投递；发给**当前**进程的信号在
  系统调用返回前同步检查。

### `int raise(int sig)`

- **参数**：`sig` — 信号编号。
- **返回值**：成功 `0`，失败 `< 0`。
- **详细说明**：等同 `kill(getpid(), sig)`。
