# 05 — Ring 3 进程

[English](../en/05-processes.md) | 中文

## 进程表（`src/os/ring3.c`）

最多 `RING3_PROCESS_COUNT = 4` 个并发 Ring 3 进程。状态：`UNUSED`、
`READY`、`RUNNING`、`DEAD`（`include/os/ring3.h`）。每个进程带 `vt`
字段（0 或 1）绑定到某个虚拟终端，拥有一个用户栈（32 KiB，栈顶
`0x00810000` 或 `0x00820000`）、一个入口点、可选的 `parent_pid`，
以及供页错误处理器使用的 ELF 镜像区间记录。前台 PID **按 vt 分槽**
跟踪（`foreground_pid[VT_COUNT]`）。

固定的每进程用户地址：

| 地址 | 用途 |
|---|---|
| `0x00810000`、`0x00820000` | 用户栈顶（`USER_STACK_TOP_0/1`），各 32 KiB |
| `0x00900000` | 每进程 sigreturn 跳板（`USER_SIG_TRAMPOLINE`）；保留地址，测试 ELF 不要加载到这里 |
| `0x01000000`–`0x40000000` | 用户堆（`USER_HEAP_START`/`USER_HEAP_END`） |

## 生命周期

- `ring3_schedule(entry)` / `ring3_schedule_mm(...)` /
  `elf_run_vt(image, size, vt)` —— 把入口页映射为
  `USER|WRITABLE|PRESENT`，在指定 vt 上登记进程（子进程继承父 vt）
  并交给调度器运行。`ring3_schedule_mm` 在入口处接管调用方的 `mm`
  所有权：成功时归进程槽位所有，任何失败路径都会将其释放，调用方
  之后不得再释放。
- `ps` / `fg <pid>` / `bg <pid>` / `kill <pid>` —— Shell 命令，分别封
  装 `ring3_process_list`、`ring3_set_foreground`、
  `ring3_set_background`、`ring3_kill`。
- 前台/后台只抑制进程的控制台输出，两者都继续运行。前台查询、
  Ctrl+C 投递和 `IO_WRITE` 门控都限定在进程所属的 vt 内。
- 前台子进程退出时，`ring3_foreground_release()` 在父进程仍存活于
  同一 vt 的情况下把 `parent_pid` 恢复为该 vt 前台——避免 `run`
  子进程结束后出现双提示符。
- 某 vt 的前台进程运行时，该 vt 的 Shell 隐藏提示符并停止消费键盘，
  按键留在缓冲区里等子进程或子进程退出后的 Shell 读取。Ctrl+C 在
  键盘 ISR 中处理——ISR 直接向*当前可见* vt 的前台进程投递
  `SIGINT`。
- `ring3_terminate_current()` 由 `isr14_handler`、`PROC_EXIT` 和致命
  信号路径调用：标记 `DEAD`、释放资源（栈页、堆、镜像映射）、释放
  前台所有权、恢复 `esp0`，并把内核 `irq_frame` 交还给 Shell 继续
  执行。
- `shell3_supervisor()`（kernel.c）让每个 vt 保持一个 `shell3.elf`
  存活：死亡或缺失的 shell 由 `vt_service()` 重新拉起——见
  [15-virtual-terminals.md](15-virtual-terminals.md)。

## ELF 加载（`src/os/elf.c`）

`elf_run_vt(image, size, vt)` 解析 32 位 ELF，把 PT_LOAD 段映射进用
户区，搭好初始栈帧后在指定 vt 上调度进程（`elf_run` 默认用调用者
的 vt）。使用它的路径：

- `run <file>` —— 从 FAT32 把 ELF32 文件读入 1 MiB 内核缓冲
  （`FAT32_MAX_FILE_SIZE`），以前台身份调度到调用者 vt（内核
  `run` 命令与 `PROC_EXEC` 同路径）。
- `shell3_supervisor` 的 vt spawner —— 从 FAT32 把 `/shell3.elf`
  加载到每个 vt。
- `elf` / `PROC_RUN_USERCODE` —— 链接进内核镜像的 ELF32 内建演示
  （`boot/elf_demo.asm`）。

## 内存隔离

每个 Ring 3 进程拥有独立 CR3 的私有地址空间——详见
[06-memory-isolation.md](06-memory-isolation.md)。

## 限制

- 没有 `wait`/退出状态、更多信号编号或 IPC。
- 系统调用接口见 [04-syscall-abi.md](04-syscall-abi.md)。
