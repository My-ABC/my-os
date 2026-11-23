# 11 — 构建与测试

[English](../en/11-build-and-test.md) | 中文

## 构建

`Makefile` 自动收集源文件：

- `C_SRCS` 自动收集 `src/**/*.c`，**排除** `src/shell/shell_ring3.c`
  （单独构建为 `shell3.elf`）；`ASM_SRCS` 覆盖 `boot/*.asm`，**排除**
  `boot/gdt.asm`/`boot/paging.asm`（这两个用特殊参数单独汇编为
  `gdt_asm.o`/`paging_asm.o`）。
- `INCLUDE_DIRS` 列出所有 `include/` 子目录；新增头文件目录时需同步
  （如 `include/shell/`）。
- 链接：`ld -m elf_i386 -T linker.ld -nostdlib` → `myos.bin`
  （入口 `start`，内核 VMA `0xC0000000`，物理加载 1 MiB）。
- `make iso` 用 GRUB（`grub-mkrescue`）把 `myos.bin` 封装为
  `myos.iso`。
- `make disk` 构建 `shell3.elf`（经 `libmyc` + `shell3` 目标）并写入
  `disk.img`（与测试 ELF 同盘）；`make run` 依赖 `iso disk` 并无条件
  挂载该镜像，监督器据此自动启动 Ring 3 shell。其他运行目标：
  `run-kernel`、`run-nographic`、`run-serial`、`run-disk`、
  `run-q35`、`run-q35-nographic`。
- `make test-libmyc` 运行 mini-libc QEMU 测试套件。

常用编译选项：`-m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -g`
加 `-MMD -MP` 依赖跟踪。可选宏：`PANIC_DEMO`、`ELF_DEMO`、
`KEYBOARD_DEMO`、`SCANCODE_SET=n`、`PMM_QEMU_HIGHMEM_TEST`。

## Host 测试（`make test`）

- `test/python/` —— unittest 发现的契约测试，读取内核源码断言结构约
  束（架构、内存、ELF、存储/Shell）。
  `test_storage_shell_contracts.py` 按依赖顺序拼接拆分后的 FAT32 源
  文件再匹配函数体。
- `test/c/` + `test/shell/` —— host 编译的 C 套件，由 shell 脚本经
  `test/support/compile_c_test` 启动。

## QEMU 集成（`make test-qemu`）

`test/integration/run_qemu_integration.py` 用 `qemu-system-i386`
（128 MiB）启动 `myos.bin`，通过 `fat32_image.py` 生成 FAT32 测试盘
（依赖 `mkfs.fat`），用 QEMU monitor `sendkey` 驱动 Shell，并通过
**两个**串口 TCP socket 读输出——COM1 内核诊断与 COM3 Ring 3
`IO_WRITE` 镜像（`qemu_session.py`，COM2 为 `-serial none`）。场景
模块：`shell_scenarios.py`（启动、存储、`cp`/`mv`/`cp -r`、LFN 大小
写）、`ring3_scenarios.py`、`page_fault_scenarios.py`
（`fault_demo.asm` 汇编出的 `stack.elf`/`faultu.elf`/`faultp.elf`/
`oom.elf`/`kfault.elf`）。全部通过时输出 `PASS: QEMU integration
suite` 和 `ALL PASS`；`DEFAULT_TIMEOUT` 为 150 秒。测试使用两个
QEMU 会话：主套件以用户堆 OOM 杀进程收尾（堆块在进程死亡时泄漏，
后续检查不能再申请新页），内核态缺页 panic 由独立的第二个会话验
证（`kfault.elf` 向 `MEMORY_REALLOC` 传坏指针，该路径仍无校验解引
用），因为 panic 会重启机器。

## 高位内存测试（`make test-qemu-highmem`）

独立的有界运行，使用 6 GiB 客户机内存，验证
`pmm_alloc_page_at_or_above(4 GiB)` 及 PAE 映射/回读。仅在内存充足的
主机上运行。
