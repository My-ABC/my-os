# 02 — 内存管理

[English](../en/02-memory.md) | 中文

## 地址空间布局

| 范围 | 用途 |
|---|---|
| `0x00000000`–`0x000FFFFF` | 实模式 IVT/BDA 区，启动后基本不再使用 |
| `0x00100000` 起 | 内核物理加载地址（1 MiB） |
| `0x00800000` | 启动期映射冒烟测试页（临时） |
| `0x01000000`–`0x40000000` | Ring 3 用户堆（`USER_HEAP_START`/`USER_HEAP_END`） |
| `0x810000`、`0x820000` | 每进程 Ring 3 用户栈，各 32 KiB |
| ELF 镜像区间 | 按存活 Ring 3 进程按需映射 |
| `< 0xC0000000` | 用户区（`USER_SPACE_END`） |
| `0xC0000000`+ | 内核高半区映射（`KERNEL_VIRT_BASE`） |
| `0xF0000000` | 页表临时访问映射（`KMAP_BASE`） |

内核链接在虚拟 `0xC0000000` / 物理 `0x100000`（`linker.ld`）；
`.text/.rodata/.data` 通过 `AT()` 指定物理地址。

## PAE 分页（`src/mm/paging.c`）

- 4 级页表（PDPT → PD → PT）、4 KiB 页、64 位 PTE
  （`PAE_PRESENT`、`PAE_WRITABLE`、`PAE_USER`、`PAE_ACCESSED`、
  `PAE_DIRTY`、`PAE_PAGE_SIZE`、`PAE_NX`）。
- 物理地址上限 36 位（< 64 GiB），`PMM_PHYSICAL_CEILING`。
- 页表页没有永久映射；`paging_map_physical()` 在 `0xF0000000` 临时
  映射一个物理页并关中断，`paging_unmap_physical()` 恢复现场。
- `paging_map_page()` 拒绝在内核地址空间建立 `PAGE_USER` 映射；
  `paging_unmap_page()` 对缺失项告警。
- `paging_init()` 建立运行时页表：内核高半区映射、内核所需的低端恒等
  覆盖，用户区留空交给按需分页。

## PMM（`src/mm/pmm.c`）

覆盖 36 位物理上限内 4 KiB 帧的位图分配器。`pmm_init()` 消费
Multiboot 内存图（仅可用区域）。基本操作为 `pmm_alloc_page()` /
`pmm_free_page()`；`pmm_alloc_page_at_or_above()` 支持 >4 GiB 高位内
存测试。

## 内核堆（`src/mm/heap.c`）

`kmalloc`、`kcalloc`、`krealloc`、`kfree` 从 PMM 帧分配可变长块。Shell
（`cp -r` 目录项列表、`run` 镜像缓冲）、Ring 3 进程结构和 FAT32 复制路
径都在用。

## 用户地址空间

用户内存全部按需映射：

- **ELF 镜像区间** —— 按存活进程记录，加载器按需映射代码/数据页。
- **用户栈** —— 两个 32 KiB 区域，位于 `0x810000` 和 `0x820000`，进程
  终止时整体释放。
- **用户堆** —— `0x01000000`–`0x40000000`，由 `src/os/syscall.c` 的
  `SYS_MEMORY` 子操作管理。

## ISR14 页错误处理（`src/arch/x86/trap.c`）

`isr14_handler` 对每次 fault 分类：

- **用户/内核判定**：错误码 bit 2（U/S）或 `cs & 3`。
- **`P=0` 且地址落在已登记用户范围**（存活进程 ELF 区间、用户栈、
  `USER_HEAP_*`）：分配物理帧，映射 `PRESENT|WRITABLE|USER`，清零后
  返回继续执行。
- **保护违例（`P=1`）或未登记用户访问**：通过
  `ring3_terminate_current()` 终止出错进程。
- **内核 fault**：panic。

返回约定：`0` = 正常 `iret`；非 0 = 内核 `irq_frame *`，用于恢复被中
断的上下文。`boot/interrupt.asm` 的 `isr14` 桩检测到返回值后切栈并重
放 IRQ0 出栈序列。

范围判定在 `ring3_user_range_permitted()`（`src/os/ring3.c`）和
`sys_user_heap_contains()`（`src/os/syscall.c`）。
