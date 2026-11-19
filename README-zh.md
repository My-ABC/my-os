[English](README.md) | 中文

# MyOS

一个 32 位 x86 内核：通过 Multiboot/GRUB 启动，使用 GRUB 提供的 framebuffer 控制台和 COM1 串口，处理 CPU 异常与硬件中断。

## 功能

- **Multiboot/GRUB 视频启动** — GRUB 请求 640x480x32 framebuffer，并将地址传给内核
- **Framebuffer 控制台** — 16x16 放大 ASCII 字体，支持大小写、标点、滚屏和闪烁下划线光标
- **VGA 文本回退** — 没有 GRUB framebuffer 时仍支持直接使用 `0xB8000` 文本输出
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
- **Ring 3 进程** — 使用 PIT IRQ0 抢占式软件上下文切换，最多运行两个用户进程，每个进程有独立的内核栈和用户栈
- **进程管理** — Shell 支持 `ps`、`fg <pid>`、`bg <pid>` 和 `kill <pid>`；Ring 3 进程默认在后台运行
- **系统调用** — Ring 3 可输出文本，并通过 `SYS_GETPID` 查询当前进程 PID
- **shell(内核)** — 制作了一个内核态的shell
- **内存分页** — 实现了完整内存分页
- **PMM** — 实现了PMM
- **堆分配器(内核)** — 实现了kmalloc, kfree, kcalloc, krealloc
- **堆分配器(用户)** — 实现了堆分配器在系统调用

## 下载

最新标签版本包含由 GitHub Actions 自动构建、可通过 GRUB 启动的 ISO：

[下载 myos.iso](https://github.com/My-ABC/my-os/releases/latest/download/myos.iso) · [查看所有 Releases](https://github.com/My-ABC/my-os/releases)
- **ELF32** — 校验并装载内存中的 i386 ELF 可加载段，支持用户页权限与 BSS 清零；`elf` 命令将其作为前台 Ring 3 任务调度并返回 shell
- **SVGA** — `svga_blue` 将 GRUB framebuffer 填充为蓝色

## 构建

依赖：`nasm`、i686 工具链、Python 3 和宿主机 `cc`（测试用）、`qemu-system-x86`（运行用）、`grub-pc-bin`、`xorriso`、`grub-common`。

```bash
make build          # -> myos.bin
make run            # 通过 GRUB ISO 启动并打开图形窗口
make run-kernel     # 直接 -kernel 启动，不经过 GRUB framebuffer
make run-nographic  # 串口输出到终端的 QEMU
make run-q35        # 使用 q35 机型运行（更好的 ACPI 支持）
make run-q35-nographic  # 使用 q35 机型运行，串口输出到终端
make run-iso        # 使用 q35 机型通过 ISO 启动并打开图形窗口
make iso            # 生成iso文件
make test           # 运行内核例程的宿主机测试
```

`make test` 使用 Python `unittest` 自动发现 Python 用例，并运行 `test/` 中的每个 shell 测试套件。C 测试直接使用宿主机 `cc`，与内核的 `CC` 交叉编译器无关。可执行的宿主测试覆盖字符串函数、RTC 日期/时区计算、ELF32 输入校验、GDT/IDT 描述符编码，以及带硬件 I/O stub 的键盘、PIC、PIT 和串口行为。静态源码和布局检查覆盖 ACPI、heap、I/O、paging、panic、PMM、shell、syscall、trap、TSS、VBE、VGA 和 IRQ0 栈帧约定。这些测试不会执行特权指令、访问物理地址或启动 QEMU；硬件行为仍需通过模拟器或设备验证。C 检查和具名 Python 用例都会输出清晰的测试名称及 PASS/FAIL 状态。

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
ring3    在后台启动一个 Ring 3 进程
ps       查看 Ring 3 进程
fg       显示指定 Ring 3 进程输出（fg <pid>）
bg       隐藏指定 Ring 3 进程输出（bg <pid>）
kill     按 PID 结束 Ring 3 进程（kill <pid>）
elf      装载并运行内置 ELF32 测试程序
svga_blue 将 framebuffer 填充为蓝色
```

每次运行 `ring3` 只会在后台启动一个 Ring 3 测试进程。进程仍由 PIT 定时器抢占调度，
但默认不输出；运行 `fg <pid>` 显示指定进程的输出，`bg <pid>` 再次隐藏。
`ps` 查看进程状态，`kill <pid>` 结束指定进程。

`elf` 命令会以前台任务启动内置 ELF32 程序，因此可以看到其 `Hello from ELF32`
系统调用输出，且 shell 不会被阻塞。使用 `bg <pid>` 隐藏输出，使用 `kill <pid>`
结束程序；结束时会释放 ELF 用户映射，因此之后可以再次运行 `elf`。

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

[GPLv2](LICENSE)
