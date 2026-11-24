# MyOS 代码审计报告（P0–P3）

审计范围：`src/`、`boot/`、`include/` 全量重读（约 1.2 万行）。mini-libc 按约定不在本次范围。
行号基于 `feature/vt-f1-f2` 分支 HEAD（`e03d9b7` 之后）。

严重度定义：**P0** 必坏 / **P1** 可能坏 / **P2** 隐患 / **P3** 风格。
状态：✅ = 已修复；⬜ = 未修复（文档已记录或待后续 PR）。

本轮相对上一版的变化：完成了 mm/fs/acpi-adjacent/shell_ring3/trap/idt 的逐行审计，
新发现 3 个 P1（虚拟地址冲突、堆空闲链表截断、FAT32 写路径数据丢失）和一批 P2/P3。

---

## 一、P1 — 可能坏（本轮新发现）

| # | 位置 | 问题 | 触发条件 | 状态 |
|---|------|------|----------|------|
| N1 | `src/drivers/vt.c:21` vs `src/mm/heap.c:7` | **`VT_FB_BASE = 0xD0000000` 与 `KERNEL_HEAP_START = 0xD0000000` 完全重叠。** VBE 模式下 `vt_init` 先把两个终端的离屏帧缓冲映射到 0xD0000000 起；随后任何 `kmalloc`（`heap_next` 同样从 0xD0000000 起）通过 `paging_map_page` **无条件覆盖同一批 PTE**——fb 物理页被挤出映射（泄漏），堆块头写进帧缓冲区域，vt 切换时 memcpy 出来的是堆数据。kmain 的 heap 自检（kmalloc(100)）就足以触发。 | 任何 GRUB/VBE 启动 + 首次内核堆分配 | ✅ 已修（`018c4e0`+`a209efe`/`9fa802a`）：最终 `VT_FB_BASE=0xE8000000`、`VT_FB_LIMIT=0xF0000000`——位于堆窗口（0xD0000000–0xE0000000）与 ~4MiB scanout 之上、KMAP 槽位之下，内核镜像 headroom ~740MiB（0xD8000000 落在堆窗口内被否决；0xE8000000 方案曾撤回后又按用户决定恢复） |
| N2 | `src/mm/heap.c:100-124` + `kmalloc:175-176` | **`heap_split` 摘除空闲块后继承的是已清零的 prev/next**：`heap_remove_free` 把 `block->next/prev` 置 0，`heap_split` 让 remainder 继承这两个 0，于是 `free_list = remainder`——**其他所有空闲块被孤立**（free=1 但不在链表上），永久泄漏 + 无法合并 → 堆随碎片化最终 OOM。仅当"拆分"发生（free block 比请求大）且链表中还有别的空闲块时触发；整块分配（remaining==0）不触发。 | 释放多个不同大小块后再次 kmalloc | ✅ 已修（`222ff5d`）：remainder 改走 `heap_insert_free` 有序重插 |
| N3 | `src/fs/fat32.c:76-102` | **`fat32_write_file` 先 `free_cluster_chain(old)` 再分配/写新簇**：磁盘满（`allocate_cluster` 失败）或 `write_cluster` 中途失败时直接 return -1，但目录项仍指向**已被释放的旧簇** → 已有文件内容被毁（读到的是空闲簇/新文件数据）。 | 覆盖写一个已存在文件且磁盘空间不足 | ✅ 已修（`183752b`）：新簇链写完且目录项更新成功后才释放旧链 |

## 二、P1 — 上轮已修（保持记录）

| # | 位置 | 问题 | 状态 |
|---|------|------|------|
| 1 | `syscall.c` IO_WRITE | 用户字符串指针未校验 → 内核 panic | ✅ `sys_copy_user_string` + 4096 上限，失败返回 -1 |
| 2 | `syscall.c` IO_READ | `sys_gets` 无指针/size 校验 | ✅ `size<=0` 或 `sys_user_buffer_ok` 失败返回 -1 |
| 3 | `syscall.c` 校验判据 | 初版用 range-permitted 判定，漏映射页 | ✅ `ac88eac`：改 `paging_mm_get_phys` 判定实际映射 |
| 4 | `syscall.c` `user_realloc` | `ptr-20` 无范围校验直接解引用 → 内核 panic | ⬜ 保留为 `kfault.elf` panic 演示路径；正式修法见 #22 |
| 8 | `vt.c` `spawn_failed` | 瞬时故障永久锁存 | ✅ `VT_SPAWN_RETRY` 可重试（~1Hz），ELF 错误才锁存 |
| 13 | `serial.c` TX 等待 | 死等 LSR bit5 无超时 | ✅ `SERIAL_TX_TIMEOUT` 有界等待 |

## 三、P2 — 隐患

| # | 位置 | 问题 | 触发条件 | 状态 |
|---|------|------|----------|------|
| N4 | `serial.c:47-54` | COM3 超时打印 `"output dropped"` 警告后**仍然 `outb` 写出该字符**——警告与行为不符；COM1 超时则完全静默。且超时后每字符仍烧 ~1M 次 port 读。 | 无 COM3 后端的真机/部分 QEMU 配置 | ⬜ 二选一：超时后真正 drop（return），或改警告文案 |
| N5 | `elf.c:120-124` + `ring3.c:204-221` | **mm 双重释放**：`ring3_schedule_internal` 失败路径已 `paging_mm_release(mm)`（含调用方传入的 mm），`elf_run_vt` 拿到非零返回又 `elf32_release_mm(mm)` 再放一次。两次释放之间 IRQ0 让出的窗口内，若槽位被新进程复用，第二次 release 会拆掉别人的地址空间。 | spawn 失败 + 精确时序 | ⬜ 修法：约定"schedule 失败不释放调用方 mm"或 elf_run_vt 不重复释放 |
| N6 | `syscall.c:301-332` | `sys_user_buffer_ok`/`sys_copy_user_string` 要求**页已映射**，而栈只有栈顶一页预映射、其余靠 isr14 demand-map → `IO_READ`/`IO_WRITE` 对落在未触碰栈页上的缓冲/字符串返回 -1。mini-libc 当前用堆/静态缓冲所以没暴露，属潜伏 ABI 缺口。 | 用户程序把 IO 缓冲放在栈的低地址区 | ⬜ 修法：校验时把"permitted 但未映射"的页视为可 demand-map（调用 isr14 同款分配），或文档明确限制 |
| N7 | `idt.c:9-15` | 除 #BP(3)/#PF(14) 外所有异常/中断走 `default_handler` = 打一行 VGA 错误后 `cli;hlt` 死循环——#DE/#GP/#SS 等内核异常**静默挂死**，无寄存器转储、无 panic。 | 任意未处理异常 | ⬜ 修法：default_handler 转 `panic_blue_screen`（需要 regs，需 asm 桩配合） |
| N8 | `trap.c:62-66` | demand-map 的许可范围含 `sys_user_heap_contains`（0x1000000–0x40000000 整段）：访问**已 free 的堆页**（user_free 会 unmap）会被静默 demand-map 成新的零页而不是 fault → 掩盖堆 UAF；且按读写缺页一律给 RW 页。 | 进程读写已释放的堆内存 | ⬜ 修法：demand-map 只对"从未映射过"的页生效（需追踪），或收窄许可范围到栈/镜像 |
| N9 | `paging.c:138-186` | `paging_map_page` 对已存在 PTE **静默覆盖**（旧物理页泄漏）——N1 的直接成因；且不检查 `PAE_PAGE_SIZE`，若某级是大页会把页表指针当物理页写 → 破坏任意内存（当前无大页，属潜伏）。 | 重复映射同一虚拟地址 | ⬜ 修法：映射前查旧 PTE，存在则拒绝或先 unmap；大页显式拒绝 |
| N10 | `syscall.c` fd 表 / FS_READDIR | fd 表全局共享、进程退出不回收（上轮 #7 保留）；`FS_READDIR` 内核缓冲固定 4096，目录更大时**静默截断**返回成功。 | 多进程共用 fd；大目录 | ⬜ 文档已载 fd 限制；READDIR 截断建议返回已写字节数+截断标志或文档注明 |
| N11 | `heap.c:218-275` `krealloc` | 与 `kfree` 不同，**没有堆范围/magic 边界校验**：`ptr` 任意值时 `block->magic` 读的是任意内核地址，碰巧匹配 `0x48454150` 则后续写 remainder 头 → 内存破坏。当前调用方都是可信内部代码。 | 未来出现不可信调用方 | ⬜ 修法：补 `kfree` 同款范围检查 |
| N12 | `fat32.c:228-237` `fat32_move` | 目标已存在时**先删 dst 再 insert**：insert 失败（目录满/短名冲突）→ dst 文件已丢。与 N3 同类"先破坏后提交"。 | mv 覆盖已存在文件且 insert 失败 | ⬜ 修法：insert 成功后再删旧 dst（需要两步提交或回滚） |
| N13 | `fat32_core.c:86-101` `allocate_cluster` | 从簇 2 起线性扫描，每个候选簇都 `fat_next_cluster` → **每簇一次磁盘扇区读**，大盘分配是 O(n²) I/O。64MiB 测试盘无感，真实大盘会很慢。 | 大容量 FAT32 卷频繁写文件 | ⬜ 修法：扫描 FAT 扇区缓存/free-run 提示，或从上次分配点续扫 |
| N14 | `fat32.c:104-118` `free_cluster_chain` / `fat32_delete_file` | 先 `free_cluster_chain` 后 `mark_entry_deleted`：mark 失败 → 目录项指向已释放簇（与 N3 同类，概率低）。 | mark 写盘失败 | ⬜ 低概率，记录 |
| N15 | `stdio.c:264` `sprintf` | **完全无边界检查**（`vsprintf_helper` 往 `str` 无限写）。内核内部调用方需自觉；`gets`（内核 shell 用）同样无信号/让出。 | 误用 sprintf 到小缓冲 | ⬜ 内部函数，建议加注释或换 snprintf |
| N16 | `paging.c:257-292` `paging_mm_create` | `mm_ensure_pt` 失败时 `continue` 仍返回 mm——低 4MiB 只映一半，进程带"残缺"地址空间运行。 | PMM 耗尽时 spawn | ⬜ 概率低；可改为失败即释放槽位返回 NULL |
| N17 | `trap.c:18-37` `isr14_demand_map` | `memset((void*)page)` 通过**当前 CR3** 写用户页——依赖"缺页进程必为当前进程"（单核下成立）；且对镜像范围内的缺页也给 RW 页（镜像页本应按段权限，实际加载时已映射，属死代码路径）。 | 未来 SMP/嵌套 fault | ⬜ 单核下正确，记录不变量 |
| N18 | `fat32_dir.c` 回调约束 | `fat32_list_directory` 用共享静态 cluster 缓冲，回调内禁止文件 I/O——`sysfs_readdir_cb` 已遵守，但属易碎约束。 | 未来回调里加文件操作 | ⬜ 架构约束，文档已载 |

## 四、P2/P3 — 其余记录项（合并上轮未修项）

| # | 位置 | 问题 | 级别 |
|---|------|------|------|
| 5 | `syscall.c` IO_READ/IO_GETCHAR | 无前台检查，后台进程可抢按键 | P2 ⬜ 文档已载 |
| 6 | `syscall.c` `sys_gets` | 方向键控制字节 0x01–0x04 原样入缓冲并回显 | P3 ⬜ 文档已载 |
| 9 | `vt.c` `vt_do_switch` | 任一侧 fb 缺失拒绝切换，仅串口日志 | P2 ⬜ 文档已载 |
| 10 | `vt.c` `vt_alloc_fb` | 中途失败泄漏已映射页（叠加 N1 更严重） | P2 ⬜ 文档已载 |
| 11 | `vt.c` `vt_pending` / `ring3.c` `foreground_pid` | ISR 写/服务读非原子，单核窗口极窄 | P3 ⬜ 文档已载 |
| 12 | `vt.c` `vt_spawn_shell` | spawner 全程关中断（有意为之） | P3 ⬜ |
| 15 | `keyboard.c` | F1/F2 只认 scan set 1；E1 前缀（Pause）未处理会被当普通扫描码 | P3 ⬜ |
| 17 | `vga.c`/`vbe.c` | 后台进程 IO_CLEAR/光标操作作用于自己 vt 缓冲（正确但无前台检查） | P3 ⬜ |
| 19 | `ring3.c` `ring3_kill` | 对 current 只标 DEAD，回收靠下个 tick | P3 ⬜ |
| 20 | `elf.c` `elf_run` | `image_start/end=0` 调用方（内嵌 demo/PROC_RUN_USERCODE）不注册用户范围，demand-map/保护检查对其部分失效 | P2 ⬜ |
| 22 | `paging.c` `paging_mm_unmap`/`paging_try_reclaim_page_table` | 回收 PT 后 `paging_flush_tlb(0)` 只刷 VA 0 一个 TLB 项；单核无 PCID 时 CR3 切换即全刷，实际无害但写法误导 | P3 ⬜ |
| 24 | `pmm.c:217-225` | `pmm_alloc_page_at_or_above`（仅 `PMM_QEMU_HIGHMEM_TEST`）非 wrap 模式下 `frame>=total→0` 回绕后仍按 limit 扫描，可能分配到低于 minimum 的帧 | P3 ⬜ 测试专用 |
| 26 | `fat32.c` `fat32_read_file` | `*size` 截断到 buffer_size 且返回 0（成功）——>1MiB 文件被静默截断；write 路径有 `FAT32_MAX_FILE_SIZE` 检查拒绝超限 | P3 ⬜ 调用方需自行判断 |
| 27 | `stdio.c` `gets`/`sprintf` | 内核侧无校验/无界（见 N15） | P3 ⬜ |
| 28 | `drivers/acpi.c` | 553 行 ACPI 解析本轮只做了 grep 级排查，未逐行审计 | P2 ⬜ 建议单独审计 |
| 29 | `shell/shell.c` | 内核 shell 行编辑与 shell_ring3 同构，本轮未逐行重读（shell_ring3 已读） | P2 ⬜ |
| 30 | `boot/*.asm` + `interrupt.asm` | 中断帧/TSS/trap gate 合约靠 QEMU 集成测试背书，未逐指令复核 | P2 ⬜ |
| 31 | `drivers/vbe.c:125-133` | `vbe_map_framebuffer` 无上限检查：超大模式 pitch*height 可顶到 KMAP 窗口 0xF0000000 | P3 ⬜ |
| 32 | `drivers/vbe.c` | 非活跃 vt 光标像素冻结在 backing store（外观问题，切回后重画） | P3 ⬜ |
| 33 | `shell_ring3.c:157-166` `parse_pid` | pid 解析 `pid*10` 无溢出检查，超长数字回绕后可能命中真实 pid → kill/fg 错进程 | P3 ⬜ |
| 34 | `shell_ring3.c:807` `cmd_write` | 先 `fs_unlink` 再 `fs_open`：open 失败则原文件已丢 | P3 ⬜ |
| 35 | `shell_ring3.c:68-94` `split_line` | 未闭合引号落入普通分支，引号字符原样进入 argv | P3 ⬜ |
| 36 | `syscall.c:334-379` `sysfs_readdir_ctx`/`sys_user_buffer_ok` | readdir 用静态 ctx（单核 OK）；buffer_ok 逐页 `paging_mm_get_phys`（每页一次 KMAP cli/sti），大 len 参数可被用户用于拖慢内核 | P3 ⬜ |
| 37 | `serial.c:95-105` `serial_getchar` | COM1 RX 非阻塞读取，当前无内核调用方（保留备用） | P3 ⬜ |

## 五、测试基建修复记录

| # | 位置 | 问题 | 状态 |
|---|------|------|------|
| T1 | `test/integration/qemu_session.py` | COM3 迁移后三次修复：加 COM3 后端 → wait_for 双 buffer → COM1/COM3 分缓冲防行内交错 | ✅ |
| T2 | `fault_demo.asm` + `page_fault_scenarios.py` | kfault 前提失效后改用 `MEMORY_REALLOC` 坏指针 | ✅ |
| T3 | `test/mini-libc/run_tests.py` | `serial_text()` 只读 COM1，Ring3 DONE 行在 COM3 → 全部误报 "no DONE line" | ✅ `e03d9b7`：新增 `serial_all_text()` |

## 验证状态

- `make build` ✓、`make test` 45 项 OK
- `make test-qemu`：主套件全 ✓ PASS（最后一轮 kfault 场景在 T2 修复后通过）
- `make test-libmyc`：**18/18 suites ALL PASS**（T3 修复后实测）
- mini-libc：按约定未审计

## 建议修复顺序

1. **N1**（VT_FB_BASE 搬到 0xE8000000）——一行常量改动，消除 VBE 模式下的堆/帧缓冲互踩
2. **N2**（heap_split 用 heap_insert_free）——修复堆空闲链表截断
3. **N3**（fat32_write_file 先写新后放旧）——修复覆盖写数据丢失
4. N4、N5、N9、N11——小改动顺手清
5. N6/N7/N8 涉及 ABI/异常框架语义，建议单独 PR 讨论
