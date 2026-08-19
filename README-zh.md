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
- **键盘** — IRQ1，支持扫描码集 1 和 2（含小键盘与 `0xE0` 扩展码），32 字节环形缓冲
- **蓝屏** — INT3 在蓝底屏幕上 dump 全部寄存器，然后重启
- **ACPI 重启** — 通过 FADT reset register 复位，依次回退到 `0xCF9`、键盘控制器、三重错误

## 构建

依赖：`nasm`、i686 工具链、`qemu-system-x86`（运行与测试用）。

```bash
make build          # -> myos.bin
make run            # 带图形窗口的 QEMU
make run-nographic  # 串口输出到终端的 QEMU
```

Makefile 默认使用 `i686-elf-gcc` / `i686-elf-ld`。没有交叉工具链时可以用宿主编译器的 32 位模式：

```bash
make CC=gcc LD=ld build   # 需要 gcc-multilib
```

## 测试

所有测试都是无图形界面的，通过校验 COM1 输出来判断结果。

```bash
make test               # 100Hz 时钟: COM1 每秒输出一个递增数字
make test-panic         # INT3 蓝屏: 寄存器 dump + 自动重启
make test-keyboard      # 键盘（扫描码集1）: 'b' 蓝屏, 其他键停机
make test-scancode-set2 # 键盘（扫描码集2）: 切换到扫描码集2后测试
```

键盘和蓝屏测试通过 QEMU monitor（`-monitor pipe:`）注入按键、抓取截图，不需要额外工具。给 make 传参用 `MAKE_ARGS`，例如 `MAKE_ARGS="CC=gcc LD=ld" make test-keyboard`。

## 使用

内核启动完成后会等待一个按键：

- `b` — 触发蓝屏：dump 寄存器，倒计时 5 秒后重启
- 其他键 — 打印 `Halted` 并停机

### 扫描码集设置

内核支持通过编译选项设置键盘扫描码集：

```bash
make SCANCODE_SET=1 build  # 使用扫描码集1（默认）
make SCANCODE_SET=2 build  # 使用扫描码集2
make run
```

扫描码集2是现代PS/2键盘的标准格式，使用 `0xF0` 前缀标识断码，而扫描码集1使用最高位标识断码。

用 `make PANIC_DEMO=1` 构建时，内核会在初始化后直接触发 `int $0x03`，`make test-panic` 就是这么做的。

## 目录结构

```
boot/     Multiboot 头、内核入口、中断桩 (NASM)
src/      内核、VGA、串口、IDT、PIC、PIT、键盘、蓝屏、ACPI
include/  硬件抽象头文件与 freestanding 类型定义
scripts/  无图形界面的 QEMU 测试
linker.ld 1MB 加载地址与段布局
```

## 说明

QEMU 默认的 `pc` 机型（SeaBIOS）只提供 ACPI 1.0 的 FADT，里面没有 reset register，所以那里的重启实际走 `0xCF9` 兜底；用 `-machine q35` 时固件提供 ACPI 2.0+ FADT，才会真正使用 reset register。

## 许可证

[MIT](LICENSE)
