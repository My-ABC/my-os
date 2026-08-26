[English](README.md) | 中文

# MyOS

一个 32 位 x86 内核：通过 Multiboot 启动，输出到 VGA 文本模式和 COM1 串口，处理 CPU 异常与硬件中断。

## 功能

- **Multiboot 启动** — 由 GRUB/QEMU 加载到 1MB，自行建立栈后进入 `kmain`
- **VGA 文本模式** — `0xB8000` 上的 80x25 驱动，支持颜色、滚屏、硬件光标、十六进制/十进制打印
- **串口控制台** — COM1 (`0x3F8`) 日志，所有无图形测试都靠它
- **IDT** — 256 项，INT3 与 IRQ0/IRQ1 有真实处理函数
- **PIC** — 8259 重映射到向量 `0x20`/`0x28`，支持单个 IRQ 屏蔽与 EOI
- **PIT 时钟** — 通道 0 跑 100Hz（IRQ0），每秒在 COM1 打印一个递增数字
- **GDT** — 全局描述符表，支持代码段、数据段、用户态段、TSS
- **TSS** — 实现了esp0, ss0
- **键盘** — IRQ1，支持扫描码集 1 和 2（含小键盘与 `0xE0` 扩展码），32 字节环形缓冲
- **RTC** — CMOS 实时时钟，读取年/月/日和时/分/秒，支持4位数年份避免千年虫问题，支持时区转换和Unix时间戳
- **蓝屏** — INT3 在蓝底屏幕上 dump 全部寄存器，然后重启
- **ACPI 重启** — 通过 FADT reset register 复位，依次回退到 `0xCF9`、键盘控制器、三重错误
- **ACPI 关机** — 通过 ACPI S5 状态关机，备用方案使用常见 ACPI I/O 端口
- **Ring 3** — 实现了Ring3
- **syscall** — 实现系统调用为了Ring3
- **shell(内核)** — 制作了一个内核态的shell
- **内存分页** — 实现了完整内存分页
- **PMM** — 实现了PMM
- **堆分配器(内核)** — 实现了kmalloc, kfree, kcalloc, krealloc

## 构建

依赖：`nasm`、i686 工具链、`qemu-system-x86`（运行与测试用）、`grub-pc-bin`、`xorriso`、`grub-common`。

```bash
make build          # -> myos.bin
make run            # 带图形窗口的 QEMU
make run-nographic  # 串口输出到终端的 QEMU
make run-q35        # 使用 q35 机型运行（更好的 ACPI 支持）
make run-q35-nographic  # 使用 q35 机型运行，串口输出到终端
make run-iso        # 带图形窗口的 QEMU 用 ISO
make iso            # 生成iso文件
```

Makefile 默认使用 `i686-elf-gcc` / `i686-elf-ld`。没有交叉工具链时可以用宿主编译器的 32 位模式：

```bash
make CC=gcc LD=ld build   # 需要 gcc-multilib
```

### 扫描码集设置

内核支持通过编译选项设置键盘扫描码集：

```bash
make SCANCODE_SET=1 build  # 使用扫描码集1（默认）
make SCANCODE_SET=2 build  # 使用扫描码集2
make run
```

扫描码集2是现代PS/2键盘的标准格式，使用 `0xF0` 前缀标识断码，而扫描码集1使用最高位标识断码。

用 `make PANIC_DEMO=1` 构建时，内核会在初始化后直接触发 `int $0x03`。

## shell使用
```
命令列表：
time     获取时间
panic    触发INT 3
echo     打印后面的文字
reboot   ACPI重启
shutdown ACPI关机
secho    串口打印文字
tick     获取PIT中的tick
help     显示帮助
clear    清屏
hello    打印hello消息
```

## 目录结构

```
boot/     Multiboot 头、内核入口、中断桩 (NASM)
src/      内核、VGA、串口、IDT、PIC、PIT、键盘、RTC、蓝屏、ACPI、shell
include/  硬件抽象头文件与 freestanding 类型定义
scripts/  CI/CD文件
linker.ld 1MB 加载地址与段布局
```

## 说明

QEMU 默认的 `pc` 机型（SeaBIOS）只提供 ACPI 1.0 的 FADT，里面没有 reset register，所以那里的重启实际走 `0xCF9` 兜底；用 `-machine q35` 时固件提供 ACPI 2.0+ FADT，才会真正使用 reset register。

## 许可证

[GPLv3](LICENSE)
