# 10 — 构建与测试

[English](../en/10-build-and-test.md) | 中文

## 构建

`Makefile` 自动收集源文件：

- `C_SRCS = $(shell find src -type f -name '*.c')` —— `src/` 下新增
  `.c` 自动纳入；目标文件按源路径镜像输出到 `build/`。
- `ASM_SRCS` 覆盖 `boot/*.asm`（NASM）；`boot/gdt.asm` 还产出
  `gdt_asm.o`。
- `INCLUDE_DIRS` 列出所有 `include/` 子目录；新增头文件目录时需同步
  （如 `include/shell/`）。
- 链接：`ld -m elf_i386 -T linker.ld -nostdlib` → `myos.bin`
  （入口 `start`，内核 VMA `0xC0000000`，物理加载 1 MiB）。
- `make iso` 用 GRUB（`grub-mkrescue`）把 `myos.bin` 封装为
  `myos.iso`。

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
（依赖 `mkfs.fat`），用 QEMU monitor `sendkey` 驱动 Shell，串口 TCP
socket 读取输出（`qemu_session.py`）。场景模块：`shell_scenarios.py`
（启动、存储、`cp`/`mv`/`cp -r`、LFN 大小写）、`ring3_scenarios.py`、
`page_fault_scenarios.py`（`fault_demo.asm` 汇编出的
`stack.elf`/`faultu.elf`/`faultp.elf`）。全部通过时输出
`PASS: QEMU integration suite` 和 `ALL PASS`；`DEFAULT_TIMEOUT`
为 150 秒。

## 高位内存测试（`make test-qemu-highmem`）

独立的有界运行，使用 6 GiB 客户机内存，验证
`pmm_alloc_page_at_or_above(4 GiB)` 及 PAE 映射/回读。仅在内存充足的
主机上运行。
