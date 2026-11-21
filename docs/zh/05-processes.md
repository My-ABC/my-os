# 05 — Ring 3 进程

[English](../en/05-processes.md) | 中文

## 进程表（`src/os/ring3.c`）

最多 `RING3_PROCESS_COUNT = 2` 个并发 Ring 3 进程。状态：`UNUSED`、
`READY`、`RUNNING`、`DEAD`（`include/os/ring3.h`）。每个进程拥有一
个用户栈（`0x810000` 或 `0x820000`，32 KiB）、一个入口点，以及供页错
误处理器使用的 ELF 镜像区间记录。

## 生命周期

- `ring3_schedule(entry)` —— 把入口页映射为 `USER|WRITABLE|PRESENT`，
  登记进程并交给调度器运行。
- `ps` / `fg <pid>` / `bg <pid>` / `kill <pid>` —— Shell 命令，分别封
  装 `ring3_process_list`、`ring3_set_foreground`、
  `ring3_set_background`、`ring3_kill`。
- 前台/后台只抑制进程的控制台输出，两者都继续运行。
- `ring3_terminate_current()` 由 `isr14_handler` 在用户进程非法 fault
  时调用：标记 `DEAD`、释放资源（栈页、堆、镜像映射）、恢复 `esp0`，
  并把内核 `irq_frame` 交还给 Shell 继续执行。

## ELF 加载（`src/os/elf.c`）

`elf_run(image, size)` 解析 32 位 ELF，把 PT_LOAD 段映射进用户区，搭好
初始栈帧后调度进程。Shell 有两条路径使用它：

- `elf` —— 运行链接进内核镜像的 ELF32 演示
  （`boot/elf_demo.asm`）。
- `run <file>` —— 从 FAT32 把 ELF32 文件读入 1 MiB 内核缓冲
  （`FAT32_MAX_FILE_SIZE`）后调用 `elf_run`。

## 限制

- 没有按进程地址空间隔离：所有 Ring 3 进程共享内核页表和彼此的用户
  区间。
- 没有 `wait`/退出状态、信号或 IPC。
- 系统调用接口见 [04-syscall-abi.md](04-syscall-abi.md)。
