# MyOS 项目 TODO（中文）

此文件记录已完成的项目工作和后续路线图。完成条目后，应同时勾选 [TODO.md](./TODO.md) 与本文件，并同步维护中英文 README 和内核 agent 指引。

## 已完成

### 内核、内存与程序运行

- [x] 建立 32 位 x86 Multiboot/GRUB 启动流程，以及 GDT、IDT、TSS 和基础中断处理。
- [x] 实现内核分页、PAE 页表和高端物理页映射；PMM 可管理 64 GiB 以下的物理地址，并有 4 GiB 以上页分配的宿主测试；可选的 QEMU `-m 6G` 测试会分配 `0x100000000` 高地址物理页，通过映射校验读写，然后释放并重新分配该页。
- [x] 实现内核堆分配器及基础用户堆分配接口。
- [x] 实现基础 Ring 3 进程调度和进程管理命令；最多四个并发用户进程（`RING3_PROCESS_COUNT`）。
- [x] 提供基础系统调用 `IO_WRITE` 和 `PROC_GETPID`。
- [x] 实现 ELF32 可加载段校验与装载、BSS 清零，以及内置和磁盘 ELF 程序运行。
- [x] 修复 PAE 更改后 GRUB ISO 启动时可能出现的无限重启；构建依赖自动跟踪头文件，避免 ABI 变更后复用过期对象文件。
- [x] 将系统调用 ABI 重构为两级大类/子操作号：`eax` 选择七个大类之一（`SYS_IO`/`SYS_TIME`/`SYS_MEMORY`/`SYS_POWER`/`SYS_PROCESS`/`SYS_FS`/`SYS_SIG`），`ebx` 为子操作号，参数移至 `ecx`/`edx`/`esi`；新增 `TIME_TO` 支持 UTC±n 偏移换算，并同步更新 sigreturn trampoline、全部汇编演示、契约测试和双语 ABI 文档。

### ATA、FAT32 与 Shell

- [x] 实现 ATA 主盘 PIO/LBA28 扇区读写及启动时设备探测；未检测到磁盘时不显示磁盘路径提示符。
- [x] 实现 FAT32 挂载、文件和目录操作，并在检测到 ATA 磁盘时自动尝试挂载。
- [x] 实现 FAT32 ASCII 长文件名（LFN）及 8.3 短别名；支持 `help_aaaaa.1234` 等长文件名，并校验 LFN 序号和校验和。
- [x] 实现 `utf16LE_to_ascii` 和 `ascii_to_utf16LE` 转换接口。
- [x] 实现 Shell 当前路径提示符、现有目录导航和 `cd ..`；`cd` 不创建目录，`ls` 默认列出当前目录并支持指定路径。
- [x] 实现 `rmdir -f` 递归删除非空目录，并限制删除当前目录及其父目录。
- [x] 实现 `cp`/`cp -r`/`mv` 文件和目录操作，按 Linux 语义处理目标已存在的情况。
- [x] 实现 `run` 命令，从 FAT32 磁盘装载并运行 ELF32 程序。
- [x] 增加文件系统调用 `FS_OPEN`/`CLOSE`/`READ`/`WRITE`/`UNLINK`/`MKDIR`/`RMDIR`/`STAT`/`GETCWD`/`CHDIR`/`RENAME`/`READDIR`（`SYS_FS` 子操作 1–12）、`PROC_EXIT`/`PROC_YIELD`/`PROC_LIST`/`PROC_SET_FG`/`PROC_EXEC`（`SYS_PROCESS`）、`TIME_GET`（`SYS_TIME`）以及信号调用 `SIG_KILL`/`SIG_SIGNAL`/`SIG_RETURN`（`SYS_SIG`）；用户指针按页映射存在性校验（`sys_user_buffer_ok`），路径统一走 `fat32_resolve_path`/`g_cwd`。
- [x] 端到端实现信号投递：仅 `SIGINT`/`SIGKILL`，每进程 pending/blocked/handler 状态，Ctrl+C `KEYBOARD_KEY_SIGINT` → 前台 `SIGINT`，默认致命即终止，用户 handler 经每进程私有的 `0x00900000` restorer trampoline + `SIG_RETURN` 恢复上下文执行。
- [x] 实现 Shell 行编辑：16 条历史（Up/Down）、光标移动（Left/Right）、插入与退格编辑及整行重绘、双引号参数、`\n`/`\\`/`\"` 转义（未知转义告警）、Ctrl+C 回显 `^C` + 丢弃当前行 + 重绘提示符、ELF/`run` 错误信息细分（镜像错误 vs 进程槽满 vs 读文件失败）。
- [x] `echo` 同时输出屏幕和 COM1 串口，脚本化/无头会话保留串口可见性；移除 `secho` 命令。
- [x] 新增 unittest 风格 QEMU 集成框架（`QemuTestCase` 及 `assert*` 断言、按方法输出 PASS），与原有有界会话编排并存；检查结果以 `✓ PASS`/`✗ FAIL` 标记。
- [x] 修复 RTC 12/24 小时制：PM 位在 BCD 解码前提取，解码后小时 >12 时按 24 小时值处理（部分 RTC 模式位不可靠），所有 QEMU 调用统一 `-rtc base=utc` 使客体 RTC 读 UTC 供 UTC→北京时间换算。`time` 支持 `time 12`/`time 24`。
- [x] 为 `vbe_put_pixel` 增加坐标边界检查（越界像素串口告警，不再间歇性内核 panic）。
- [x] 实现虚拟终端：F1/F2 切换的两个 tty，各自运行独立的 `shell3.elf` Ring 3 进程并保留各自屏幕；每 vt 独立的键盘缓冲与前台 pid 槽位，经 `vt_service` 延迟切换，VBE 模式下在 `0xD0000000`–`0xE0000000` 窗口分配离屏帧缓冲，并自动重启各终端的 shell。详见 `docs/zh/15-virtual-terminals.md`。
- [x] 将 Ring 3 `IO_WRITE` 输出镜像到 COM3（单一全局流，COM1 保留内核侧输出），并扩展 QEMU 测试框架分别采集 COM1/COM3 两路串口。
- [x] 通过 `shell3_supervisor` vt spawner 开机自动启动 `/shell3.elf` 并在每个终端自动重启；仅当未挂载磁盘或 ELF 缺失/无法装载时才回退到内核 shell。

### 驱动、控制台与电源

- [x] 实现基础平台驱动：PIC 中断控制器初始化、PIT IRQ0 定时器、PS/2 键盘扫描码解码（含 Ctrl+C `KEYBOARD_KEY_SIGINT` 注入）、VGA 文本控制台、基于 GRUB multiboot 信息的 VBE framebuffer 初始化（像素绘制带边界检查）、COM1 串口输出和 RTC。
- [x] 实现 ACPI 表发现与 FADT 解析，包括经 PM1a 控制块的 S5 关机；表或寄存器不可用时优雅回退。
- [x] 按 P0–P3 审计报告（`AUDIT.md`）加固系统调用与串口边界：串口 TX 等待加超时（无 UART 硬件时不再永久挂死）、`IO_WRITE` 经 4096 字节有界内核缓冲拷贝用户字符串、`IO_READ` 校验缓冲与长度、用户指针改为按页映射存在性校验（`sys_user_buffer_ok`/`paging_mm_get_phys`）。

### 项目结构、测试与文档

- [x] 整理源码和头文件布局，保留 `src/kernel.c` 为内核入口，并按 `arch/x86`、`mm`、`os`、`drivers`、`fs`、`lib` 分类。
- [x] 按用途整理测试目录，将 Python、宿主 C、Shell 启动脚本和 QEMU 集成测试分开放置；新增可选的 `make test-qemu-highmem` 测试，验证真实高地址物理页分配与访问。
- [x] 建立宿主和 QEMU 集成测试，覆盖 PAE/PMM、高地址物理页分配与访问、FAT32 LFN、Shell 操作、Ring 3 进程及磁盘 ELF 运行。
- [x] 建立项目级 TODO 路线图，并在中英文 README 和内核 agent 指引中记录维护入口。
- [x] 在 `docs/en` 和 `docs/zh` 下建立双语架构文档系列（从启动到虚拟终端的编号章节、驱动缺口分析及内存隔离章节），语言链接保持同步；各章节已在 vt-f1-f2 审计期间对照当前代码逐一核对。

## 内核、用户态与系统调用

- [x] **完善 ISR14 页错误处理**
  - [x] 根据错误码和被中断的特权级区分用户态与内核态缺页。
  - [x] 只对已登记的 ELF 段、用户堆和用户栈按需分配页面。
  - [x] 区分未映射页与保护违规；非法用户访问应终止对应进程，内核态非法缺页应进入 panic。
  - [x] 用户页映射前清零；分配或映射失败时正确回收资源。
  - [x] 覆盖有效按需映射、越界访问、保护违规、内核态缺页和内存不足测试。（QEMU 通过 `stack.elf`/`faultu.elf`/`faultp.elf`/`oom.elf`/`kfault.elf` 全部覆盖；内核态缺页 panic 在独立的第二个会话中运行，因为它会重启机器。）
  - 入口：[isr14_handler](./src/arch/x86/trap.c)。
- [x] **实现高精度定时等待**
  - [x] 用 PIT 校准 TSC 并提供时间戳/延时辅助接口。
  - [x] 新增 `SYS_TIME`/`TIME_SLEEP` 系统调用，参数为微秒级时长。
  - [x] 用 QEMU 测试验证亚毫秒级睡眠精度与信号检查。
- [ ] **完善用户态与系统调用接口**
  - [ ] 定义稳定、版本化的系统调用 ABI、参数约定和错误码。（`eax`/`ebx` 两级大类/子操作号、参数走 `ecx`/`edx`/`esi` 已记录在 `docs/zh/04-syscall-abi.md`；尚无版本机制。）
  - [ ] 校验用户指针、长度和地址范围，提供安全的用户/内核数据复制。（FS 与控制台系统调用已按页映射存在性校验指针（`sys_user_buffer_ok`/`paging_mm_get_phys`）；`MEMORY_REALLOC` 仍无校验解引用块头，保留为 `kfault.elf` panic 演示路径。）
  - [ ] 完善进程创建、退出、等待、信号/事件及资源回收接口。（已有 `PROC_EXIT`/`PROC_YIELD`/`PROC_LIST`/`PROC_SET_FG`/`PROC_EXEC` 和信号集；仍无 fork/wait 或每进程 fd 表。）
  - [ ] 增加文件、目录、控制台、时间和设备访问所需的系统调用。（`SYS_FS` 子操作 1–12、`TIME_GET`/`TIME_TO`/`TIME_SLEEP`、控制台读写已有；设备访问仍缺。）
  - [ ] 为 ABI 和非法参数处理添加宿主与 QEMU 测试。（QEMU 覆盖正常路径和信号投递；非法参数覆盖不完整。）
- [ ] **实现信号机制**（目前只有 `SIGINT`/`SIGKILL`；更多信号编号、`sigprocmask` 式屏蔽接口和嵌套投递仍待做）
  - [x] 定义信号编号、投递语义（在 IRQ0/系统调用等待点轮询，而非同步异常）以及每进程的待处理/屏蔽信号状态。
  - [x] 增加向进程发送信号（`SIG_KILL`）和注册/替换用户态处理函数（`SIG_SIGNAL`）的系统调用；handler 返回后由 `SIG_RETURN` 恢复被中断的上下文。
  - [x] 确保投递不会破坏 `irq_frame` 约定或进行中的系统调用；未处理的致命信号应终止进程。（投递时保存完整帧并在用户栈上重建；投递进系统调用帧会放弃该调用而非重启。）
  - [x] 用 QEMU 测试覆盖投递、屏蔽和终止路径。（`sigdemo.elf` 验证 handler 调用、寄存器恢复与继续执行；Ctrl+C 终止由 Ring 3 场景覆盖。）
- [x] **实现每进程页表以获得真正的内存隔离**
  - [x] 让每个 Ring 3 进程拥有独立用户地址空间（上下文切换时切换 CR3），不再共享同一页表。（每个 `struct user_mm` 私有 PDPT[0]->PD，共享内核 PDPT[3]；CR3 在 `ring3_timer_switch`/yield/terminate 时切换。）
  - [x] 区分每进程映射（用户堆、栈、ELF 段）与共享映射（内核半区、低 4 MiB supervisor 恒等映射）。（信号 trampoline 按进程私有；`.user.text`/低恒等页为借用页，用 PTE bit 9 标记。）
  - [x] 处理 CR3 切换时的 TLB 失效、缺页归属以及进程退出时释放每进程映射。（CR3 重载冲刷非全局 TLB 项；ISR14 只向当前进程的 mm 按需映射；`paging_mm_release` 释放所有自有用户页。）
  - [x] 用 QEMU 测试验证进程间无法互相观察或破坏内存，且内核映射对用户不可见。（`make test-qemu` 全套通过；隔离由私有页表结构保证。）
- [ ] **实现内核诊断与日志子系统（dmesg / 环形缓冲区）**
  - [ ] 增加定长内核日志环形缓冲区，带严重级别和时间戳。
  - [ ] 将内核诊断统一走日志通道，并提供 `dmesg` 式 shell 命令用于查看/回放。
  - [ ] 定义环形缓冲区回绕行为、面向 SMP 的锁以及日志级别过滤。
- [ ] **将 Shell 完善并迁移到 Ring 3**
  - [x] 将命令解析、交互和内建命令逐步移出内核。（`src/shell/shell_ring3.c` + `include/shell/shell_ring3.h`，`make shell3` 生成 `build/shell3.elf`。）
  - [x] 通过系统调用访问控制台、文件系统、进程和设备。
  - [ ] 提供命令行编辑、历史记录、错误码和可执行程序搜索等基础体验。（两个 shell 均已有行编辑、16 条历史、转义和细分错误信息；可执行程序搜索/PATH 查找未实现。）
  - [x] 保持用户 Shell 崩溃或退出时内核仍可恢复控制。（vt 层经 `vt_service` 在每个终端自动重启 `shell3.elf`；瞬时拉起失败按约 1 Hz 重试，仅 ELF 加载错误才永久锁存。）
- [ ] **扩展虚拟终端支持**
  - [ ] 支持两个以上终端（需同步扩展 vt 数组、每终端前台槽位和 F 键映射）。
  - [ ] 增加滚动回溯缓冲；目前每个 vt 只保留一屏。
  - [ ] 将 COM3 全局镜像拆分为按终端的串口流，并让 VBE 备份分配失败不再导致切换永久失效。
  - 入口：[vt.c](./src/drivers/vt.c)、[15-virtual-terminals.md](./docs/zh/15-virtual-terminals.md)。

## 进程间通信

- [ ] **实现管道与 IPC 原语**
  - [ ] 增加匿名管道（`SYS_PIPE`），由有界内核缓冲区支撑，提供阻塞式读写语义。
  - [ ] 支持 fd 重定向，使 `cmd1 | cmd2` 形式的管道能在用户进程间工作（依赖尚不存在的每进程 fd 表）。
  - [ ] 按需扩展到命名管道或其他 IPC 原语（共享内存、消息队列等）。
- [ ] **实现输入输出重定向**
  - [ ] 在每进程 fd 表之上提供 fd 复制/重定向支持（`dup2` 式）。
  - [ ] 在用户态命令执行可用后，让 shell 支持 `>`、`>>`、`<` 重定向。

## 文件系统、存储与缓存

- [ ] **完善文件系统与 Shell 存储体验**
  - [ ] 系统化检查 FAT32 路径解析、目录边界、错误传播和磁盘损坏处理。
  - [ ] 完善文件句柄及打开/关闭/顺序读写接口，供用户态程序使用。（`SYS_FS` fd 子操作 1–4 和 mini-libc `fs.h`/`unistd.h` 封装已有；fd 表是全局的，非每进程。）
  - [x] 增加文件复制、移动、重命名等常用操作，并补齐持久化和回归测试。（`cp` 复制文件、`cp -r` 递归复制目录树；`mv` 按 Linux 语义重命名/移动文件和目录——目标为已存在目录时移入其中、目标为已存在文件时替换、禁止目录移入自身子树。）
  - [x] 明确 LFN、短文件名别名和非 ASCII 名称的支持范围及限制。（Shell 创建的目录项总是携带保留大小写的 LFN；查找按 LFN 精确匹配，仅在无 LFN 时回退 8.3 短名。详见 README。）
- [ ] **实现块缓存**
  - [ ] 为块设备提供按设备号和 LBA 索引的缓存及替换策略。
  - [ ] 定义一致性、写回/同步、失效和错误传播规则。
  - [ ] 让 FAT32 使用缓存并验证读写性能与数据一致性。
- [ ] **实现页缓存**
  - [ ] 将文件内容页与物理页管理、文件偏移及写回关联。
  - [ ] 支持缓存命中、脏页回写、失效和与块缓存的协作。
  - [ ] 为可执行文件装载及文件映射复用页缓存。

## 硬件、设备与 ACPI

- [ ] **梳理并枚举驱动生态缺口**
  - [x] 盘点现有 ATA、键盘、PIC/PIT、RTC、VGA/VBE、ACPI 驱动的能力和限制。（见 `docs/en/12-driver-gaps.md` / `docs/zh/12-driver-gaps.md`。）
  - [x] 按总线和设备类别列出缺失驱动、优先级及对应测试设备。（同一文档，含建议引入顺序。）
  - [ ] 基于实际需求逐项选择和实现驱动，不预先承诺完整兼容性。
- [ ] **建立设备子系统**
  - [ ] 设计统一的设备、总线和驱动注册/探测接口。
  - [ ] 加入设备资源管理、驱动匹配、生命周期和错误恢复。
  - [ ] 评估并实现 PCI 枚举，为后续网卡和其他 PCI 设备驱动提供基础。（已提升为下方独立的"实现 PCI 总线枚举"条目。）
- [ ] **实现 PCI 总线枚举**
  - [ ] 实现 PCI 配置空间访问（机制 1，端口 `0xCF8`/`0xCFC`）及读写辅助函数。
  - [ ] 枚举总线/设备/功能，读取厂商/设备 ID 和类代码；通过串口输出总线映射。
  - [ ] 发现并分配 BAR（内存和 I/O 窗口），不与现有 ISA 设备冲突。
  - [ ] 提供基础的 INTx 中断路由，让 PCI 设备能向 PIC/APIC 发信号。
  - [ ] 按 `docs/zh/12-driver-gaps.md` 用 QEMU `-device` 设备（e1000、ich9-ahci、qemu-xhci、bochs-display）验证。
- [ ] **增加 GPU 支持**
  - [ ] 枚举显示类 PCI 设备并识别 GPU 候选（bochs-display、virtio-gpu、VMware SVGA、QXL）。
  - [ ] 扩展 framebuffer 抽象以覆盖 GPU 提供的扫描输出表面。
  - [ ] 评估 2D/3D 加速路径（bochs 2D、virtio-gpu 或用户态图形栈）；范围保持实验性。
- [ ] **完善 ACPI**
  - [ ] 将 ACPI 表发现、映射、长度/校验和验证整理为通用且有界的接口。
  - [ ] 解析 MADT，提供处理器和中断控制器信息。
  - [ ] 评估 HPET、MCFG 等表及对应硬件功能的支持。
  - [ ] 让 `shutdown`/`reboot` 在 QEMU 下真正关机/复位：目前 PM1a S5 写入和所有复位兜底（FADT 复位寄存器、0xCF9 端口、键盘脉冲、三重错误）都无法让 guest 停止运行——疑似 AML/DSDT 或 PM1 寄存器位宽问题；用 mini-libc 的 `power.elf` 测试验证（`make test-libmyc`）。
  - [ ] 完善电源状态、关机/重启错误处理，并评估 AML/DSDT 支持范围。

## 并发与内存

- [ ] **为 Ring 3 共享状态增加内核互斥锁**
  - [ ] 引入轻量内核 mutex/临界区原语（单核下 IRQ 安全，后续可扩展到 SMP），保护被并发 Ring 3 进程共享的全局状态。
  - [ ] 覆盖 FAT32/ATA 全局缓冲（`g_sector`/`g_cluster`/`g_fs`/`g_cwd`）、全局 `sysfs_fds` 表以及用户堆 `user_free_list`/`user_heap_next`，避免 IRQ0 在操作中途调度其它进程导致状态被覆盖（AUDIT P1-5、P1-1）。
  - [ ] 对不适合加锁的对象改为按归属隔离（例如把用户堆元数据移入 `struct user_mm`），并文档化各路径持锁约定。
  - [ ] 增加 QEMU 覆盖：两个 vt 上并发 Ring 3 进程同时做 FAT32/堆操作。
- [ ] **增加空闲任务与基于 HLT 的省电**
  - [ ] 在没有可运行任务时由专用空闲循环执行 `HLT`，而不是在调度器中忙等。
  - [ ] 确保 IRQ0/PIT 及其他中断能在休眠间可靠唤醒 CPU。
  - [ ] 用 QEMU 测试验证唤醒行为和功耗下降。
- [ ] **增加 SSE 支持**
  - [ ] 按需在 CR4/XCR0 中启用 SSE 并通过 CPUID 校验可用性。
  - [ ] 为内核和用户代码提供对齐/非对齐的 load-store 与算术辅助接口。
  - [ ] 添加 QEMU 测试，覆盖上下文切换间的 SSE 上下文保存/恢复。
- [ ] **实现 SMP / 多核支持**
  - [ ] 基于 MADT 枚举处理器并启动应用处理器。
  - [ ] 引入每 CPU 状态、Local APIC 定时器和核间中断。
  - [ ] 为调度器、内存管理、设备和缓存数据结构补充 SMP 同步原语。
  - [ ] 支持跨核任务调度并添加多核 QEMU 回归测试。
  - [ ] 增加超线程感知：通过 CPUID leaf 0xB/0x1F 枚举每核逻辑处理器，调度决策时不把 SMT 兄弟线程当作完全独立的核。
- [ ] **实现交换与磁盘分页（swap）**
  - [ ] 定义交换区及页状态/所有权管理。
  - [ ] 实现受控的页面换出、换入和替换策略。
  - [ ] 处理 I/O 失败、并发缺页、脏页及无交换空间等情况。
  - [ ] 在块缓存、页缓存和可靠缺页处理建立后再接入。

## 网络

- [ ] **建立网络设备层**
  - [ ] 提供网络设备接口并接入至少一种网卡驱动（如 virtio-net、e1000 或 RTL8139）。
  - [ ] 实现带中断或轮询的收发包路径及驱动统计信息。
- [ ] **实现链路与网络协议**
  - [ ] 实现以太网和 ARP。
  - [ ] 实现 IPv4、路由和基本 ICMP。
  - [ ] 实现 ICMP 回显请求/应答，使用户程序可以实现 `ping`。
  - [ ] 实现 UDP。
  - [ ] 实现 TCP 状态机、重传、流控和连接管理。
- [ ] **增加 IPv6 支持**（实验性）
  - [ ] 实现 ICMPv6、邻居发现和单链路无状态自动配置。
  - [ ] 在用户态套接字接口之上提供 IPv6 支持。
- [ ] **设计用户态套接字接口**
  - [ ] 定义面向用户程序的系统调用/套接字 ABI。
  - [ ] 实现用户态网络工具（包括 `ping`）并用 QEMU 用户态网络验证。
- [ ] **实现熵池与内核伪随机数生成器（PRNG）**
  - [ ] 从可用源收集熵（RDTSC 抖动、RTC/中断时序、设备时序）。
  - [ ] 通过 CPUID 探测 RDRAND/RDSEED，优先使用硬件随机数，不可用时回退到熵池。
  - [ ] 维护内核熵池并驱动 CSPRNG（如基于 ChaCha20），提供随机字节和整数。
  - [ ] 通过系统调用或设备节点向用户程序暴露随机数；记录播种与熵池健康规则。

## 图形与 GUI

- [ ] **建立内核图形子系统**
  - [ ] 抽象 framebuffer 和显示设备，完善图形模式与像素格式管理。
  - [ ] 提供 VGA 文本模式与 VBE 模式共用的统一屏幕/区域抽象。
- [ ] **实现窗口管理与输入分发**
  - [ ] 实现窗口管理、合成、输入事件分发和焦点管理。
  - [ ] 定义图形输出的虚拟画布或多窗口布局模型。
- [ ] **构建用户态 GUI 层**
  - [ ] 通过系统调用或共享内存缓冲区提供用户态绘图/窗口接口。
  - [ ] 构建基础 GUI 工具包或控件集及示例应用。

## 可执行文件、安全与用户空间生态

- [ ] **完善可执行格式支持**
  - [ ] 扩展 ELF 动态段、重定位和依赖解析。
  - [ ] 支持共享库装载、符号解析、生命周期和地址空间布局。
  - [ ] 明确静态/动态程序兼容性并提供加载器测试。
- [ ] **建立安全机制**
  - [ ] 定义用户、进程和文件权限模型。
  - [ ] 实现 capabilities 或等价的细粒度授权机制。
  - [ ] 为进程和设备访问提供沙箱边界及资源限制。
  - [ ] 对系统调用、路径解析、共享内存和进程间接口进行安全测试。
- [ ] **评估 VT-x 虚拟化**
  - [ ] 通过 CPUID 检查 VMX 支持并启用 VMX 操作（CR4.VMXE、VMXON 区域）。
  - [ ] 搭建最小 VMCS 配置，在 VMX non-root 模式下运行 guest。
  - [ ] 先明确目标（如用户态沙箱 vs 完整 hypervisor）；没有具体用途前不启动。
- [ ] **定义用户态 ABI 与工具链**
  - [ ] 设计并稳定满足 libc 所需的系统调用与 ABI。
  - [ ] 建立简单的用户态编译、链接和打包工具链。
- [ ] **实现 mini-libc（`mini-libc/`，MIT 协议）**
  - [x] 基于大类/子操作 ABI 的系统调用封装（`syscall.S` 中的 `syscall0`-`syscall3`；`write`/`read`/`open`/`close`、`malloc` 系列、`exit`、`yield`、信号均由下列模块覆盖）。
  - [x] `string.h`/`stdlib.h` freestanding 实现（memcpy/memset/strlen/strcmp/malloc 封装、`aligned_alloc`/`atexit`、strtol/qsort/rand、`assert.h`、`setjmp.h`、`inttypes.h`、`stdnoreturn.h`、`signal.h`、`process.h`、`console.h`；`test/mini-libc/` 下有 QEMU 测试套件（15/15））。
  - [x] `stdio.h` 控制台/字符串覆盖：`printf`/`sprintf`/`snprintf`/`puts`/`putchar`/`scanf`/`sscanf`/`getchar`，支持标志、宽度、精度。
  - [x] `stdio.h` `FILE*` 流：`stdin`/`stdout` 控制台流，以及 `fopen`/`fclose`/`fread`/`fwrite`/`fgetc`/`fputc`/`fputs`/`fgets`/`fprintf`/`fscanf`/`feof`/`ferror`/`clearerr`/`fflush`（无缓冲）。
  - [x] `errno.h`（`errno` 全局量 + `E*` 常量）、`time.h`（基于 `SYS_TIME` 的 UTC+8 日历/时钟）、`fs.h`/`unistd.h`（基于 `SYS_FS`/`SYS_PROCESS` 的 FAT32 fd 与进程/路径封装）。
  - [x] `_start` 入口（`src/crt0.S`）与 ELF32 链接方式，使用户程序可直接链接使用。
  - [x] 用 QEMU 下的 hello-world ELF32 验证（`examples/hello.c`）。
- [ ] **移植 musl libc**
  - [ ] 移植 musl libc 并将其系统调用层适配到 MyOS。
  - [ ] 用测试验证核心 libc 功能（stdio、malloc、字符串、时间）。
- [ ] **移植用户态命令行工具**
  - [ ] 移植并验证一组基础命令行工具。
- [ ] **移植 glibc**
  - > **已弃用——目前暂不实现。** glibc 的动态链接器、NSS 和 locale 架构远比 musl 沉重；除非有具体程序依赖 glibc 专有扩展，否则不考虑。

## 测试与质量保障

- [ ] **自动化代码质量保障（静态分析与模糊测试）**
  - [ ] 将内核 C/汇编的静态分析（如 sparse、cppcheck 或更严格的编译告警）接入 `make test`/CI。
  - [ ] 用 QEMU 中的畸形磁盘镜像对 FAT32 解析（目录项、文件名、损坏扇区）做模糊测试。
  - [ ] 用用户态测试 ELF 对系统调用 ABI 输入（坏指针、长度、信号编号）做模糊测试。
  - [ ] 对 shell 行编辑器（转义、引号、历史）和 ELF 加载器用生成的输入做模糊测试。

## 64 位模式

> **已弃用——目前暂不实现。** 本节仅作存档参考；32 位 PAE 内核仍是目标架构。

- [ ] **将内核移植到 64 位长模式**
  - [ ] 用长模式切换（IA-32e，PML4/PDPT/PD/PT）替换 32 位 PAE 启动流程，保留 Multiboot 入口路径。
  - [ ] 按 x86-64 ABI 移植内核代码：扩大指针宽度，重写内联汇编与栈帧约定，更新全部 `include/` 头文件。
  - [ ] 为 64 位地址空间重做内存管理（4 级分页、规范地址、内核/用户分割），包括高半核映射和物理内存位图。
  - [ ] 将中断、系统调用、Ring 3 进程、ELF 加载、FAT32、驱动和 Shell 移植到 64 位；更新全部 QEMU 测试夹具。
  - [ ] 在 `docs/en`/`docs/zh` 中记录架构差异并保持双语同步。
