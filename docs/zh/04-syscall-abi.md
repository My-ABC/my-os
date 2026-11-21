# MyOS 系统调用 ABI（现状记录）

本文档记录当前 `int 0x80` 系统调用接口的实际行为，供 Ring 3 程序和后续 ABI 稳定化工作参考。
定义见 `include/os/syscall.h`，实现在 `src/os/syscall.c`，汇编入口在 `boot/interrupt.asm` 的 `syscall` 桩。

English: [English version](../en/04-syscall-abi.md)

## 调用约定

- 触发方式：执行 `int 0x80`。IDT 门 `0x80` 为 DPL=3 的 32 位陷阱门（trap gate，`idt.c` 中 `idt_set_entry(0x80, syscall, 0x08, 0xEF)`），Ring 3 和 Ring 0（CPL ≤ 3）都可以触发。
- 汇编桩 `syscall`：按 `struct irq_frame` 布局保存寄存器与段寄存器，调用 `syscall_handler`，再复用 IRQ0 的恢复序列返回。
- **寄存器约定**：
  - `eax` = 系统调用号（进入时），返回值（离开时）
  - `ebx` = 参数 1，`ecx` = 参数 2，`edx` = 参数 3；最多 3 个参数，无栈传参
  - 除 `eax` 外所有通用寄存器由 `pushad`/`popad` 保证不变
- **中断状态**：陷阱门不清除 IF，handler 继承调用者的中断状态（Ring 3 用户态 EFLAGS 为 `0x202`，即 IF=1）。因此 IRQ0（PIT）和 IRQ1（键盘）可以在 handler 执行期间触发：键盘 IRQ 会把扫描码放入环形缓冲，`SYS_READ`/`SYS_GETCHAR` 的 `keyboard_wait_key` 阻塞等待因此可以被正常唤醒。IRQ0 命中的是内核态帧（`cs & 3 != 3`），`ring3_timer_switch` 不会在内核系统调用中途切换；进程可以在安全的系统调用边界通过 `SYS_YIELD` 主动让出 CPU。
- **返回值**：`eax`。未定义的调用号返回 `-1`（`0xFFFFFFFF`）。**没有 errno，错误语义不统一**。
- **Ring 0 调用**：内核可通过 `sys_call(call_number, arg1, arg2, arg3)`（内联 `int 0x80`）或直接 `int 0x80` 调用。内核上下文中 `ring3_current_pid()` 返回 0，因此 `SYS_GETPID` 返回 0，`SYS_WRITE` 因前后台检查（要求 `pid != 0`）被静默丢弃。
- C 侧封装：`sys_call` 以及 `sys_malloc`/`sys_calloc`/`sys_realloc`/`sys_free`/`sys_shutdown`/`sys_reboot`/`sys_getpid`（`include/os/syscall.h`）。

## 系统调用号

| 号 | 名称 | 参数 | 返回 | 职责 |
|----|------|------|------|------|
| 1 | `SYS_WRITE` | `ebx` = `const char *`（NUL 结尾字符串指针） | 恒 0 | 向 VGA 和串口输出字符串（`vga_print` + `serial_print`，无长度参数，按 NUL 结尾）。**仅当调用者 PID 等于 `ring3_foreground_pid()` 时才输出**；后台进程或内核上下文调用被静默丢弃。 |
| 2 | `SYS_READ` | `ebx` = `char *buf`，`ecx` = `size` | 恒 0 | 从键盘读取一行到 `buf`（内部走 `gets`，带回显）。最多写入 `size-1` 个字符加 NUL；`size <= 1` 时只写空串。阻塞直到收到换行。 |
| 3 | `SYS_GETCHAR` | 无 | 字符 | 阻塞读取单个键盘字符（`getchar` → `keyboard_wait_key`），循环到拿到非 0 字符为止；实际不会返回 0。 |
| 4 | `SYS_CLEAR` | 无 | 0 | 清屏（`vga_clear`）。 |
| 5 | `SYS_REBOOT` | 无 | 不返回 | ACPI 重启。 |
| 6 | `SYS_TIME` | 无 | Unix 时间戳 | `rtc_read_time` + `rtc_to_unix_timestamp`，返回当前时间。 |
| 7 | `SYS_MEMORY` | `ebx` = 操作码，见下表 | 见下表 | 用户堆内存操作。 |
| 8 | `SYS_POWER` | `ebx` = 操作码，见下表 | 0 | 电源操作。 |
| 9 | `SYS_GETPID` | 无 | 当前 Ring 3 进程 PID | 内核上下文调用时返回 0。 |
| 10 | `SYS_EXIT` | 无 | 不返回 | 终止当前 Ring 3 进程并回收其资源（见下文"进程退出"）。内核上下文调用时退化为普通返回 0。 |
| 11 | `SYS_FOPEN` | `ebx` = 路径字符串，`ecx` = 模式 | fd（0–7）或 -1 | 打开 FAT32 文件。模式：`0`=`SYSFS_O_RDONLY`（文件必须存在），`1`=`SYSFS_O_RDWR`（不存在时允许，首次写入时创建）。目录一律拒绝。 |
| 12 | `SYS_FCLOSE` | `ebx` = fd | 0 或 -1 | 关闭文件描述符。 |
| 13 | `SYS_FREAD` | `ebx` = fd，`ecx` = `buf`，`edx` = `size` | 实际读取字节数，EOF 返回 0，失败 -1 | 从当前偏移读取，偏移随读取前进。 |
| 14 | `SYS_FWRITE` | `ebx` = fd，`ecx` = `buf`，`edx` = `size` | 实际写入字节数或 -1 | 仅 `O_RDWR` 描述符可写；按当前偏移写入并前进，文件不足时补零扩展。 |
| 15 | `SYS_FUNLINK` | `ebx` = 路径字符串 | 0 或 -1 | 删除 FAT32 文件（`fat32_delete_file`）。 |
| 16 | `SYS_FMKDIR` | `ebx` = 路径字符串 | 0 或 -1 | 创建 FAT32 目录（`fat32_make_directory`）。 |
| 17 | `SYS_FRMDIR` | `ebx` = 路径字符串 | 0 或 -1 | 删除空 FAT32 目录（`fat32_remove_directory`，不递归）。 |
| 18 | `SYS_FSTAT` | `ebx` = 路径字符串 | 文件大小；目录返回 -2；失败 -1 | 不跟随 fd，直接按路径查询。 |
| 19 | `SYS_GETCWD` | `ebx` = `buf`，`ecx` = `size` | 路径长度（不含 NUL）或 -1 | 把当前工作目录（含 NUL）复制到用户缓冲区；`size` 必须 ≥ 长度+1。 |
| 20 | `SYS_CHDIR` | `ebx` = 路径字符串 | 0 或 -1 | 切换工作目录；目标必须是已存在的目录。 |
| 21 | `SYS_YIELD` | 无 | 0 | 主动让出 CPU，保存当前 Ring 3 上下文并返回 Shell；进程之后由 PIT 调度恢复。 |
| 22 | `SYS_KILL` | `ebx`=pid、`ecx`=信号编号 | 0 或 -1 | 向进程发送信号（置为 pending）。 |
| 23 | `SYS_SIGNAL` | `ebx`=信号编号、`ecx`=处理器（0=默认、1=忽略、否则为用户地址） | 0 或 -1 | 设置当前进程对某信号的处置；`SIGKILL` 不可捕获。 |

## `SYS_MEMORY`（7）子操作码 — `ebx`

| 操作码 | 名称 | 参数 | 返回 |
|--------|------|------|------|
| 1 | `MEMORY_MALLOC` | `ecx` = 字节数 | 用户堆指针，失败返回 0；`size == 0` 返回 0 |
| 2 | `MEMORY_CALLOC` | `ecx` = 元素数，`edx` = 元素大小 | 清零后的用户堆指针，失败返回 0；`ecx*edx` 溢出返回 0 |
| 3 | `MEMORY_REALLOC` | `ecx` = 指针，`edx` = 新字节数 | 新指针，失败返回 0；`ptr == 0` 等价于 malloc，`size == 0` 等价于 free |
| 4 | `MEMORY_FREE` | `ecx` = 指针 | 0；`NULL` 或越界指针被静默忽略 |

未知的 `SYS_MEMORY`/`SYS_POWER` 子操作码返回 0。

用户堆范围：`0x01000000`–`0x40000000`（`USER_HEAP_START`/`USER_HEAP_END`），按页分配并映射为 USER 页；块头 `struct user_block`（magic `0x55534552`），释放后进入空闲链表供复用，块所在的首页映射保留。

## `SYS_POWER`（8）子操作码 — `ebx`

| 操作码 | 名称 | 行为 |
|--------|------|------|
| 1 | `POWER_SHUTDOWN` | ACPI 关机 |
| 2 | `POWER_REBOOT` | ACPI 重启 |

## 文件系统调用（10–20）

- **路径解析**：所有路径参数都是用户态 NUL 结尾字符串，先按当前工作目录解析并规范化（`fat32_resolve_path`，与 Shell 共用同一个 `g_cwd`，支持相对路径、`.`、`..`），再调用 FAT32。路径最长 127 字符。
- **文件描述符**：内核维护一张全局 fd 表（`SYSFS_MAX_FDS` = 8 项），fd 是表项下标 0–7。fd 表是全局的、不是每进程独立的；进程退出时**不会**自动关闭其打开的 fd（后续进程可能继承同一文件偏移状态，属已知限制）。
- **读写语义**：FAT32 层只有整文件读写接口，因此 `SYS_FREAD` 每次调用都重新读取整个文件（≤ `FAT32_MAX_FILE_SIZE` = 1 MiB）到内核缓冲再拷贝偏移处数据；`SYS_FWRITE` 采用 read-modify-write：读出现有内容、在偏移处覆盖、按 `max(原大小, 偏移+长度)` 整文件写回。大文件/频繁小写性能较差，属已知限制。
- **用户指针校验**：10–20 号调用会用 `sys_user_buffer_ok`/`sys_copy_user_string` 校验指针落在已登记用户范围（ELF 镜像、用户栈、用户堆）内，非法指针返回 -1 而不是 panic；1–9 号旧调用仍无校验。
- **工作目录**：`g_cwd` 存于 `src/fs/fat32.c`，Shell 的 `cd`/提示符与用户态 `SYS_CHDIR`/`SYS_GETCWD` 共享同一目录。

## 进程退出（`SYS_EXIT`）

`SYS_EXIT` 调用 `ring3_terminate_current()`：释放该进程的用户栈与 ELF 映像页、标记 DEAD、清除前台 PID（若是），并把保存的内核 `irq_frame` 写入全局 `syscall_exit_frame`。`syscall` 桩检测到该值非零时不再 `popad`/`iret` 回用户态，而是把 `esp` 切到该内核帧并复用 IRQ0 的出栈序列直接恢复 Shell 上下文——与 ISR14 杀进程的路径相同。`syscall_exit_frame` 在每次 `syscall_handler` 入口清零。

## 信号

信号机制详见 [12-signals.md](12-signals.md)：信号编号、每进程
pending/blocked/handler 状态、Ctrl+C `SIGINT` 投递、
`SYS_KILL`/`SYS_SIGNAL`，以及当前限制（自定义 handler 已存储但尚未
调用）。

## 已知限制（ABI 稳定化前需解决）

- **无用户指针校验**：`SYS_WRITE`/`SYS_READ`/`SYS_MEMORY` 直接解引用用户传入的指针，未检查范围、越界或内核地址。Ring 3 传入未登记的非法指针会触发页错误并终止该进程；内核上下文传入非法指针会进入 kernel page fault panic（`kfault.elf` 测试覆盖）。
- **无 ABI 版本**：调用号是裸整数，无版本字段。
- **错误语义不统一**：未定义调用号返回 `-1`，内存操作失败返回 0，成功调用各自返回 0/指针/时间戳，调用方需逐个判断。
- **阻塞调用不让出 CPU**：`SYS_READ`/`SYS_GETCHAR` 期间 IRQ0 照常触发，但 `ring3_timer_switch` 看到的是内核态帧而直接返回，其他 Ring 3 进程在调用返回前无法被调度。键盘等待本身可被 IRQ1 正常唤醒（trap gate 保留 IF）；需要合作式让出时可调用 `SYS_YIELD`。
- **`SYS_READ`/`SYS_GETCHAR` 只认键盘**：不经过文件/控制台抽象。方向键以控制字节 `KEYBOARD_KEY_UP`/`DOWN`/`LEFT`/`RIGHT`（0x01–0x04）形式下发：`SYS_GETCHAR` 将其作为普通字符返回，`SYS_READ` 原样写入缓冲区。
- **无设备访问接口**（串口、ATA 直访等）；无进程创建/等待类调用（`SYS_EXIT` 已有，`kill` 仍只能由 Shell 发起）。
- fd 表全局共享且进程退出不回收 fd；`SYS_FREAD`/`SYS_FWRITE` 为整文件读改写，I/O 复杂度高。
- `SYS_WRITE` 的前后台抑制是隐式行为，调用方无法主动选择输出通道。
