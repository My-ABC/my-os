# MyOS 系统调用 ABI（现状记录）

本文档记录当前 `int 0x80` 系统调用接口的实际行为，供 Ring 3 程序和后续 ABI 稳定化工作参考。
定义见 `include/os/syscall.h`，实现在 `src/os/syscall.c`，汇编入口在 `boot/interrupt.asm` 的 `syscall` 桩。

English: [English version](../en/04-syscall-abi.md)

## 调用约定

- 触发方式：执行 `int 0x80`。IDT 门 `0x80` 为 DPL=3 的 32 位陷阱门（trap gate，`idt.c` 中 `idt_set_entry(0x80, syscall, 0x08, 0xEF)`），Ring 3 和 Ring 0（CPL ≤ 3）都可以触发。
- 汇编桩 `syscall`：按 `struct irq_frame` 布局保存寄存器与段寄存器，调用 `syscall_handler`，再复用 IRQ0 的恢复序列返回。
- **寄存器约定**——两级编号：
  - `eax` = 系统调用**大类**（1–7，即 `SYS_*` 组）
  - `ebx` = 该大类内的**子操作号**
  - `ecx` = 参数 1，`edx` = 参数 2，`esi` = 参数 3；最多 3 个参数，无栈传参
  - `eax` 同时承载返回值
  - 除 `eax` 外所有通用寄存器由 `pushad`/`popad` 保证不变
- **中断状态**：陷阱门不清除 IF，handler 继承调用者的中断状态（Ring 3 用户态 EFLAGS 为 `0x202`，即 IF=1）。因此 IRQ0（PIT）和 IRQ1（键盘）可以在 handler 执行期间触发：键盘 IRQ 会把扫描码放入环形缓冲，`IO_READ`/`IO_GETCHAR` 的 `keyboard_wait_key` 阻塞等待因此可以被正常唤醒。IRQ0 命中的是内核态帧（`cs & 3 != 3`），`ring3_timer_switch` 不会在内核系统调用中途切换；进程可以在安全的系统调用边界通过 `PROC_YIELD` 主动让出 CPU。
- **返回值**：`eax`。未定义的大类返回 `-1`（`0xFFFFFFFF`）；已知大类内未知的子操作号返回 0。**没有 errno，错误语义不统一**。
- **Ring 0 调用**：内核可通过 `sys_call(klass, op, arg1, arg2, arg3)`（内联 `int 0x80`）或直接 `int 0x80` 调用。内核上下文中 `ring3_current_pid()` 返回 0，因此 `PROC_GETPID` 返回 0，`IO_WRITE` 因前后台检查（要求 `pid != 0`）被静默丢弃。
- C 侧封装：`sys_call` 以及 `sys_malloc`/`sys_calloc`/`sys_realloc`/`sys_free`/`sys_shutdown`/`sys_reboot`/`sys_getpid`（`include/os/syscall.h`）。

## 系统调用大类

| 大类 | 名称 | 子操作 |
|------|------|--------|
| 1 | `SYS_IO` | `IO_WRITE`(1)、`IO_GETCHAR`(2)、`IO_READ`(3)、`IO_CLEAR`(4)、`IO_CURSOR_GET`(5)、`IO_CURSOR_SET`(6) |
| 2 | `SYS_TIME` | `TIME_GET`(1)、`TIME_TO`(2)、`TIME_SLEEP`(3) |
| 3 | `SYS_MEMORY` | `MEMORY_MALLOC`(1)、`MEMORY_CALLOC`(2)、`MEMORY_REALLOC`(3)、`MEMORY_FREE`(4) |
| 4 | `SYS_POWER` | `PWR_SHUTDOWN`(1)、`PWR_REBOOT`(2) |
| 5 | `SYS_PROCESS` | `PROC_GETPID`(1)、`PROC_EXIT`(2)、`PROC_YIELD`(3)、`PROC_LIST`(4)、`PROC_SET_FG`(5)、`PROC_EXEC`(6) |
| 6 | `SYS_FS` | `FS_OPEN`(1) … `FS_READDIR`(12) |
| 7 | `SYS_SIG` | `SIG_KILL`(1)、`SIG_SIGNAL`(2)、`SIG_RETURN`(3) |

## `SYS_IO`（1）

| 子操作 | 名称 | 参数 | 返回 | 职责 |
|--------|------|------|------|------|
| 1 | `IO_WRITE` | `ecx` = `const char *`（NUL 结尾字符串指针） | 成功 0，失败 -1 | 向进程所属虚拟终端的屏幕缓冲输出字符串（上限 `IO_WRITE_MAX` = 4096 字节），并镜像到 COM3（`vga_print` + `serial3_print`，无长度参数，按 NUL 结尾）。字符串先拷入有界内核缓冲，因此非法指针或缺 NUL 返回 -1 而不会 panic。**仅当调用者 PID 等于其 vt 上的 `ring3_foreground_pid_vt()` 时才输出**；后台进程或内核上下文调用被静默丢弃。COM1 只承载内核侧输出——Ring 3 输出不出现在 COM1。 |
| 2 | `IO_GETCHAR` | 无 | 字符 | 阻塞读取单个键盘字符，循环到拿到非 0 字符为止；实际不会返回 0。 |
| 3 | `IO_READ` | `ecx` = `char *buf`，`edx` = `size` | 成功 0，失败 -1 | 从键盘读取一行到 `buf`（内部走 `gets`，带回显）。最多写入 `size-1` 个字符加 NUL。`size <= 0` 或 `buf` 不完全位于允许的用户地址范围内时返回 -1。阻塞直到收到换行。 |
| 4 | `IO_CLEAR` | 无 | 0 | 清屏（`vga_clear`）。 |
| 5 | `IO_CURSOR_GET` | 无 | `(row << 16) \| col` | 经 `vga_get_cursor` 返回当前光标位置。 |
| 6 | `IO_CURSOR_SET` | `ecx` = 行，`edx` = 列 | 0 | 经 `vga_set_cursor` 移动光标（VGA 文本与 VBE 模式均可用）。 |

## `SYS_TIME`（2）

| 子操作 | 名称 | 参数 | 返回 | 职责 |
|--------|------|------|------|------|
| 1 | `TIME_GET` | 无 | Unix 时间戳 | `rtc_read_time` + `rtc_to_unix_timestamp`，返回当前 UTC 时间的 Unix 时间戳。 |
| 2 | `TIME_TO` | `ecx` = 有符号偏移（**秒**），`edx` = `rtc_time_t *` 用户缓冲区 | 0 或 -1 | 读取 RTC 并按给定偏移执行 `rtc_convert_timezone`，把结果 `rtc_time_t`（8 字节：秒、分、时、日、月、年）拷贝到用户缓冲区。缓冲区必须通过用户指针校验。 |
| 3 | `TIME_SLEEP` | `ecx` = 时长（**微秒**） | 0；TSC 未校准时返回 -1 | 基于已校准 TSC 忙等至截止时刻；信号处置按 PIT tick（10ms）粒度轮询，已投递信号可提前中断睡眠。支持亚毫秒精度；睡眠期间占用 CPU。 |

## `SYS_MEMORY`（3）

| 子操作 | 名称 | 参数 | 返回 |
|--------|------|------|------|
| 1 | `MEMORY_MALLOC` | `ecx` = 字节数 | 用户堆指针，失败返回 0；`size == 0` 返回 0 |
| 2 | `MEMORY_CALLOC` | `ecx` = 元素数，`edx` = 元素大小 | 清零后的用户堆指针，失败返回 0；`ecx*edx` 溢出返回 0 |
| 3 | `MEMORY_REALLOC` | `ecx` = 指针，`edx` = 新字节数 | 新指针，失败返回 0；`ptr == 0` 等价于 malloc，`size == 0` 等价于 free |
| 4 | `MEMORY_FREE` | `ecx` = 指针 | 0；`NULL` 或越界指针被静默忽略 |

用户堆范围：`0x01000000`–`0x40000000`（`USER_HEAP_START`/`USER_HEAP_END`），按页分配并映射为 USER 页；块头 `struct user_block`（magic `0x55534552`），释放后进入空闲链表供复用，块所在的首页映射保留。

## `SYS_POWER`（4）

| 子操作 | 名称 | 行为 |
|--------|------|------|
| 1 | `PWR_SHUTDOWN` | ACPI 关机 |
| 2 | `PWR_REBOOT` | ACPI 重启 |

## `SYS_PROCESS`（5）

| 子操作 | 名称 | 参数 | 返回 | 职责 |
|--------|------|------|------|------|
| 1 | `PROC_GETPID` | 无 | 当前 Ring 3 进程 PID | 内核上下文调用时返回 0。 |
| 2 | `PROC_EXIT` | `ecx` = 退出码（已接受，暂不记录） | 不返回 | 终止当前 Ring 3 进程并回收其资源（见下文"进程退出"）。内核上下文调用时退化为普通返回 0。 |
| 3 | `PROC_YIELD` | 无 | 0 | 主动让出 CPU，保存当前 Ring 3 上下文并返回 Shell；进程之后由 PIT 调度恢复。 |
| 4 | `PROC_LIST` | `ecx` = 缓冲区，`edx` = 大小 | 进程数或 -1 | 枚举存活进程为 12 字节记录 `{pid, state, is_foreground}`；`state` 取 `RING3_PROCESS_*` 值。缓冲区须通过用户指针校验且容纳 `count * 12` 字节。 |
| 5 | `PROC_SET_FG` | `ecx` = pid（`0` 取消前台） | 0 或 -1 | 经 `ring3_set_foreground`/`ring3_set_background` 设置或清除前台进程。 |
| 6 | `PROC_EXEC` | `ecx` = FAT32 上的 ELF32 路径 | 0 或 -1 | 读取文件、`elf32_load` 校验装载，并**以前台**调度为 Ring 3 进程（与 Shell `run` 命令同路径）。失败返回 -1；不返回新 pid。 |

## `SYS_FS`（6）

| 子操作 | 名称 | 参数 | 返回 | 职责 |
|--------|------|------|------|------|
| 1 | `FS_OPEN` | `ecx` = 路径字符串，`edx` = 模式 | fd（0–7）或 -1 | 打开 FAT32 文件。模式：`0`=`SYSFS_O_RDONLY`（文件必须存在），`1`=`SYSFS_O_RDWR`（不存在时允许，首次写入时创建）。目录一律拒绝。 |
| 2 | `FS_CLOSE` | `ecx` = fd | 0 或 -1 | 关闭文件描述符。 |
| 3 | `FS_READ` | `ecx` = fd，`edx` = `buf`，`esi` = `size` | 实际读取字节数，EOF 返回 0，失败 -1 | 从当前偏移读取，偏移随读取前进。 |
| 4 | `FS_WRITE` | `ecx` = fd，`edx` = `buf`，`esi` = `size` | 实际写入字节数或 -1 | 仅 `O_RDWR` 描述符可写；按当前偏移写入并前进，文件不足时补零扩展。 |
| 5 | `FS_UNLINK` | `ecx` = 路径字符串 | 0 或 -1 | 删除 FAT32 文件（`fat32_delete_file`）。 |
| 6 | `FS_MKDIR` | `ecx` = 路径字符串 | 0 或 -1 | 创建 FAT32 目录（`fat32_make_directory`）。 |
| 7 | `FS_RMDIR` | `ecx` = 路径字符串 | 0 或 -1 | 删除空 FAT32 目录（`fat32_remove_directory`，不递归）。 |
| 8 | `FS_STAT` | `ecx` = 路径字符串 | 文件大小；目录返回 -2；失败 -1 | 不跟随 fd，直接按路径查询。 |
| 9 | `FS_GETCWD` | `ecx` = `buf`，`edx` = `size` | 路径长度（不含 NUL）或 -1 | 把当前工作目录（含 NUL）复制到用户缓冲区；`size` 必须 ≥ 长度+1。 |
| 10 | `FS_CHDIR` | `ecx` = 路径字符串 | 0 或 -1 | 切换工作目录；目标必须是已存在的目录。 |
| 11 | `FS_RENAME` | `ecx` = 旧路径，`edx` = 新路径 | 0 或 -1 | 移动/重命名文件或目录（`fat32_move`）；两路径均相对当前目录解析。 |
| 12 | `FS_READDIR` | `ecx` = 路径，`edx` = `buf`，`esi` = `size` | 写入字节数或 -1 | 将目录列入用户缓冲区，每行 `d name` / `- name`（`d` = 目录）。先暂存 4 KiB 内核缓冲再拷贝；`size` 截断。 |

## `SYS_SIG`（7）

| 子操作 | 名称 | 参数 | 返回 | 职责 |
|--------|------|------|------|------|
| 1 | `SIG_KILL` | `ecx` = pid，`edx` = 信号编号 | 0 或 -1 | 向进程发送信号（置为 pending）。 |
| 2 | `SIG_SIGNAL` | `ecx` = 信号编号，`edx` = 处理器（0=默认、1=忽略、否则为用户地址） | 0 或 -1 | 设置当前进程对某信号的处置；`SIGKILL` 不可捕获。 |
| 3 | `SIG_RETURN` | 无 | 恢复的 `eax` | 恢复投递信号 handler 时保存的上下文。仅由内核放置的 restorer 桩在 handler 返回后调用；无活动 handler 时返回 -1。 |

## 文件系统调用

- **路径解析**：所有路径参数都是用户态 NUL 结尾字符串，先按当前工作目录解析并规范化（`fat32_resolve_path`，与 Shell 共用同一个 `g_cwd`，支持相对路径、`.`、`..`），再调用 FAT32。路径最长 127 字符。
- **文件描述符**：内核维护一张全局 fd 表（`SYSFS_MAX_FDS` = 8 项），fd 是表项下标 0–7。fd 表是全局的、不是每进程独立的；进程退出时**不会**自动关闭其打开的 fd（后续进程可能继承同一文件偏移状态，属已知限制）。
- **读写语义**：FAT32 层只有整文件读写接口，因此 `FS_READ` 每次调用都重新读取整个文件（≤ `FAT32_MAX_FILE_SIZE` = 1 MiB）到内核缓冲再拷贝偏移处数据；`FS_WRITE` 采用 read-modify-write：读出现有内容、在偏移处覆盖、按 `max(原大小, 偏移+长度)` 整文件写回。大文件/频繁小写性能较差，属已知限制。
- **用户指针校验**：`IO_WRITE`、`IO_READ`、FS 系列调用和 `TIME_TO` 都会校验指针——`IO_WRITE` 经 `sys_copy_user_string` 拷贝（4096 字节上限），`IO_READ`/FS/`TIME_TO` 用 `sys_user_buffer_ok` 检查；非法指针返回 -1 而不是 panic。校验要求页面在当前 `user_mm` 中*已映射*（`paging_mm_get_phys`），而不仅是落在已登记范围内。`IO_GETCHAR` 无指针参数，`MEMORY_*` 仍直接解引用调用方指针（见已知限制）。
- **工作目录**：`g_cwd` 存于 `src/fs/fat32.c`，Shell 的 `cd`/提示符与用户态 `FS_GETCWD`/`FS_CHDIR` 共享同一目录。

## 进程退出（`PROC_EXIT`）

`PROC_EXIT` 调用 `ring3_terminate_current()`：释放该进程的用户栈与 ELF 映像页、标记 DEAD、清除前台 PID（若是），并把保存的内核 `irq_frame` 写入全局 `syscall_exit_frame`。`syscall` 桩检测到该值非零时不再 `popad`/`iret` 回用户态，而是把 `esp` 切到该内核帧并复用 IRQ0 的出栈序列直接恢复 Shell 上下文——与 ISR14 杀进程的路径相同。`syscall_exit_frame` 在每次 `syscall_handler` 入口清零。

## 信号

信号机制详见 [13-signals.md](13-signals.md)：信号编号、每进程
pending/blocked/handler 状态、Ctrl+C `SIGINT` 投递、经
`SIG_RETURN` 的用户态 handler 调用、`SIG_KILL`/`SIG_SIGNAL`，
以及当前限制。

## 已知限制（ABI 稳定化前需解决）

- **部分用户指针校验**：`IO_WRITE`/`IO_READ` 现已拷贝/校验调用方指针并在非法输入时返回 -1，但 `MEMORY_*` 仍直接解引用用户指针；非法指针会触发页错误终止进程，内核上下文则进入 kernel page fault panic（`kfault.elf` 测试覆盖）。
- **无 ABI 版本**：大类/子操作号是裸整数，无版本字段。
- **错误语义不统一**：未定义大类返回 `-1`，未知子操作与内存操作失败返回 0，成功调用各自返回 0/指针/时间戳，调用方需逐个判断。
- **阻塞调用不让出 CPU**：`IO_READ`/`IO_GETCHAR` 期间 IRQ0 照常触发，但 `ring3_timer_switch` 看到的是内核态帧而直接返回，其他 Ring 3 进程在调用返回前无法被调度。键盘等待本身可被 IRQ1 正常唤醒（trap gate 保留 IF）；需要合作式让出时可调用 `PROC_YIELD`。
- **`IO_READ`/`IO_GETCHAR` 只认键盘**：不经过文件/控制台抽象，且无前台检查——同一 vt 上的后台进程可以消费本应属于前台进程的按键。方向键以控制字节 `KEYBOARD_KEY_UP`/`DOWN`/`LEFT`/`RIGHT`（0x01–0x04）形式下发：`IO_GETCHAR` 将其作为普通字符返回，`IO_READ` 原样写入缓冲区并回显——需要行编辑的调用方应自行过滤或改用 `IO_GETCHAR`。
- **无设备访问接口**（串口、ATA 直访等）；无进程创建/等待类调用（`PROC_EXIT` 已有，`kill` 仍只能由 Shell 发起）。
- fd 表全局共享且进程退出不回收 fd；`FS_READ`/`FS_WRITE` 为整文件读改写，I/O 复杂度高。
- `IO_WRITE` 的前后台抑制是隐式行为，调用方无法主动选择输出通道。
