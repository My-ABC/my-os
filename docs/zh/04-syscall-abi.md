# MyOS 系统调用 ABI（现状记录）

本文档记录当前 `int 0x80` 系统调用接口的实际行为，供 Ring 3 程序和后续 ABI 稳定化工作参考。
定义见 `include/os/syscall.h`，实现在 `src/os/syscall.c`，汇编入口在 `boot/interrupt.asm` 的 `syscall` 桩。

English: [English version](../en/04-syscall-abi.md)

## 调用约定

- 触发方式：执行 `int 0x80`。IDT 门 `0x80` 为 DPL=3 的 32 位陷阱门（trap gate，`idt.c` 中 `idt_set_entry(0x80, syscall, 0x08, 0xEF)`），Ring 3 和 Ring 0（CPL ≤ 3）都可以触发。
- 汇编桩 `syscall`：`pushad` → `call syscall_handler` → 把返回值写回保存的 EAX（`[esp+28]`）→ `popad` → `iret`。
- **寄存器约定**：
  - `eax` = 系统调用号（进入时），返回值（离开时）
  - `ebx` = 参数 1，`ecx` = 参数 2，`edx` = 参数 3；最多 3 个参数，无栈传参
  - 除 `eax` 外所有通用寄存器由 `pushad`/`popad` 保证不变
- **中断状态**：陷阱门不清除 IF，handler 继承调用者的中断状态（Ring 3 用户态 EFLAGS 为 `0x202`，即 IF=1）。因此 IRQ0（PIT）和 IRQ1（键盘）可以在 handler 执行期间触发：键盘 IRQ 会把扫描码放入环形缓冲，`SYS_READ`/`SYS_GETCHAR` 的 `keyboard_wait_key` 阻塞等待因此可以被正常唤醒。IRQ0 命中的是内核态帧（`cs & 3 != 3`），`ring3_timer_switch` 直接返回、不发生上下文切换，所以系统调用本身不会被调度打断（见"已知限制"）。
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

## 已知限制（ABI 稳定化前需解决）

- **无用户指针校验**：`SYS_WRITE`/`SYS_READ`/`SYS_MEMORY` 直接解引用用户传入的指针，未检查范围、越界或内核地址。Ring 3 传入未登记的非法指针会触发页错误并终止该进程；内核上下文传入非法指针会进入 kernel page fault panic（`kfault.elf` 测试覆盖）。
- **无 ABI 版本**：调用号是裸整数，无版本字段。
- **错误语义不统一**：未定义调用号返回 `-1`，内存操作失败返回 0，成功调用各自返回 0/指针/时间戳，调用方需逐个判断。
- **阻塞调用不让出 CPU**：`SYS_READ`/`SYS_GETCHAR` 期间 IRQ0 照常触发，但 `ring3_timer_switch` 看到的是内核态帧而直接返回，其他 Ring 3 进程在系统调用返回前无法被调度。键盘等待本身可被 IRQ1 正常唤醒（trap gate 保留 IF）。
- **`SYS_READ`/`SYS_GETCHAR` 只认键盘**：不经过文件/控制台抽象。
- **无文件、目录、设备访问接口**；无进程创建/退出/等待类调用。
- `SYS_WRITE` 的前后台抑制是隐式行为，调用方无法主动选择输出通道。
