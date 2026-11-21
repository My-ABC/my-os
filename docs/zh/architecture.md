# MyOS 架构

[English](../en/architecture.md) | 中文

MyOS 是一个用 C 和 NASM 汇编编写的 freestanding 32 位 x86 教学型单体内
核。它通过 GRUB/Multiboot 启动，在 64 位 PAE 分页模式下运行，内核采用高
半区映射（higher-half），提供 Ring 3 用户态、精简的系统调用接口、交互式
Shell，以及运行在 ATA 磁盘上的 FAT32 文件系统。

## 文档索引

编号文档按子系统分类详述：

| 文档 | 主题 |
|---|---|
| [01-boot.md](01-boot.md) | Multiboot 头、早期 PAE 页表、`kmain` 初始化序列 |
| [02-memory.md](02-memory.md) | PAE 分页、PMM、内核堆、用户地址空间、ISR14 按需分页 |
| [03-interrupts.md](03-interrupts.md) | IDT/PIC/PIT、`irq_frame` 契约、异常与 IRQ 分发 |
| [04-syscall-abi.md](04-syscall-abi.md) | `int 0x80` 门、调用约定、调用号表、ABI 限制 |
| [05-processes.md](05-processes.md) | Ring 3 调度器、ELF 加载、前后台、进程终止 |
| [06-shell.md](06-shell.md) | 交互式 Shell 主循环、命令分发、路径解析 |
| [07-shell-commands.md](07-shell-commands.md) | 全部 Shell 命令、参数与语义 |
| [08-filesystem.md](08-filesystem.md) | ATA PIO、FAT32 模块划分、文件名编码、目录语义 |
| [09-drivers.md](09-drivers.md) | 键盘、RTC、串口、VGA/VBE、ACPI、PIC/PIT 驱动说明 |
| [10-build-and-test.md](10-build-and-test.md) | Makefile 目标、链接布局、host 测试、QEMU 集成套件 |
| [11-driver-gaps.md](11-driver-gaps.md) | 驱动能力盘点、按总线列出的缺失驱动、优先级、QEMU 测试设备 |

英文版见 [../en/](../en/)。

## 仓库结构

```
boot/             Multiboot 头、启动代码、GDT/分页汇编、中断桩
src/kernel.c      内核入口（kmain）与顶层初始化流程
src/arch/x86/     GDT/IDT/TSS、trap 处理、端口 I/O
src/mm/           PMM、PAE 分页、内核堆
src/os/           panic、ELF 加载器、Ring 3 调度器、系统调用层
src/shell/        交互式 shell
src/drivers/      ATA、键盘、PIC/PIT、RTC、串口、VGA/VBE、ACPI
src/fs/           FAT32（core/name/dir 拆分 + 私有 fat32_int.h）
src/lib/          freestanding 字符串/stdio
include/          与 src/ 子系统对应的公开头文件
docs/en|zh/       本文档所在目录
test/             host 契约测试、C 测试套件、QEMU 集成测试
```

## 顶层调用关系

- `boot/boot.asm:start` → 早期 PAE 页表 → `kmain(multiboot_info)`。
- `kmain` 依次初始化 GDT/TSS、串口、PMM、分页、VBE/VGA、ATA/FAT32、
  堆自检、PIC/PIT/键盘、RTC，然后进入 `start_shell()`。
- Shell 分发 `cmd_table`；磁盘命令走 FAT32 公开 API
  （`include/fs/fat32.h`）；进程命令走 Ring 3 API
  （`include/os/ring3.h`）；`run`/`elf` 经 `elf_run`。
- 硬件事件路径：`boot/interrupt.asm` 中断桩 → `src/arch/x86/` 与
  `src/drivers/` 的 C 处理器；`int 0x80` → `syscall_handler`。
- 页错误走 `isr14_handler`，可按需映射用户页或终止出错的 Ring 3 进程。
