# 13 — 信号

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
- `sig_blocked` — 被屏蔽信号位掩码；已投递的信号在其 handler 执行期
  间被屏蔽（暂无 syscall 修改它）
- `sig_handler[NSIG]` — 每个信号的处置：`SIG_DFL`、`SIG_IGN` 或
  用户态处理器地址
- `sig_active` / `sig_saved_frame` — 当前正在运行的 handler 所对应
  的信号，以及它返回时恢复的被中断 `irq_frame`

进程槽位每次调度时状态都会重置。

## 投递

投递采用在安全点轮询，而非中断驱动：

- `ring3_signal_scan()` 在每次 PIT tick（IRQ0）中由
  `ring3_timer_switch` 调用。有致命、未屏蔽、pending 信号的进程被
  标记为 `DEAD`，随后由正常的上下文切换路径回收。当前运行进程只
  在被中断帧为用户态帧时才被标记——内核态帧意味着进程正处于系统
  调用中。
- `IO_READ`/`IO_GETCHAR` 使用信号感知的等待循环，轮询
  `ring3_signal_pending_fatal()` 和
  `ring3_signal_deliver_current()`，因此阻塞在键盘输入上的进程也能
  被及时中断。致命终止复用 `PROC_EXIT` 的已保存内核帧机制。

`sig_actionable()` 的处置逻辑：

- `SIG_IGN` → 清除 pending 位。
- `SIG_DFL` 且信号致命 → 终止进程。
- `SIG_DFL` 且信号默认忽略 → 丢弃 pending 位（目前没有此类信号）。
- 自定义用户 handler → 投递编号最小的 pending 已处理信号（见下）。

## Handler 投递

当进程存在未屏蔽且带自定义 handler 的 pending 信号时，
`sig_deliver()` 改写即将恢复它的 `irq_frame`：

- 被中断的帧复制到 `sig_saved_frame`。
- 在用户栈上放置两个字的调用帧：`[esp]` = restorer 地址、
  `[esp+4]` = 信号编号，并把 `eip`/`useresp` 指向 handler，于是
  handler 看到的是普通的 cdecl `handler(sig)` 调用。
- restorer 是固定用户页 `0x00900000`（`USER_SIG_TRAMPOLINE`，首
  次投递时惰性映射），内容为 `mov eax, SIG_RETURN; int 0x80`。
  handler 返回后，桩恢复保存的帧——包括寄存器、`eip` 和
  `useresp`——执行正好从被中断处继续。
- 已投递的信号在 handler 执行期间加入 `sig_blocked`，由
  `SIG_RETURN` 解除屏蔽。

投递发生在 IRQ0 的 `ring3_signal_scan()`（对运行中进程的实时帧和
READY 进程的已存帧都有效）以及 `IO_READ`/`IO_GETCHAR` 等待循环中
的 `ring3_signal_deliver_current()`。handler 运行期间（`sig_active`
非零）新到达的已处理信号推迟到下一 tick；致命信号仍立即终止。投递
到系统调用帧会放弃该系统调用——没有重启或 `EINTR` 返回。

## 信号来源

- **Ctrl+C**：键盘驱动跟踪 Ctrl 修饰键，产生
  `KEYBOARD_KEY_SIGINT`（0x05）控制字节而非字符 `'c'`。Shell 输入
  循环消费该字节，对前台进程调用 `ring3_signal_send()` 发送
  `SIGINT`；无前台进程时忽略。Shell 消费该字节时回显 `^C`、丢弃
  当前输入行，并在前台进程结束后重绘提示符。若
  `IO_READ`/`IO_GETCHAR` 等待循环
  先读到该字节，则改为向自身投递 `SIGINT`，因此阻塞在键盘输入上的
  进程同样会被中断（仍受其处置影响）。
- **`SIG_KILL`（`SYS_SIG`/1）**：`ecx` = pid、`edx` = 信号编号；把信号置为
  目标进程的 pending。只接受 `SIGINT`/`SIGKILL`，保留编号返回 -1。
  返回 0 或 -1。
- **`SIG_SIGNAL`（`SYS_SIG`/2）**：`ecx` = 信号编号、`edx` = 处理器
  （0 = 默认、1 = 忽略、否则为 `USER_SPACE_END` 以下的用户地址）。
  设置*当前*进程对该信号的处置；`SIGKILL` 不可捕获，保留编号返回
  -1。返回 0 或 -1。
- **`SIG_RETURN`（`SYS_SIG`/3）**：无参数；恢复投递 handler 时保存的上
  下文。仅由内核放置的 restorer 桩调用——用户代码不应直接调用。
  返回恢复的 `eax`；无活动 handler 时返回 -1。

## 当前限制

- 只跟踪一层 handler：`sig_active` 置位期间，后续已处理信号保持
  pending 而不嵌套投递（致命信号仍立即终止）。
- 被中断的系统调用被放弃，而不是重启或上报（未实现
  `EINTR` 语义）。
- handler 的返回值被丢弃；`sigreturn` 恢复被中断上下文的 `eax`。
- 没有 `sigprocmask`/`sigpending`/`sigsuspend` 系统调用；
  `sig_blocked` 仅在 handler 执行期间由内核内部管理。
- 没有 `wait`/退出状态或 IPC。
- 同步信号（如页错误时的故障信号）尚未生成；故障仍直接终止进
  程。
- `SIGSTOP`/作业控制未实现。
