# 01 — 启动流程

[English](../en/01-boot.md) | 中文

## Multiboot 头

`boot/boot.asm` 开头是 Multiboot 头（magic `0x1BADB002`，flags `0x07`），
GRUB 据此把内核加载到物理 1 MiB 并跳转到 `start`，`%ebx` 为 Multiboot
信息结构的物理地址。头部同时请求 640x480 视频模式。

## 早期页表

任何 C 代码运行之前，`boot.asm`：

1. 检查 CPUID PAE 支持（CPUID leaf 1，EDX bit 6），不支持则挂起。
2. 在 `.bss` 中构建静态 PAE 页表：
   - `early_pae_pdpt` + `early_pae_pd0..pd3` —— 四个 512 项页目录。
   - `early_pae_pt_low` —— 前 16 MiB 低端恒等映射。
   - `early_pae_pt_high` —— 高半区别名：虚拟 `0xC0000000` 映射到物理
     `0x00100000`（内核加载地址），共 16 MiB。
3. 置位 CR4.PAE（bit 5），CR3 载入 `early_pae_pdpt`，置位 CR0.PG
   （bit 31）。

这套早期布局只为到达 `kmain`；运行时页表由 `paging_init()` 重建（见
[02-memory.md](02-memory.md)）。

## 进入 kmain

分页启用后，`boot.asm` 跳到 `.pae_entry`，从栈上恢复 `%ebx`
（Multiboot 信息指针）并调用 `kmain`。失败路径（PAE 不支持、`kmain`
返回后）进入 `cli; hlt` 循环。

## kmain 初始化序列

`src/kernel.c:kmain(uint32_t multiboot_info)` 按顺序执行：

1. `gdt_init()` + `tss_init()` —— 描述符表（必须最先）。
2. `serial_init()` —— COM1 早期调试输出。
3. `pmm_init(multiboot_info)` —— 依据 Multiboot 内存图建立位图分配器。
4. `paging_init()` + `tss_update_cr3()` —— 运行时 PAE 页表；TSS 的
   `esp0` 跟随新的 CR3。
5. `PMM_QEMU_HIGHMEM_TEST` 宏下可选的 `highmem_qemu_test()`。
6. 重映射 Multiboot 信息页，供后续 VBE 探测读取。
7. `vbe_initialize(multiboot_info)`（BIOS VBE 信息在启动前捕获）然后
   `vga_init()`。
8. `ata_init()` 探测；成功则调用 `cmd_disk()` 挂载 FAT32。
9. `idt_init()` —— 异常/IRQ/系统调用门。
10. 堆自检（`kmalloc`/`kfree`/`kcalloc`/`krealloc`）与 `0x00800000`
    处的 PMM+映射冒烟测试。
11. `pic_remap()`、`pit_init(100 Hz)`、`keyboard_init()`、`sti`、
    `rtc_init()`。
12. `PANIC_DEMO` / `ELF_DEMO` / `KEYBOARD_DEMO` 宏控制的演示路径，
    否则进入 `start_shell()`。

> 注意：`kmain` 把真实初始化和启动期自检日志混在一起。串口标记
> （`MyOS v0.0.1`、`[HEAP] ...`、`[PAGING] ...`）是
> `test/integration/` QEMU 契约的一部分。
