# MyOS 代码审计报告（P0–P3）

- 审计基线：`feature/vt-f1-f2` @ `b1989bf`（工作区干净，未新建 feature 分支）
- 范围：`src/`、`include/`、`boot/` 全量重读（约 1.14 万行：boot 汇编入口、全部头文件、内核/内存/中断/Ring3/syscall/ELF/FAT32/驱动/串口/ACPI/VBE/VGA/VT/双 shell）
- 方法：按 `.github/agents/my-os-kernel.agent.md` 的架构约束逐文件核对；对照 `regs.h`/`interrupt.asm`/`idt.c` 栈帧契约、VT/串口/KMAP/PDE 复用/内存所有权/syscall 用户指针边界逐项验证
- 验证：本轮为文档审计，未改代码；已运行 `make build`（通过）与 `make test`（45 项通过）。未运行 `make test-qemu`

## 严重度定义

- **P0 必坏**：现有正常路径必然损坏/死机。
- **P1 可能坏**：特定但合法/可达输入会损坏、泄漏内核信息或 panic。
- **P2 隐患**：正确性/一致性/可维护性风险，当前测试未覆盖或需要边界条件触发。
- **P3 风格**：健壮性、命名、注释、非关键边界。

状态：✅ 已修复 / ⬜ 未修复。

---

## P0

无。未发现“正常使用必然损坏”的缺陷。

---

## P1（可能坏）

### P1-1 用户堆元数据全局共享，但虚拟地址空间是每进程私有的 ⬜

`src/os/syscall.c:126-285`：`user_heap_next` 与 `user_free_list` 是全局变量，而 `user_alloc/user_free/user_realloc` 使用的是 `ring3_current_mm()` 的私有 `user_mm`。

- 进程 A `MEMORY_FREE` 后，`user_free_list` 指向 A 地址空间中的 `user_block`；进程 B `MEMORY_MALLOC` 复用时，`user_find_free/user_restore_pages` 在 B 的 CR3 下解引用该用户虚拟地址。若 B 未映射同一 VA，会被 `isr14` demand-map 成全零页（`src/os/trap.c`），读到 `pages=0,next=0`；若 B 恰好映射了同 VA，则读到无关数据。
- `user_heap_next` 全局递增使各进程堆 VA 不复用，浪费 1 GiB 用户地址空间；但真正危险是 free list 跨 mm。
- `user_free` 只校验 `magic/free`，不校验 `block->pages` 上界、`address+pages*PAGE_SIZE<=USER_HEAP_END`，也不校验页是否属于该 block；`paging_mm_unmap`（`src/mm/paging.c`）未像 `paging_mm_get_phys` 那样检查 `USER_MM_SPACE_LIMIT`，对 `>=0x40000000` 的地址会按 `&0x1FF` 回绕到 pd0/pt0，可能误清低地址映射。

影响：多 Ring3 进程同时使用 mini-libc 堆时可能映射错误页、泄漏 PMM 页或破坏其它进程堆。建议：把 user heap free list/next 放进 `struct user_mm`（或按 mm 分链），并在 `user_free/user_realloc` 校验 block 范围、pages 上界与 `USER_HEAP_END`，同时给 `paging_mm_unmap` 补 `virtual_addr < USER_MM_SPACE_LIMIT` 检查。

### P1-2 `user_realloc` 在校验前解引用用户可控指针 ⬜

`src/os/syscall.c:258-270`：`user_realloc(ptr,size)` 直接计算 `block=ptr-sizeof(struct user_block)` 并读取 `block->magic/block->free`，没有先做 `user_free` 同款的 `USER_HEAP_START..USER_HEAP_END` 范围检查。`MEMORY_REALLOC` 的 `a1` 完全由用户控制；`kfault` 仍保留 `MEMORY_REALLOC` 坏指针作为 panic demo。

- 指向未映射地址 → 内核页错误 panic；指向可控用户内存且伪造 `USER_BLOCK_MAGIC` → 可能让 `memcpy(new_ptr, ptr, copy_size)` 从任意地址复制到用户（信息泄漏）或误释放。
- 这是 syscall ABI 边界缺陷，不只是演示路径。建议：与 `user_free` 一致先做范围检查，再校验 magic/free；demo 另找稳定触发方式。

### P1-3 `krealloc` 缺少与 `kmalloc` 相同的 `size` 上限检查，可回绕出巨大 `block->size` ✅（`2d75e54`）

`src/mm/heap.c:157-163` `kmalloc` 有 `requested > 0xFFFFFFFFU - sizeof(struct heap_block)` 保护；`src/mm/heap.c:212-235` `krealloc` 在 `requested != 0` 后直接 `heap_pages_for_size(requested)`，没有该上限。

- `requested` 接近 `0xFFFFFFFF` 时，`heap_pages_for_size` 内部 `size + sizeof(struct heap_block)` 回绕，`new_pages` 可能很小；若 `new_pages <= block->pages`，走 `block->size = requested` 的收缩分支并返回原指针。此后 `copy_size = min(block->size, requested)` 可把 `requested` 当作有效大小，后续 `krealloc`/`memcpy` 会按远超实际分配的大小读写内核堆。
- 当前调用方可信，属于潜在内核堆溢出。建议：`krealloc` 入口复用 `kmalloc` 的上限判断，或让 `heap_pages_for_size` 返回 0/失败。
- 修复（`2d75e54`）：`krealloc` 在 `requested != 0` 分支后加与 `kmalloc` 相同的 `requested > 0xFFFFFFFFU - sizeof(struct heap_block)` 上限检查，超限返回 0。

### P1-4 ELF 装载把合法 `PT_LOAD` 接受到 3 GiB，但 `user_mm` 只映射低 1 GiB，段 `>=0x40000000` 会在切 CR3 后 memset 触发内核保护错误 ✅（`9c557b3`）

`src/os/elf.c:41-49` 用 `USER_SPACE_END`（`0xC0000000`）校验 `vaddr/memsz`；`src/mm/paging.c` 的 `paging_mm_map` 对 `virtual_addr >= USER_MM_SPACE_LIMIT`（`0x40000000`）直接拒绝映射，但 `elf32_load` 不检查每页 `paging_mm_map` 是否成功。

- 恶意/畸形 ELF 只要 `PT_LOAD.vaddr` 落在 `[0x40000000,0xC0000000)`，`image_start..image_end` 会包含未映射页；`elf32_load` 切到目标 CR3 后 `memset`（`src/os/elf.c:81-85`）以 CPL0 访问未映射用户页，`isr14` 判为非法内核访问并 `panic_blue_screen`。
- 跨 `0x40000000` 边界的镜像同样会在高半段 memset 时 panic。建议：ELF 校验与 `ring3_user_range_permitted` 统一使用 `USER_MM_SPACE_LIMIT`，并逐页确认 `paging_mm_map` 成功。
- 修复（`9c557b3`）：`PT_LOAD` 的 `vaddr/memsz` 与 `entry` 校验统一收敛到 `USER_MM_SPACE_LIMIT`（1 GiB）；逐页 `paging_mm_map` 后用 `paging_mm_get_phys` 确认映射成功，失败释放物理页并返回 -8。QEMU 实测：vaddr=`0x50000000` 的畸形 ELF 被 `run` 拒绝（"Unable to load or run"），内核存活无 panic。

### P1-5 FAT32/ATA 全局缓冲与 fd 表非重入，IRQ0 可在 kernel shell FAT 操作中途切到 Ring3 进程再进入 FAT32 ⬜

`src/fs/fat32_core.c:6-8`：`g_sector/g_cluster/g_fs` 是全局静态缓冲；`src/os/syscall.c:108-116` `sysfs_fds` 也是全局 fd 表；`g_cwd`/FAT32 路径状态同样全局共享。

- 用户态 FAT32 syscall 期间，IRQ0 打断的是内核态 syscall 代码，其 IRQ0 `frame->cs` 为内核 CS，`ring3_timer_switch` 会原地返回、不会切走；但 **kernel shell / supervisor 上下文**（`current_process < 0`）执行 `cmd_ls/cat/cp/run/disk` 时，IRQ0 可以切到 READY 的 Ring3 进程。该进程随后可通过 syscall 再进入 FAT32，覆盖 `g_sector/g_cluster/g_fs/g_cwd/sysfs_fds`；回到被抢占的 kernel shell 后继续使用已被覆盖的簇缓冲/目录状态。
- 触发条件不是理论性的：vt0 回退到 ring0 shell 时，vt1 的 shell3 仍可能 READY/RUNNING；或 supervisor 空闲时其它 vt 有活动进程。`vt_spawn_shell` 已特意关中断规避同类问题（`src/drivers/vt.c:149-158`），但 kernel shell 的 FAT32 命令没有等价保护。
- 建议：给 FAT32/ATA 操作加“非重入”保护（如全局 FS 锁 + IRQ 关闭临界区），或把 `g_sector/g_cluster` 改为调用方提供的缓冲；至少在 kernel shell 的 FAT32 命令入口保存/恢复共享状态并关中断。

---

## P2（隐患）

### P2-1 `IO_READ/IO_WRITE` 只接受“已映射”页，未 demand-map 的用户栈页会被拒绝 ⬜

`src/os/syscall.c:291-332`：`sys_user_buffer_ok/sys_copy_user_string` 用 `paging_mm_get_phys` 实际映射检查，而不是 range permission。Ring3 栈只预映射顶页（`src/os/ring3.c` schedule 路径），更低栈页靠 `isr14` demand-map。结果：指向合法但尚未触碰的栈缓冲区会返回 `-1`，而不是先 fault-in。

建议：要么文档化“syscall buffer 必须已提交”，要么在 buffer 校验中允许 `ring3_user_range_permitted` 的栈/镜像页并由复制路径处理缺页（注意不能在内核态 memcpy 缺页）。

### P2-2 未注册异常仍 `cli/hlt`，不走 `panic_blue_screen` ⬜

`src/arch/x86/idt.c:9-15`：默认 handler 对未单独实现的异常/IRQ 直接停机；`isr14` 非法内核访问会 panic，但 `#GP/#UD/#NM` 等仍静默挂起。调试时难以区分“死机”与“panic”。

建议：默认 handler 至少 `panic_blue_screen`（或记录寄存器后 panic），保持 boot/IDT 契约测试同步。

### P2-3 `isr14` demand-map 对整个用户堆区一律 RW 映射，掩盖 UAF/野指针 ⬜

`src/os/trap.c`/`src/mm/paging.c`：`sys_user_heap_contains` 命中 `0x1000000..0x40000000` 即分配并映射 RW 用户页，不区分该 VA 是否由 `user_alloc` 分配、是否属于当前 block、是否应为只读。后果：

- 对已释放/未分配堆 VA 的访问不会 fault，破坏 `free=1` 语义；
- 读错误用 `MEMORY_*` 前先 fault-in 的零页会通过 `sys_user_buffer_ok`，把“未提交”变成“已映射”；
- 与 P1-1 叠加，跨进程 free list 解引用会静默建零页。

建议：demand-map 至少区分 stack/image/heap subrange，或记录“已提交堆页”位图，仅允许已分配页 fault-in。

### P2-4 `paging_map_page`/`paging_mm_map` 无条件覆盖已有 PTE，且不拒绝 2 MiB 大页 ⬜

`src/mm/paging.c`：内核 `paging_map_page` 与用户 `paging_mm_map` 都直接写 `pt->entries[pt_index]`，不检查旧 PTE；当 PDE 已是 `PAE_PAGE_SIZE` 大页时，`paging_map_page` 会把 PDE 值当 PT 物理地址写 PTE，破坏映射。当前多数调用点依赖“新地址未映射”约定，缺少防御。

建议：映射前检查 `paging_mm_get_phys/paging_get_page_physical`；对已存在映射返回错误或要求先 unmap；对大页 PDE 拒绝或先 split。

### P2-5 全局 `sysfs_fds` 不回收、`FS_READDIR` 4 KiB staging 截断、`FS_READ/FS_WRITE` 每次整文件重读 ⬜

`src/os/syscall.c:108-116,334-453`：

- fd 表只有 8 项且进程退出不清理（`release_process_resources` 未见关闭 fd）；fd 号在进程间共享，PID 复用/多进程会看到彼此 fd。
- `FS_READDIR` 用固定 `kmalloc(4096)` staging，目录列表超长时 `sysfs_readdir_cb` 静默丢弃尾部记录（`ctx->len+need>cap` 直接 return），调用方无法区分“目录结束”与“被截断”。
- `sysfs_fread` 每次 `FS_READ` 都 `fat32_read_file` 整文件到 1 MiB 内核缓冲再拷贝 chunk；`sysfs_fwrite` 每次都整文件读出-拼接-整文件写回。chunked I/O 是 O(file_size) 每次，`cat`/`write` 大文件退化为 O(n²) 磁盘 IO。

建议：fd 表挂到 `user_mm`/进程并在 release 时关闭；readdir 返回“是否截断”；read/write 维护文件偏移的流式路径或缓存簇链。

### P2-6 FAT32 move/delete 仍先释放簇链再删目录项，失败会留下悬空目录项 ⬜

`src/fs/fat32.c`/`src/fs/fat32_dir.c`：`fat32_move` 删除目标项后再插入源项；`fat32_delete_file`/`fat32_remove_directory_internal` 先 `free_cluster_chain` 再 `mark_entry_*_deleted`。若目录项更新失败（FAT 写错、目录满、断电窗口），目录项仍指向已释放簇，后续访问得到 BAD/FREE 链。

N3 已修“覆盖写先建新链后放旧链”，但 delete/move 的顺序仍是非事务性的。建议：删除路径改为“先标记删除/改名，成功后释放簇链”，或失败时回滚 FAT 项。

### P2-7 `allocate_cluster` 对每个候选簇都重读 FAT，整体 O(n²) ⬜

`src/fs/fat32_core.c:86-100`：线性扫描 + 每簇 `fat_next_cluster`（读一个 FAT 扇区）+ `fat_set_cluster`（再读写 FAT/备份）。大 FAT32 卷分配/扩容会非常慢。

建议：缓存 FAT 空闲提示/按 FAT sector 缓存扫描，避免每簇重复读盘。

### P2-8 `paging_mm_create` 对低 4 MiB borrowed 页建立失败时静默 `continue`，返回不完整 mm ⬜

`src/mm/paging.c`：创建用户 mm 时复制低身份页为 borrowed；`mm_ensure_pt` 失败只 `continue`，最终仍返回 `mm`。结果 mm 缺低半映射但 `in_use=1`，后续 `elf32_load/ring3_schedule` 可能在缺页地址上跑，或把缺页误判为“镜像未占用”。

建议：任一必要 pt 分配失败应释放已分配页表并返回 NULL，或至少记录失败位。

### P2-9 `vt_alloc_fb` 中途 `pmm_alloc_page` 失败会泄漏已映射页，且 `fb_ready` 仍可能置位 ⬜

`src/drivers/vt.c:262-278`：循环映射 backing store 时，失败直接 `return -1`，已映射的物理页既未记录也未回收；`vts[vt].fb_ready` 只在全部成功后置 1，但部分映射页仍留在页表中成为无主内存。

建议：失败路径按已映射页数 unmap+free；或先按 `pages` 预留/提交成功再置 `fb_ready`。

### P2-10 扫描码集 2 的修饰/锁定键释放处理顺序错误 ⬜

`src/drivers/keyboard.c:196-264`：`0xF0` 只置 `release=1` 后 return；下一字节若是 Shift `0x2A/0x36`、Caps `0x3A`、Num `0x45`、Ctrl `0x14`，会先命中“按下/切换”分支并 `return`，根本走不到 `current_scancode_set==2 && release` 的丢弃逻辑。结果：set2 下松开 Shift 会把 `shift_pressed` 再置 1（卡住），Caps/Num Lock 断码会再次翻转。

默认 set1 不受影响；启用 `SCANCODE_SET=2` 时输入状态损坏。建议：在处理任何 set2 断码前先检查 `release` 前缀并消费整组断码。

### P2-11 `sysfs_fwrite` 的 `fd->offset` 与全局文件大小语义在多进程/多 fd 下不一致 ⬜

`src/os/syscall.c:422-453`：`FD` 只存 `offset/size/path`，`FS_WRITE` 永远“读整文件-按 offset 拼接-写整文件”。两个 fd 指向同一文件、或文件被其它进程改写后，`old_size/needed` 会按当前文件而非打开时快照计算，可能覆盖并发写入；`fd->size` 在 `FS_READ` 后被更新但 `FS_WRITE` 不用它判断 EOF/截断。

与 P2-5 的全局 fd 表叠加，属于一致性风险。建议：fd 增加 open 时 size/flags 快照或改为真正的文件偏移写路径。

---

## P3（风格/次要健壮性）

1. `src/drivers/serial.c:44-59`：COM3 `serial3_putchar('\n')` 分两次等 THR，第一次成功第二次超时时会留下孤立 `\r`；影响仅是输出美观。
2. `src/lib/stdio.c`：`sprintf` 无界写入调用方 buffer；`int_to_str/int_to_str_padded` 对 `INT_MIN` 的 `-value` 有 UB 风险；内核 `gets` 走 `keyboard_wait_key` 不经过 `sys_keyboard_wait_yield` 的让出路径（仅 kernel shell 使用）。
3. `src/drivers/keyboard.c`：`alt_pressed` 声明后未使用；set1 `apply_modifiers` 对 Caps+Shift 仍输出大写；`keyboard_send_command_with_response` 未使用（编译警告）。
4. `src/shell/shell.c`/`shell_ring3.c`：`split_line` 未闭合引号按字面量处理（与注释一致但仍是边界）；`parse_pid`/PID 解析在 `shell.c` 有溢出保护而 `shell_ring3.c:157-166` 的 `parse_pid` 未做 `pid*10` 溢出检查；`shell_ring3 cmd_bg` 不校验 pid 是否存在/是否真为后台，`bg <bad>` 也打印成功。
5. `src/fs/fat32_name.c`：LFN 只支持 ASCII，非 ASCII UTF-16 会折叠成 `?`，可能造成不同长名映射到同一短名/同一显示名。
6. `src/fs/fat32.c`：`fat32_read_file` 在 buffer 不足时返回成功并把 `*size` 截断为实际复制长度；调用方若不比较 size 与文件长度会静默截断（`cmd_cat` 64 KiB buffer 即如此）。
7. `src/drivers/vbe.c:125-133`：`vbe_map_framebuffer` 按 `pitch*height` 映射到 `0xE0000000`，无上限/重叠检查；极端模式可能覆盖 `0xE8000000` VT backing 或 `0xF0000000` KMAP。`pitch*height` 也可能 32-bit 回绕。
8. `src/drivers/vt.c`：`vt_fb_stride` 计算失败只打印日志，已 spawn 的 vt 仍会置 `switch_enabled`，F 键请求会被接受但 `vt_do_switch` 因 backing 缺失拒绝，用户只看到 serial 日志。
9. `src/mm/paging.c`：`paging_try_reclaim_page_table` 回收页表后只 `flush_tlb(0)`，未按需刷其它地址；`paging_mm_unmap` 缺 `USER_MM_SPACE_LIMIT` 检查（与 P1-1 相关）。
10. `src/mm/pmm.c`：`pmm_get_free_frames` 每次 O(位图全扫)，`g_pmm.used_frames` 维护但未被查询路径使用；高位 `pmm_alloc_page_at_or_above` 只在测试宏路径验证。
11. `src/drivers/acpi.c`：已全量重读。RSDP/FADT/DSDT 定位依赖固件表长度与签名，AML `_S5` 解析是启发式且未做完整 AML 边界校验；reset/shutdown 走多级 fallback，失败最终 `hlt`。固件可信前提下可接受，建议后续加表长度/checksum 复核。
12. `src/drivers/rtc.c`：`rtc_read_time` 未校验 month/day 范围；`rtc_convert_timezone` 对极大 `offset_seconds` 会长循环；`rtc_to_unix_timestamp` 在 RTC 年份 <1970 时结果不可靠。
13. `src/os/panic.c`/`panic_blue_screen`：倒计时会 `sti` 等 PIT；若 panic 发生在 `current_process<0` 且仍有 READY Ring3 进程，IRQ0 理论上可切走显示 panic 的上下文。边界场景，建议 panic 路径关中断 busy-wait 或屏蔽调度。
14. `boot/*.asm`/`gdt/idt`：32-bit protected mode 下 TSS 描述符为 8 字节，DF TSS(0x30) 与普通 TSS(0x28) 不重叠；`regs.h` 与 `interrupt.asm` 栈帧契约有 host 侧测试覆盖。无新增 P0-P2。

---

## 已复核为已修复/不再成立的历史项

以下条目本轮对照当前源码确认状态，不再按未修复重复计入：

- N1 VT framebuffer 与内核堆重叠：已修，`VT_FB_BASE=0xE8000000` 且有 `#error` 约束（`src/drivers/vt.c:17-32`）。✅
- N2 `heap_split` 截断 free list：已修，remainder 走 `heap_insert_free` 有序重插（`src/mm/heap.c:100-118`）。✅
- N3 FAT32 覆盖写先放旧链：已修，`fat32_write_file` 先写新链/更新目录项再释放旧链（`src/fs/fat32.c`）。✅
- `IO_WRITE/IO_READ` 用户指针：已修，`sys_copy_user_string`/`sys_user_buffer_ok` 有界校验（`src/os/syscall.c:291-332,468-499`）。✅
- `user_realloc`/`krealloc` 的“完全无校验”：`kfree/krealloc` 已加范围+magic+pages+free 检查（`src/mm/heap.c:176-195,222-233`），`krealloc` 已补 size 上限（`2d75e54`）；但 `user_realloc` 仍缺入口范围检查，见 P1-2。⬜（部分）
- VT spawn 失败/重试/vt0 回退：已实现 `VT_SPAWN_RETRY`、重试上限、永久失败关 F 键、vt0 回退 ring0 shell（`src/drivers/vt.c`）。✅
- shell3 `cmd_write` 先 unlink 后 open：已修为先 open 再 unlink（`src/shell/shell_ring3.c:807-820`）。✅
- serial TX timeout 仍写：已修，超时丢弃并只告警一次（`src/drivers/serial.c:24-59`）。✅
- `ring3_schedule_mm` mm 双重释放：已修，schedule 接管所有权，失败路径已释放（`src/os/ring3.c`/`src/os/elf.c:129-135`）。✅
- `sys_keyboard_wait_yield` 饿死 `vt_service`：已修，无条件让出到 kernel idle frame（`src/os/syscall.c:56-67`）。✅
- COM1/COM3 职责混用：COM1 诊断/kernel shell，COM3 镜像 `IO_WRITE`，保持分离（`src/drivers/serial.c`/`syscall.c:485-487`）。✅

---

## 建议修复顺序

1. **P1-4 ELF 上限**：把 ELF 段校验收敛到 `USER_MM_SPACE_LIMIT` 并逐页检查 `paging_mm_map`，否则畸形镜像可稳定 panic。
2. **P1-1/P1-2 用户堆**：user heap 元数据按 mm 隔离 + `user_free/user_realloc` 完整范围/pages 校验 + `paging_mm_unmap` 上限检查。
3. ~~**P1-3 `krealloc` size 上限**~~ ✅（`2d75e54`）
4. **P1-5 FAT32/ATA 重入**：为 kernel shell 与 Ring3 并发 FS 加临界区/缓冲隔离。
5. **P2-1/P2-3 syscall buffer 与 demand-map**：明确“已提交页”语义，避免栈页误拒与堆 UAF 被掩盖。
6. **P2-4 页表覆盖**：`paging_map_page/paging_mm_map` 增加冲突检测。
7. **P2-5/P2-11 fd/FS 语义**：fd 随进程回收，readdir/report 截断，read/write 偏移语义。
8. **P2-6 FAT32 事务顺序**：delete/move 改为“先提交目录项、后释放簇链”。
9. **P2-7/P2-8/P2-9/P2-10**：FAT 分配性能、mm 创建失败原子性、VT backing 泄漏、set2 键盘状态。
10. **P3**：按上表逐项清理；ACPI/RTC 固件解析加固可随后续硬件兼容工作一起做。
