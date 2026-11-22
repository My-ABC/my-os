[English](../en/06-memory-isolation.md) | 中文

# 06 —— 内存隔离（每进程页表）

每个 Ring 3 进程通过 `struct user_mm`（`src/mm/paging.c`，
`USER_MM_MAX = 8` 个槽位）拥有独立地址空间。

## 布局

- **私有 PDPT[0] -> 页目录**，覆盖低 1 GiB 用户空间。页表经 KMAP 窗口
  按需分配，因此修改页表不依赖当前加载的 CR3。
- **PDPT[3] -> 共享内核页目录**，与内核自身条目相同，因此内核半区——
  包括 KMAP 窗口——在所有地址空间中一致，内核代码可以在任意 CR3 下运行。
- **用户 PD 中私有副本的低 4 MiB supervisor 恒等映射**。它保证任何
  CR3 切换后 `.boot.text` 中断/syscall stub 与低地址初始内核栈
  （`0x11B000`）仍可达；正因如此 `paging_switch_cr3` 可以在普通 C
  函数中调用而不会让返回路径缺页。

## 规则

- 每次上下文切换都更换 CR3：`ring3_timer_switch`（双向抢占）、
  `PROC_YIELD` 以及两条终止路径（`PROC_EXIT`、致命信号、`ring3_kill`）。
  CR3 重载会冲刷所有非全局 TLB 项。
- ISR14 按需分页只向*正在运行*的进程的 mm 映射
  （`ring3_current_mm()`）；`ring3_user_range_permitted()` 因此只查询
  当前进程的镜像/栈范围。
- 信号 restorer trampoline 在调度时按进程安装；信号投递会临时切换
  CR3，把 handler 帧写到目标进程私有的用户栈上。
- `paging_mm_release()` 遍历私有页表，释放所有自有用户页——栈、ELF
  镜像、用户堆、trampoline——连同页表页本身。这也回收了旧的共享页表
  拆除路径会泄漏的用户堆页。
- 借入用户空间的内核页（低恒等映射、`ring3` 内建命令使用的链接器
  `.user.text` 代码页）以 PTE bit 9（`PAGE_BORROWED`）标记，
  `paging_mm_release` 永不释放它们。
- 4 MiB 以下和 1 GiB 以上的用户映射都会被拒绝，使低恒等区与未使用的
  PDPT[1..2] 区间保持内核专用。

## 验证

`make test-qemu` 全套通过：Ring 3 调度与前后台切换、SIGINT handler
投递与 `SIG_RETURN`、ELF 加载执行、栈按需映射、非法访问终止与
OOM 回收。隔离是结构性的——进程无法观察另一进程的页，因为这些页
根本不在它的页表中。
