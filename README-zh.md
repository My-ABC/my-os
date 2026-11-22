[English](README.md) | 中文 | [文档](docs/zh/)

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
- **RTC** — CMOS 实时时钟，读取年/月/日和时/分/秒，处理 12/24 小时 CMOS 模式，支持4位数年份避免千年虫问题，UTC→北京时间换算和Unix时间戳
- **蓝屏** — INT3 在蓝底屏幕上 dump 全部寄存器，然后重启
- **ACPI 重启** — 通过 FADT reset register 复位，依次回退到 `0xCF9`、键盘控制器、三重错误
- **ACPI 关机** — 通过 ACPI S5 状态关机，备用方案使用常见 ACPI I/O 端口
- **Ring 3 进程** — 使用 PIT IRQ0 抢占式软件上下文切换，最多运行两个用户进程，每个进程有独立的内核栈和用户栈
- **进程管理** — Shell 支持 `ps`、`fg <pid>`、`bg <pid>` 和 `kill <pid>`；Ring 3 进程默认在后台运行
- **系统调用** — Ring 3 可输出文本、通过 `PROC_GETPID` 查询进程 PID，并用 `PROC_YIELD` 主动让出 CPU
- **信号** — `SIGINT`/`SIGKILL` 及每进程 pending 状态；Ctrl+C 向前台进程投递 `SIGINT`，用户注册的 handler 经 restorer 跳板和 `SIG_RETURN` 执行
- **shell(内核)** — 制作了一个内核态的shell
- **内存分页** — 实现了完整内存分页
- **PAE 分页** — 使用 64 位 PAE 页目录/页表项、32 字节 PDPT 和 32 位 CR3，并通过映射窗口访问高端物理页与页表。PMM 支持 64 GiB 以下的物理地址（36 位 PAE 上限）；实际可用内存由 Multiboot 内存图决定。当前启动阶段要求 Multiboot 信息块和内存图缓冲区位于低 16 MiB。可选的 `make test-qemu-highmem` 会在 QEMU `-m 6G` 下启动，分配 4 GiB 以上的真实物理页，验证 PAE 映射及读写，然后释放该页；标准 QEMU 集成测试仍使用 128 MiB。
- **PMM** — 实现了PMM
- **堆分配器(内核)** — 实现了kmalloc, kfree, kcalloc, krealloc
- **堆分配器(用户)** — 实现了堆分配器在系统调用
- **ATA PIO** — 支持主盘的 LBA28 扇区读取与写入
- **FAT32** — 支持文件和目录操作、ASCII 长文件名（LFN，遵循 FAT32 的
  255 个 UTF-16 代码单元上限）及 8.3 短别名；Shell 路径缓冲区会进一步限制
  可输入长度；非 ASCII 的 UTF-16LE 字符显示为 `?`

## 下载

最新标签版本包含由 GitHub Actions 自动构建、可通过 GRUB 启动的 ISO：

[下载 myos.iso](https://github.com/My-ABC/my-os/releases/latest/download/myos.iso) · [查看所有 Releases](https://github.com/My-ABC/my-os/releases)
- **ELF32** — 校验并装载内存中的 i386 ELF 可加载段，支持用户页权限与 BSS 清零；`elf` 命令将其作为前台 Ring 3 任务调度并返回 shell
- **页错误处理** — ISR14 仅对已登记的用户范围（ELF 镜像、32 KiB 用户栈、用户堆）按需映射并清零页面；非法用户访问或保护违规会终止对应进程，内核态非法缺页进入 panic
- **SVGA** — `svga_blue` 将 GRUB framebuffer 填充为蓝色

## 构建

各目标依赖：构建内核需要 `nasm` 和 i686 工具链；宿主机测试需要 Python 3 和宿主机 `cc`；
QEMU 运行及集成测试需要 `qemu-system-i386`；ISO 目标还需要 `grub-pc-bin`、`xorriso`
和 `grub-common`。`make test-qemu-highmem` 也需要这些 GRUB/ISO 工具来生成临时测试镜像。

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
make test-qemu      # 构建内核并运行有时限的无头 QEMU 集成测试
make test-qemu-highmem  # QEMU -m 6G：分配、映射、读写并释放 4 GiB 以上的物理页
make disk           # 创建 64 MB FAT32 格式的 disk.img
make run            # 有 disk.img 就挂载，否则不连接磁盘
make run-disk       # 若需要则创建 disk.img，再通过 -kernel 启动并连接磁盘
```

内核启动时会探测主盘 ATA 设备；检测到磁盘后会自动执行 Shell 的磁盘
初始化逻辑并尝试挂载 FAT32。`make run` 不会自动创建磁盘镜像；需要磁盘时
先运行 `make disk`。

测试按用途整理在 `test/` 下：`python/` 放宿主 Python 用例和源码合约检查，`c/` 放宿主 C 测试程序，`shell/` 放各 C 测试对应的独立启动脚本，`support/` 放共享编译器脚本和断言头文件。只在 QEMU 中运行的检查位于 `integration/`，其中入口、QEMU 会话、FAT32 测试镜像和行为场景分别放在独立模块中。`make test` 自动发现 Python 用例，并由它们运行各个 shell 测试套件。C 测试直接使用宿主机 `cc`，与内核的 `CC` 交叉编译器无关。PMM 宿主测试还会验证 4 GiB 以上物理页的分配与释放。这些测试不会执行特权指令、访问物理地址或启动 QEMU；硬件行为仍需通过模拟器或设备验证。C 检查和具名 Python 用例都会输出清晰的测试名称及 PASS/FAIL 状态。

`make test` 特意保持为纯宿主机测试，不需要 QEMU 或内核交叉工具链
（需要 Python 3 和宿主机 `cc`）。`make test-qemu` 需要内核构建工具
（`nasm` 及 i686 编译器/链接器）、Python 3、`mkfs.fat` 和
`qemu-system-i386`；它会构建
`myos.bin`，以无头 TCG 模式启动
`qemu-system-i386`，通过 QEMU monitor 驱动 PS/2 键盘，并检查 COM1 串口输出。
测试会挂载自动生成的 FAT32 IDE 磁盘，并覆盖启动、Shell 输入、ATA/FAT32
文件写入与读取；它还会把由 `boot/elf_demo.asm` 汇编生成的 ELF 程序放入测试
磁盘，再通过 `run demo.elf` 执行。其余检查覆盖 Ring 3 调度/列表/前后台切换/
结束，以及 ELF32 装载、执行、清理和重新装载。测试有整体时限，并为启动、串口和 monitor 等待
设置超时；每项检查都会输出具名 PASS/FAIL，结束或失败时都会终止 QEMU。
该目标需要单独安装 QEMU；可通过 `make test-qemu QEMU=/path/to/qemu`
指定可执行文件。测试使用一种 QEMU PC/TCG 配置，不能替代其他固件、真实硬件
测试，也不验证 GRUB ISO 启动。测试使用 Python 内置 socket，无需宿主机
`timeout`、`socat` 或 netcat。

`make test-qemu-highmem` 是独立且有时限的 GRUB/Multiboot QEMU TCG 测试，
为虚拟机提供 6 GiB 内存。它会分配 4 GiB 以上的物理页，建立映射并校验写入
和读回的数据，再取消映射、释放页面并确认该页可重新分配。测试使用独立的
`build/highmem-test/` 目录，不改变标准 QEMU 集成测试的 128 MiB 内存配置。

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
time     获取时间（time 12 为 12 小时 AM/PM）
panic    触发INT 3
echo     打印文字到屏幕和串口
reboot   ACPI重启
shutdown ACPI关机
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
disk     初始化 ATA 并检测 FAT32
ls       列出当前目录；`ls <路径>` 列出指定目录
cat      输出 FAT32 文件内容
write    向 FAT32 文件写入文本
cp       复制 FAT32 文件；`cp -r <目录> <目标>` 递归复制整个目录树
mv       移动或重命名 FAT32 文件/目录；目标为已存在目录时移入其中，
        目标为已存在文件时替换之
rm       删除 FAT32 文件
cd       切换 FAT32 目录
mkdir    创建 FAT32 目录
rmdir    删除空 FAT32 目录；`rmdir -f <目录>` 递归删除目录及其内容
run      装载并运行磁盘中的 ELF32 文件
```

FAT32 长文件名支持 `help_aaaaa.1234` 这样的 ASCII 名称。通过 Shell 创建的
每个文件和目录都会写入 LFN 项，因此名称保留原始大小写（`write testtest`
在 `ls` 中显示为 `testtest` 而非 `TESTTEST`）。磁盘上使用 UTF-16LE LFN
目录项并关联自动生成的 8.3 短别名；名称查找按 LFN 精确匹配（区分大小写），
没有 LFN 的目录项仍通过大写 8.3 短名匹配。Shell 当前接受 ASCII 文件名；
非 ASCII 的 UTF-16 字符显示为 `?`。Shell 路径缓冲区限制可输入长度，
不超过磁盘上 FAT32 LFN 的最大值。

挂载 FAT32 磁盘时，Shell 提示符会显示当前路径，例如 `myos / >` 或
`myos /1 >`，运行 `cd ..` 可返回上一级目录。没有挂载 FAT32 磁盘时，
提示符保持为 `myos >`。

每次运行 `ring3` 只会在后台启动一个 Ring 3 测试进程。进程仍由 PIT 定时器抢占调度，
并在等待时通过 `PROC_YIELD` 主动让出 CPU，保持 Shell 输入响应；默认不输出，
运行 `fg <pid>` 显示指定进程的输出，`bg <pid>` 再次隐藏。
`ps` 查看进程状态，`kill <pid>` 结束指定进程。
前台进程运行时 Shell 会隐藏提示符，避免程序输出落在提示符后面；键盘输入仍可用，
因此仍能输入 `bg <pid>` 或 `kill <pid>`。进程退出或切到后台后会重新显示提示符。

`elf` 命令会以前台任务启动内置 ELF32 程序，因此可以看到其 `Hello from ELF32`
系统调用输出。程序安装 `SIGINT` 处理器后进入等待；按 Ctrl+C 后处理器打印
`Bye` 并通过 `PROC_EXIT` 退出，退出时会释放 ELF 用户映射，因此之后可以再次
运行 `elf`。

## 目录结构

```
boot/             Multiboot 头、启动代码和中断桩 (NASM)
src/kernel.c      内核初始化和顶层启动流程
src/arch/x86/     x86 描述符、中断和端口 I/O
src/mm/           物理/虚拟内存与堆
src/os/           ELF 装载、Ring 3 和系统调用
src/shell/        交互式 shell
src/drivers/      ATA、控制台、键盘、定时器、RTC 和显示驱动
src/fs/           FAT32 文件系统
src/lib/          freestanding C 标准库函数
include/          按 src/ 子系统对应分类的头文件
docs/             中英双语架构文档与参考（en/ + zh/）
scripts/          CI/CD 文件
linker.ld         1MB 加载地址与段布局
[TODO.md](./TODO.md) 英文项目路线图 | [TODO-zh.md](./TODO-zh.md) 中文项目路线图
```

## 说明

QEMU 默认的 `pc` 机型（SeaBIOS）只提供 ACPI 1.0 的 FADT，里面没有 reset register，所以那里的重启实际走 `0xCF9` 兜底；用 `-machine q35` 时固件提供 ACPI 2.0+ FADT，才会真正使用 reset register。

## 许可证

[GPLv2](LICENSE)
