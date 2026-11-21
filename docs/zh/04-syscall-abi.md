# MyOS 系统调用 ABI（现状记录）

本文档记录当前 `int 0x80` 系统调用接口的实际行为，供 Ring 3 程序和后续 ABI 稳定化工作参考。
定义见 `include/os/syscall.h`，实现在 `src/os/syscall.c`，汇编入口在 `boot/interrupt.asm` 的 `syscall` 桩。

English: [English version](../en/04-syscall-abi.md)

## 调用约定

- 触发方式：用户态执行 `int 0x80`。IDT 门 `0x80` 为 DPL=3 中断门（`idt.c` 中 `idt_set_entry(0x80, syscall, 0x08, 0xEE)`）。
- 汇编桩 `syscall`：`pushad` → `call syscall_handler` → 把返回值写回保存的 EAX → `popad` → `iret`。
- `syscall_handler()` 用内联汇编读取寄存器：
  - `eax` = 系统调用号
  - `ebx` = 参数 1
  - `ecx` = 参数 2
  - `edx` = 参数 3
- 返回值：`eax`。未定义的调用号返回 `-1`（`0xFFFFFFFF`）。**没有 errno，错误语义不统一**。
- C 侧封装：`sys_call(call_number, arg1, arg2, arg3)` 以及 `sys_malloc`/`sys_calloc`/`sys_realloc`/`sys_free`/`sys_shutdown`/`sys_reboot`/`sys_getpid`。

## 系统调用号

| 号 | 名称 | 参数 | 返回 | 职责 |
|----|------|------|------|------|
| 1 | `SYS_WRITE` | `ebx` = `const char *`（NUL 结尾字符串指针） | 0 | 向 VGA 和串口输出字符串。**仅当调用者 PID 等于 `ring3_foreground_pid()` 时才输出**；后台进程调用此调用被静默丢弃。 |
| 2 | `SYS_READ` | `ebx` = `char *buf`，`ecx` = `size` | 0 | 从键盘读取一行到 `buf`（内部走 `gets`，带回显，最多 `size-1` 字符）。 |
| 3 | `SYS_GETCHAR` | 无 | 字符或 0 | 从键盘阻塞读取单个字符（`keyboard_wait_key`）。 |
| 4 | `SYS_CLEAR` | 无 | 0 | 清屏（`vga_clear`）。 |
| 5 | `SYS_REBOOT` | 无 | 不返回 | ACPI 重启。 |
| 6 | `SYS_TIME` | 无 | Unix 时间戳（`rtc_to_unix_timestamp`） | 获取当前时间。 |
| 7 | `SYS_MEMORY` | `ebx` = 操作码，见下表 | 见下表 | 用户堆内存操作。 |
| 8 | `SYS_POWER` | `ebx` = 操作码，见下表 | 0 | 电源操作。 |
| 9 | `SYS_GETPID` | 无 | 当前 Ring 3 进程 PID | 内核态调用时返回 0。 |

## `SYS_MEMORY`（7）子操作码 — `ebx`

| 操作码 | 名称 | 参数 | 返回 |
|--------|------|------|------|
| 1 | `MEMORY_MALLOC` | `ecx` = 字节数 | 用户堆指针，失败返回 0 |
| 2 | `MEMORY_CALLOC` | `ecx` = 元素数，`edx` = 元素大小 | 清零后的用户堆指针，失败返回 0；`ecx*edx` 溢出返回 0 |
| 3 | `MEMORY_REALLOC` | `ecx` = 指针，`edx` = 新字节数 | 新指针，失败返回 0 |
| 4 | `MEMORY_FREE` | `ecx` = 指针 | 0 |

用户堆范围：`0x01000000`–`0x40000000`，按页分配并映射为 USER 页；块头 `struct user_block`（magic `0x55534552`），释放后进入空闲链表供复用。

## `SYS_POWER`（8）子操作码 — `ebx`

| 操作码 | 名称 | 行为 |
|--------|------|------|
| 1 | `POWER_SHUTDOWN` | ACPI 关机 |
| 2 | `POWER_REBOOT` | ACPI 重启 |

## 已知限制（ABI 稳定化前需解决）

- **无用户指针校验**：`SYS_WRITE`/`SYS_READ`/`SYS_MEMORY` 直接解引用用户传入的指针，未检查范围、越界或内核地址。
- **无 ABI 版本**：调用号是裸整数，无版本字段。
- **错误语义不统一**：有的返回 0、有的返回 -1、有的返回指针，调用方需逐个判断。
- **`SYS_READ`/`SYS_GETCHAR` 只认键盘**：不经过文件/控制台抽象。
- **无文件、目录、时间格式化以外的设备访问接口**；无进程创建/退出/等待类调用。
- `SYS_WRITE` 的前后台抑制是隐式行为，调用方无法主动选择输出通道。
