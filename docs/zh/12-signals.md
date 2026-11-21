# 12 — 信号

MyOS 为 Ring 3 进程实现了基础的信号机制。信号编号定义于
`include/os/signal.h`。

## 信号编号

目前只实现两个信号，1–31 范围内的其他编号为保留：

| 编号 | 名称 | 默认动作 |
|------|------|----------|
| 2 | `SIGINT` | 终止（由 Ctrl+C 产生） |
| 9 | `SIGKILL` | 终止，不可捕获或屏蔽 |

`NSIG` 为 32，pending/blocked 位掩码和 handler 表有扩展空间。
`SIG_DFL`（0）选择默认动作，`SIG_IGN`（1）忽略信号。`SIGKILL`
不可捕获（`SIG_UNCATCHABLE`）；目前没有默认忽略的信号。

## 每进程状态

每个 `struct ring3_process` 携带：

- `sig_pending` — 已发送但未处理的信号位掩码
- `sig_blocked` — 被屏蔽信号位掩码（已预留；暂无 syscall 修改它）
- `sig_handler[NSIG]` — 每个信号的处置：`SIG_DFL`、`SIG_IGN` 或
  用户态处理器地址

进程槽位每次调度时状态都会重置。

## 投递

投递采用在安全点轮询，而非中断驱动：

- `ring3_signal_scan()` 在每次 PIT tick（IRQ0）中由
  `ring3_timer_switch` 调用。有致命、未屏蔽、pending 信号的进程被
  标记为 `DEAD`，随后由正常的上下文切换路径回收。当前运行进程只
  在被中断帧为用户态帧时才被标记——内核态帧意味着进程正处于系统
  调用中。
- `SYS_READ`/`SYS_GETCHAR` 使用信号感知的等待循环，轮询
  `ring3_signal_pending_fatal()`，因此阻塞在键盘输入上的进程也能
  被及时终止。终止复用 `SYS_EXIT` 的已保存内核帧机制。

`sig_deliverable_mask()` 的处置逻辑：

- `SIG_IGN` → 清除 pending 位。
- `SIG_DFL` 且信号致命 → 终止进程。
- `SIG_DFL` 且信号默认忽略 → 丢弃 pending 位（目前没有此类信号）。
- 自定义用户 handler → pending 位保留；**用户态 handler 调用尚未实
  现**，信号会一直 pending 直到进程退出。

## 信号来源

- **Ctrl+C**：键盘驱动跟踪 Ctrl 修饰键，产生
  `KEYBOARD_KEY_SIGINT`（0x05）控制字节而非字符 `'c'`。Shell 输入
  循环消费该字节，对前台进程调用 `ring3_signal_send()` 发送
  `SIGINT`；无前台进程时忽略。该字节也可能以原始 0x05 到达
  `SYS_READ`/`SYS_GETCHAR` 的消费者。
- **`SYS_KILL`（22）**：`ebx` = pid、`ecx` = 信号编号；把信号置为
  目标进程的 pending。返回 0 或 -1。
- **`SYS_SIGNAL`（23）**：`ebx` = 信号编号、`ecx` = 处理器
  （0 = 默认、1 = 忽略、否则为 `USER_SPACE_END` 以下的用户地址）。
  设置*当前*进程对该信号的处置；`SIGKILL` 不可捕获。
  返回 0 或 -1。

## 当前限制

- 自定义信号 handler 已存储但不会被调用；pending 的自定义信号会累
  积。
- 没有 `sigprocmask`/`sigpending`/`sigsuspend` 系统调用；
  `sig_blocked` 存在但没有任何东西设置它。
- 没有 `wait`/退出状态或 IPC。
- 同步信号（如页错误时的故障信号）尚未生成；故障仍直接终止进
  程。
- `SIGSTOP`/作业控制未实现。
