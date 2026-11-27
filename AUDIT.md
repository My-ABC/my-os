# MyOS 代码审计报告（P0–P3）

- 审计基线：第二轮 @ `9dee583`（第一轮基线 `feature/vt-f1-f2` @ `b1989bf`）
- 范围：`src/`、`include/`、`boot/` 全量重读（约 1.14 万行：boot 汇编入口、全部头文件、内核/内存/中断/Ring3/syscall/ELF/FAT32/驱动/串口/ACPI/VBE/VGA/VT/双 shell）；第二轮新增 `mini-libc/` 全量（约 2.6 千行：stdlib/stdio/fs/process/signal/time/string/setjmp/syscall.S/crt0.S 及全部头文件）与 `test/` 中 ELF 生成路径核对
- 方法：按 `.github/agents/my-os-kernel.agent.md` 的架构约束逐文件核对；对照 `regs.h`/`interrupt.asm`/`idt.c` 栈帧契约、VT/串口/KMAP/PDE 复用/内存所有权/syscall 用户指针边界逐项验证；第二轮复核 P1-3/P1-4 修复正确性，并对照 `elf_demo.asm`/`shell3.elf` 程序头验证 ELF 装载页权限路径
- 验证：两轮均为文档审计，未改代码；第一轮已运行 `make build`（通过）与 `make test`（45 项通过）。未运行 `make test-qemu`

## 严重度定义

- **P0 必坏**：现有正常路径必然损坏/死机。
- **P1 可能坏**：特定但合法/可达输入会损坏、泄漏内核信息或 panic。
- **P2 隐患**：正确性/一致性/可维护性风险，当前测试未覆盖或需要边界条件触发。
- **P3 风格**：健壮性、命名、注释、非关键边界。

状态：✅ 已修复 / ⬜ 未修复。

---

## P0

### P0-1 yield 重启把中止返回值写进 frame eax 槽，`int 0x80` 以错误 class 重触发，阻塞输入进程必然报错刷屏 ✅（`8a6cf95`，调试期发现非审计条目）

`src/os/syscall.c` `sys_keyboard_wait_yield` 原把 `frame->eip` 无条件回退 2 字节使 `int 0x80` 重启，但 `boot/interrupt.asm` 的 syscall 桩在 `syscall_switch_frame` 分流**之前**总执行 `mov [esp+44], eax`，把 handler 返回值（-1/0）写进保存帧的 eax 槽。进程恢复后 eax=返回值而非 SYS_* class，重执行的 `int 0x80` 走 default 立即返回——`IO_READ`/`IO_GETCHAR` 每次 yield 恢复都得到一次假返回，用户态进入"阻塞→yield→假 EOF→报错→再阻塞"循环（实测 guess_number 每 timer tick 一条 "Invalid input"）。且 yield 失败时 eip 已回退，后续正常返回也会多触发一次 `int 0x80`。

修复：yield 成功后才回退 eip；`sys_gets` 区分 yield(2)/信号中止(1)；IO_READ/IO_GETCHAR 在 yield 路径返回 `klass`，使重启的 `int 0x80` 以原 class+op+args 重新分发；yield 仅在 `i==0` 时进行，避免行中重启丢已消费字符。

### P0-2 `sys_gets` 在 fatal 信号终止后仍写用户 buf，内核 CR3 下用户 VA 写 fault → 误报 "Out of physical memory" panic ✅（`8a6cf95`，调试期发现非审计条目）

`sys_gets` 的 `sys_signal_check`→`sys_signal_fatal_abort`→`ring3_terminate_current` 已把 CR3 切到内核页表、释放进程 mm、`current_process=-1`，随后代码继续执行 `buf[i]='\0'`——在内核 CR3 下写用户 VA（如 mini-libc `in_buf`/`.bss`），触发 supervisor 写未映射页 fault（error=0x2）。isr14 中 `sys_user_heap_contains` 是纯地址范围检查（`0x01000000`–`0x40000000`，覆盖镜像地址），判 permitted=1；`isr14_demand_map` 因 `ring3_current_mm()==NULL` 失败，走到 `panic_blue_screen("Out of physical memory")`——文案误导，实际是 CR3 状态错误。任何阻塞在 `IO_READ`/`IO_GETCHAR`/`sys_gets` 时收到致命信号（Ctrl+C 自投 SIGINT、SIGKILL）必现。

修复：`syscall_exit_frame != 0`（fatal abort 已切内核 CR3）时跳过 `buf` 写入；同时在 agent.md 记录"terminate 后禁止触碰用户内存"约束。

---

## P1（可能坏）

### P1-1 用户堆元数据全局共享，但虚拟地址空间是每进程私有的 ✅（`b101c39`）

`src/os/syscall.c:126-285`：`user_heap_next` 与 `user_free_list` 是全局变量，而 `user_alloc/user_free/user_realloc` 使用的是 `ring3_current_mm()` 的私有 `user_mm`。

- 进程 A `MEMORY_FREE` 后，`user_free_list` 指向 A 地址空间中的 `user_block`；进程 B `MEMORY_MALLOC` 复用时，`user_find_free/user_restore_pages` 在 B 的 CR3 下解引用该用户虚拟地址。若 B 未映射同一 VA，会被 `isr14` demand-map 成全零页（`src/os/trap.c`），读到 `pages=0,next=0`；若 B 恰好映射了同 VA，则读到无关数据。
- `user_heap_next` 全局递增使各进程堆 VA 不复用，浪费 1 GiB 用户地址空间；但真正危险是 free list 跨 mm。
- `user_free` 只校验 `magic/free`，不校验 `block->pages` 上界、`address+pages*PAGE_SIZE<=USER_HEAP_END`，也不校验页是否属于该 block；`paging_mm_unmap`（`src/mm/paging.c`）未像 `paging_mm_get_phys` 那样检查 `USER_MM_SPACE_LIMIT`，对 `>=0x40000000` 的地址会按 `&0x1FF` 回绕到 pd0/pt0，可能误清低地址映射。

- 修复（`b101c39`）：`struct user_mm` 新增 `user_heap_next`/`user_free_list`（`user_block` 前置声明保持 opaque），`paging_mm_create` 按 mm 初始化；`user_alloc`/`user_find_free`/`user_remove_free`/`user_restore_pages`/`user_free`/`user_realloc` 全部改为显式传 `mm`（SYS_MEMORY 分发处取一次）；`user_free` 增加 `block->pages==0 || block+pages*PAGE_SIZE>USER_HEAP_END` 上界拒绝。QEMU 新增 `heap.elf` 双 vt 并发场景验证隔离。`paging_mm_unmap` 的 `USER_MM_SPACE_LIMIT` 检查未动（遗留）。

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
2. `src/lib/stdio.c`：`sprintf` 无界写入调用方 buffer；`int_to_str/int_to_str_padded` 对 `INT_MIN` 的 `-value` 有 UB 风险 ✅（INT_MIN 改无符号取负；sprintf 无界写入为内核内部约定，保留）；内核 `gets` 走 `keyboard_wait_key` 不经过 `sys_keyboard_wait_yield` 的让出路径（仅 kernel shell 使用）。
3. `src/drivers/keyboard.c`：`alt_pressed` 声明后未使用；set1 `apply_modifiers` 对 Caps+Shift 仍输出大写；`keyboard_send_command_with_response` 未使用（编译警告）。✅（已删 alt_pressed/未用函数，Caps+Shift 改为 XOR）
4. `src/shell/shell.c`/`shell_ring3.c`：`split_line` 未闭合引号按字面量处理（与注释一致但仍是边界）；`parse_pid`/PID 解析在 `shell.c` 有溢出保护而 `shell_ring3.c:157-166` 的 `parse_pid` 未做 `pid*10` 溢出检查 ✅（已补上界检查）；`shell_ring3 cmd_bg` 不校验 pid 是否存在/是否真为后台，`bg <bad>` 也打印成功。
5. `src/fs/fat32_name.c`：LFN 只支持 ASCII，非 ASCII UTF-16 会折叠成 `?`，可能造成不同长名映射到同一短名/同一显示名。
6. `src/fs/fat32.c`：`fat32_read_file` 在 buffer 不足时返回成功并把 `*size` 截断为实际复制长度；调用方若不比较 size 与文件长度会静默截断（`cmd_cat` 64 KiB buffer 即如此）。
7. `src/drivers/vbe.c:125-133`：`vbe_map_framebuffer` 按 `pitch*height` 映射到 `0xE0000000`，无上限/重叠检查；极端模式可能覆盖 `0xE8000000` VT backing 或 `0xF0000000` KMAP。`pitch*height` 也可能 32-bit 回绕。
8. `src/drivers/vt.c`：`vt_fb_stride` 计算失败只打印日志，已 spawn 的 vt 仍会置 `switch_enabled`，F 键请求会被接受但 `vt_do_switch` 因 backing 缺失拒绝，用户只看到 serial 日志。
9. `src/mm/paging.c`：`paging_try_reclaim_page_table` 回收页表后只 `flush_tlb(0)`，未按需刷其它地址；`paging_mm_unmap` 缺 `USER_MM_SPACE_LIMIT` 检查（与 P1-1 相关）。✅（unmap 已补上限检查；reclaim TLB 项保留观察）
10. `src/mm/pmm.c`：`pmm_get_free_frames` 每次 O(位图全扫)，`g_pmm.used_frames` 维护但未被查询路径使用；高位 `pmm_alloc_page_at_or_above` 只在测试宏路径验证。
11. `src/drivers/acpi.c`：已全量重读。RSDP/FADT/DSDT 定位依赖固件表长度与签名，AML `_S5` 解析是启发式且未做完整 AML 边界校验；reset/shutdown 走多级 fallback，失败最终 `hlt`。固件可信前提下可接受，建议后续加表长度/checksum 复核。
12. `src/drivers/rtc.c`：`rtc_read_time` 未校验 month/day 范围；`rtc_convert_timezone` 对极大 `offset_seconds` 会长循环；`rtc_to_unix_timestamp` 在 RTC 年份 <1970 时结果不可靠。
13. `src/os/panic.c`/`panic_blue_screen`：倒计时会 `sti` 等 PIT；若 panic 发生在 `current_process<0` 且仍有 READY Ring3 进程，IRQ0 理论上可切走显示 panic 的上下文。边界场景，建议 panic 路径关中断 busy-wait 或屏蔽调度。✅（panic_in_progress 置位后 ring3_timer_switch 不再切换）
14. `boot/*.asm`/`gdt/idt`：32-bit protected mode 下 TSS 描述符为 8 字节，DF TSS(0x30) 与普通 TSS(0x28) 不重叠；`regs.h` 与 `interrupt.asm` 栈帧契约有 host 侧测试覆盖。无新增 P0-P2。

> **测试发现（非审计条目）**：为 P3 补 host 测试时抓到真 bug——`src/lib/stdio.c` `int_to_str` 对负数 `str++` 跳过 `'-'` 后 `return p - str` 少算符号位，`sprintf("%d", -42)` 输出 `-4`（INT_MIN 输出 `-214748364`）。`int_to_str_padded` 的 `is_negative` 单独计长不受影响。修复：`negative` 标志补回返回长度（`0ed2c07`）。既有测试无任何负数 `%d` 用例是漏网原因；现已补 -1/-9/-10/-99/-100/INT_MIN+1 边界与 padded 全内容断言。

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

---

## 第二轮复审新增条目（mini-libc 首次覆盖 + 关键路径复核）

第二轮重读 `mini-libc/`（第一轮范围外）、复核 `9c557b3`/`2d75e54` 两处修复、并对照 `boot/elf_demo.asm` 与 `build/shell3.elf` 的程序头验证 ELF 装载路径。编号沿用 R2-Nx。

### P2

#### R2-N1 mini-libc `xalloc` 的 `ksize` 32-bit 回绕导致分配不足、后续溢出 ⬜

`mini-libc/src/stdlib.c:48-65`：`ksize = size + MH_HDR + align + 8` 在 32-bit `size_t` 上回绕。`malloc(0xFFFFFF00)` 之类的巨大 size 会得到一个很小的 `ksize`，内核只分配一页，但头部 `size` 字段记录原始大值；随后 `calloc` 的 `memset(p,0,total)`、`realloc` 的 `memcpy(np, ptr, min(oldsize,size))` 都按大值读写，必然打爆本进程用户堆（自伤，但可稳定复现崩溃）。`calloc` 的 `nmemb*size` 溢出检查拦不住 `calloc(1, huge)` 这条路径。

建议：`xalloc` 入口加 `size > UINT32_MAX - (MH_HDR + align + 8)` 上限检查并返回 ENOMEM。

#### R2-N2 `elf_run_vt` 的 -7（mm 耗尽）被当作永久 spawn 失败，瞬时内存压力会永久禁用 vt ⬜

`src/kernel.c` `shell3_spawn_on_vt`：只有 `rc == -8`（进程表满）映射为 `VT_SPAWN_RETRY`；`paging_mm_create` 失败时 `elf_run_vt` 返回 -7，被当作 ELF 装载错误 → `vt_latch_spawn_failed` 永久关 F 键。`USER_MM_MAX=8` 的 mm 槽在进程密集/泄漏场景下会耗尽，属于可恢复资源不足，却走了永久失败路径。同函数还有次级问题：`fat32_read_file` 对超 `FAT32_MAX_FILE_SIZE` 的文件截断返回 0（P3-6），`elf_run_vt` 会拿截断镜像解析，大概率 -2 永久 latch。

建议：-7 也映射为 RETRY；spawn 前先 `fat32_stat` 校验 size ≤ FAT32_MAX_FILE_SIZE。

#### R2-N3 `elf32_load` 的段复制依赖 CR0.WP=0，脆弱且未文档化 ⬜

`src/os/elf.c`：按页计算 `PAGE_WRITABLE`（仅 PF_W 段可写），随后切到目标 CR3 以 CPL0 `memcpy` 段内容。对只读 PT_LOAD（`elf_demo.asm` flags=5、`shell3.elf` 的 R-E 段）写入的是无 WRITABLE 的用户页——当前不 panic 只是因为 `boot/boot.asm`/`paging.asm` 只置 PG、CR0.WP=0，supervisor 绕过写保护。一旦将来开启 WP（或在 WP=1 的测试环境），所有含只读段的正常 ELF 都会在装载期触发 "Kernel protection fault" panic。

建议：要么在 `paging_mm_map` 后用 KMAP/物理页写入段数据（不切 CR3），要么显式注释"依赖 WP=0"并加测试；更稳妥是临时把目标页 PTE 置 WRITABLE、拷贝完清除。

### P3

1. `mini-libc/src/stdio.c` `fgets`：`size==1` 时循环不执行，`c` 未初始化却参与 `i==0 && c==EOF` 判断（UB 读栈值）。
2. `mini-libc/src/ctype.c` `iscntrl`：`c < 32` 漏掉 DEL(127)，`iscntrl(0x7F)` 应为真。
3. `mini-libc/src/time.c` `strftime`：`tm_wday`/`tm_mon` 未校验即索引 `wday_names[8]`/`mon_names[12]`，调用方构造越界 `struct tm` 会造成用户态 OOB 读。
4. `mini-libc/src/stdio.c` `fread` 与 `fs.c` `fs_fread/fs_fwrite`：`size * count` 为 32-bit `size_t`/`unsigned`，大参数回绕后 `total` 偏小，返回值与实际读写不一致。
5. `src/drivers/ata.c`：`ata_read_sectors`/`ata_write_sectors` 发命令前只做 `ata_wait_400ns`（4 次读），不等 BSY 清零；前一次操作未完成时寄存器写入可能被丢弃。建议命令前补 `ata_wait_bsy_clear`。
6. `src/fs/fat32_core.c` `fat32_init`：`g_sector[16] != 2` 拒绝单 FAT 卷；`total_sectors==0` 时不回退读 BPB 19-20 的 16-bit 字段（`fat32_image.py` 已做此回退，内核没有），部分合法 FAT32 无法挂载。
7. `src/os/ring3.c` `ring3_kill`：对 `current_process` 自身置 DEAD 后不切帧，进程要等到 IRQ0 才被回收（窗口内仍可执行）；`ring3_signal_scan`/`ring3_terminate_current` 路径已处理，仅 kill-self 路径有此小窗口。
8. `mini-libc` `xalloc` 的 malloc 头（base/magic/align/size）位于用户可写内存，用户可伪造头让 `free()` 把任意 `base` 传给 `MEMORY_FREE`；内核 `user_free` 有 magic+范围校验，实际只能命中自己进程堆，记为设计说明而非缺陷。
9. `boot/boot.asm`/`boot/paging.asm`：启用分页时未置 CR0.WP/WP 相关位之外的 CR0 保留位检查；同时 WP=0 使内核可写只读页（含 rodata），属加固建议（见 R2-N3）。

### 复核结论（本轮确认无新问题）

- `krealloc` 上限修复（`2d75e54`）与 grow-merge/shrink 分支的 remainder 处理正确；`heap_split`/`heap_coalesce`/`heap_insert_free` 有序重插无回归。
- `paging_mm_create` 的借出低 4 MiB 复制（bit9 BORROWED）与 `paging_mm_release` 的借出跳过逻辑一致；`mm_ensure_pt` 失败静默 continue 仍是 P2-8。
- `elf32_load` 的 `USER_MM_SPACE_LIMIT` 收敛、逐页 `paging_mm_map`+`get_phys` 校验（`9c557b3`）正确；失败路径由 `elf_run_vt` 统一 `paging_mm_release`，无泄漏。
- `trap.c` isr14 的 demand-map/protection-kill 分支与 `err_code` 位语义核对无误；`isr14_demand_map` 只在 `ring3_current_mm()` 存在时映射。
- `ring3.c` 信号投递（`sig_deliver`/`sig_actionable`/`sigreturn`）、`ring3_foreground_release` 父进程回退、`ring3_schedule_internal` 的镜像/栈范围校验与 mm 所有权转移均与注释一致。
- `syscall.c` 的 `sys_user_buffer_ok`/`sys_copy_user_string` 映射级校验、`sysfs_fread/fwrite` 的 uint64 边界、`FS_READDIR` staging、`PROC_LIST` 12 字节记录与 mini-libc `proc_info_t` 布局一致。
- `fat32_dir.c`/`fat32_name.c`：LFN 序列校验（order/checksum/0xA0 掩码）、`find_free_directory_slots` 的 end-marker 语义、`make_short_alias` 的 `~N` 生成与 `directory_chain_contains` 的 `..` 回溯均正确；P2-6 的 delete/move 顺序问题仍在。
- `mini-libc` `syscall*.S` 的栈参数偏移、`setjmp.S` 的 esp/eip 保存、`crt0.S` 的 argc/argv/envp 帧与内核 ABI 一致；`strtol/strtoul` 溢出、`qsort` 递归深度（迭代大侧）无问题。
- `keyboard.c` set2 断码顺序问题（P2-10）本轮复核属实，默认 set1 不受影响。

---

## 第三轮复审新增条目（syscall.c / mini-libc / syscall ABI 边界）

第三轮重读 `src/os/syscall.c`、`include/os/syscall.h` 与 `mini-libc/` 全部源码（stdlib/stdio/fs/process/signal/time/unistd/console/power/ctype/string/inttypes/assert、全部头文件、`syscall.S`/`setjmp.S`/`crt0.S`），并对照 `src/mm/paging.c`、`src/os/ring3.c`、`src/os/elf.c`、`boot/user.asm`、`linker.ld` 验证页权限与调度契约。编号沿用 R3-Nx。

### P1

#### R3-N1 `sys_user_buffer_ok`/`sys_copy_user_string` 只查 `PAE_PRESENT`，借出低 4 MiB supervisor 恒等映射成为任意物理页读写原语 ✅（`277ca70`）

`src/os/syscall.c:321-357` 的 `sys_user_page_mapped` 调 `paging_mm_get_phys`，后者（`src/mm/paging.c:355-374`）只检查 PTE `PAE_PRESENT` 位，不检查 `PAE_USER`/`PAE_WRITABLE`。而 `paging_mm_create`（`src/mm/paging.c:277-288`）把内核 `pae_low_pt` 的低 4 MiB **supervisor-only 恒等映射**（`src/mm/paging.c:471-475`，无 `PAE_USER`）复制进每个 user_mm 的 VA `0x00000000–0x003FFFFF`。

- 该范围内 VA = 物理地址：PMM 只把 `<1 MiB`、内核镜像与 multiboot 结构标为已用（`src/mm/pmm.c:161-174`），`kernel_end+2 页` 到 4 MiB 的物理页可被 `pmm_alloc_page` 分配给**任意进程的堆/栈/页表/信号页**。用户态碰不到这些页（无 USER 位），但 syscall 校验放行后内核以 supervisor 身份直接 memcpy。
- 写方向：`IO_READ`(sys_gets)、`FS_READ`、`FS_READDIR`、`FS_GETCWD`、`TIME_TO`、`PROC_LIST` 的目标 buffer，以及 `FS_WRITE` 的**源** buffer（内核把物理页内容读出写进文件 → 盘上数据外泄）。
- 读方向：`IO_WRITE` 与所有 `sysfs_resolve_path` 路径参数经 `sys_copy_user_string` 读取 → `IO_WRITE(0x200000)` 把任意物理页内容打印到 VGA+COM3。
- 任一 Ring 3 进程用裸 `int 0x80`（无需 mini-libc）即可触发：`sys_call(SYS_IO, IO_READ, 0x200000, 512)`。

影响：跨进程内存破坏/信息泄漏的确定性原语，且 WP=0 使对只读用户页（ELF text/rodata）的写入也静默成功（应返回 EFAULT 而非写穿）。建议：新增按 access mode 校验的 helper——读要求 `PAE_USER`，写要求 `PAE_USER|PAE_WRITABLE`；`sys_user_buffer_ok` 增加 `writable` 参数，所有 memcpy 目的端传 1、源端传 0；`sys_copy_user_string` 只要求 USER。
- 修复（`277ca70`）：`paging_mm_get_pte` 暴露原始 PTE；`sys_user_page_ok` 要求 `PAE_PRESENT|PAE_USER`（写路径加 `PAE_WRITABLE`），替换原 `sys_user_page_mapped`；`sys_user_buffer_ok` 增加 `writable` 参数。调用点：写方向 `IO_READ`/`sysfs_fread`/`TIME_TO`/`PROC_LIST`/`FS_GETCWD`/`FS_READDIR` 传 1；读方向 `sys_copy_user_string`（IO_WRITE/path）/`sysfs_fwrite` 传 0。QEMU 新增 `bufcheck.elf` 场景验证 0x00100000 读写均返回 -1、合法 buffer 正常。

#### R3-N2 `TIME_SLEEP` 在内核态忙等全程不让出，`sleep()/usleep()` 冻结所有 Ring 3 进程 ✅

`src/os/syscall.c:594-606`：`TIME_SLEEP` 用 TSC deadline + `pause` 在内核态忙等，只在 `pit_ticks()` 变化时做信号检查，没有任何 `ring3_yield_current`。而 `ring3_timer_switch`（`src/os/ring3.c:290-292`）对 `frame->cs` 为内核 CS 的中断帧直接 `return frame`——IRQ0 在 syscall 期间永不调度。

- `sleep(3600)` 使本进程在内核态空转一小时，其它 READY 进程、kernel idle 上下文全部饿死（PIT 仍在走但每次都恢复同一帧）。对比 `IO_READ`/`IO_GETCHAR` 都有 `sys_keyboard_wait_yield` 契约。
- 次级：`sys_gets` 只在 `i==0` 时 yield（为避免重启丢行中字符），行中等键时 `hlt` 同样不让出——用户输到一半走开会让其它进程无限期饿死。

建议：`TIME_SLEEP` 循环里周期性 `ring3_yield_current`（需像 IO_READ 一样回退 eip/返回 klass 重启，或接受 sleep 被信号中断返回）；`sys_gets` 行中等待可考虑把已读字符暂存到内核侧再 yield。

**已修复**：新增 `RING3_PROCESS_SLEEPING` + 每进程 `sleep_deadline`（PIT tick）。Ring 3 的 `TIME_SLEEP` 首次进入时把 deadline 写入并经 `ring3_sleep_yield_current` 挂起（eip 回退 2 + `syscall_switch_frame` + 返回 klass 的重启契约）；IRQ0 的 `ring3_timer_switch` 到期把 SLEEPING 翻回 READY（保留 deadline 让重启的 syscall 自己清零，避免睡眠翻倍）；`ring3_signal_scan` 对有 pending 信号的睡眠者置 READY 并清 deadline（放弃剩余睡眠），`ring3_kill`/`vt_pid_alive`/`PROC_LIST` 均覆盖 SLEEPING；内核态调用方保留 TSC 忙等。新增 `sleep.elf` QEMU 场景验证：睡眠中 F2 在 ~10ms 切换 vt、睡眠正常唤醒、Ctrl+C 可杀死睡眠进程。`sys_gets` 行中等待的次级问题未动。

#### R3-N3 mini-libc `fgets(stdin)`/`fscanf` 尾随空白永远等不到 `'\n'`，正常路径挂起 ✅

内核 `IO_READ`（`sys_gets`，`src/os/syscall.c:112-115`）把 `'\n'`/`'\r'` 吃掉只回显，`in_buf` 里**不含换行符**。mini-libc `in_getch`（`src/stdio.c:243-258`）按行缓冲分发字符：

- `fgets(buf, n, stdin)`（`src/stdio.c:630-640`）以 `c=='\n'` 为行尾——该字符永远不会出现，只能靠 `size-1` 字符耗尽或空行（rc==0→in_getch 返回 -1→EOF）结束。正常用法 `fgets` 读一行会跨行阻塞直到缓冲写满，是确定性挂起。
- `scanf`/`fscanf` 格式串末尾的空白指令（经典 `scanf("%d\n")`）在 `vfscanf` 的 `isspace` 分支 `isrc_skip_ws` 里消费完当前行缓冲后继续 `in_getch` 阻塞等**下一行**——与 hosted scanf 的"等一个换行"语义恰好相反，这里换行已被内核吞掉，等价于必须再输入一整行。
- `getline()`（`src/stdio.c:488-493`）直接走 `IO_READ`，返回值不含 `'\n'`（与 POSIX getline 返回含换行的长度不符），且头文件已声明不可与 getchar 混用——属已知设计，但 `fgets` 无任何注释说明此限制。

建议：`in_getch` 行尾补一个虚拟 `'\n'`（或 EOF 语义改为"行结束"），让 `fgets`/`scanf` 行为与 hosted C 一致；至少在 `fgets`/`stdio.h` 注释中声明 stdin 行为。

**已修复**：① mini-libc `in_getch` 在每次 `IO_READ` 返回 `rc>0` 后向行缓冲追加虚拟 `'\n'`（`in_len = rc + 1`），`fgets`/`scanf`/`fscanf` 由此获得正常行尾语义——连读两行不串行（虚拟换行被上行消费，下次 `in_getch` 触发新 `IO_READ`）；`getchar()` 循环滤掉 `'\n'` 以保持"不返回换行"契约。② 内核 `sys_gets` 新增 `gets_waits[8]` 按 `pid & 7` 索引的挂起槽 `{pid, buf, i}`：等待按键前无论 `i==0` 与否都先存槽再 `sys_keyboard_wait_yield`（此前仅空缓冲让出，行中 `hlt` 会饿死 `vt_service`），重启时从槽恢复已读字节数；所有出口清槽。同 pid&7 碰撞的退化仅为丢失部分已读行。测试：mini-libc 新增 `stdio/stdin.elf`（fgets×3 含小缓冲截断 + getchar 过滤断言，runner 注入 5 行输入）；QEMU 新增 `iowait.elf` 场景——IO_READ 行中（已敲 2 字符未回车）按 F2 在 ~10ms 切 vt、补全回车后 LINE-OK。

### P2

#### R3-N4 `fopen(path,"a")` 不追加反而从文件头覆盖；`write(1)`/`read(0)` 不映射到控制台 ⬜

- `mini-libc/src/fs.c:72-94` `fs_fopen` 把 `'a'` 与 `'w'` 一样映射为 `FS_O_RDWR`，内核 fd `offset=0`；`fs_fwrite` 按 offset 写 → `"a"` 模式从文件**开头**覆盖写，与 POSIX 追加语义完全相反，属静默数据破坏（`"w"` 不截断已有尾部至少注释承认了，`"a"` 连注释都没有）。内核 ABI 无 lseek/append 模式，无法用现有 syscall 实现真正追加。
- `mini-libc/src/unistd.c:41-47` `read/write` 直接转 `fs_read/fs_write`：`write(1, buf, n)`（STDOUT_FILENO）会查内核全局 `sysfs_fds[1]`——要么返回 -1（fd 未用），要么**写进恰好占用 fd 1 的无关文件**；`read(0,...)` 同理。标准 fd 0/1/2 与内核 fd 表完全不关联，unistd 层 I/O 对控制台不可用。

建议：`fs_fopen` 对 `'a'` 显式返回 ENOSYS/EINVAL 直至内核支持追加或 lseek；`unistd read/write` 对 fd 0/1/2 路由到 `IO_READ`/`IO_WRITE`/`in_getch` 系列，或文档声明不支持。

#### R3-N5 `execve` 成功后调用进程不退出，新旧进程并存 ⬜

`PROC_EXEC`（`src/os/syscall.c:682-695`）调 `elf_run` 只 `ring3_schedule_mm` 一个**新**前台进程，随后返回 0；调用者 syscall 正常返回 → mini-libc `execve` 返回 0，原进程继续运行。`mini-libc/include/unistd.h:26` 注释声称"Returns only if the exec fails"，与实际不符。POSIX exec 语义是镜像替换；当前实现是"再 spawn 一个前台进程"，且旧进程仍占一个进程槽/CR3。

建议：要么 `PROC_EXEC` 成功路径对当前进程 `ring3_terminate_current`（真 exec），要么修正 unistd.h 注释把它定义为 spawn-and-continue。

#### R3-N6 `user_entry` 演示代码页被映射为 `WRITABLE|USER|BORROWED` 且所有 mm 共享同一物理页 ⬜

`src/os/syscall.c:702-715`（`PROC_RUN_USERCODE`）与 `src/shell/shell.c:254-255`（`cmd_ring3`）都把 `(uint32_t)user_entry` 处的物理页以 `PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER|PAGE_BORROWED` 映射进新 mm。`linker.ld:28-33` 的 `.user.text` 位于低半区恒等映射内，`physical=vaddr=user_entry`，**每个进程映射的是同一物理页**。

- 任一 Ring 3 进程向自己的 `user_entry` VA 写入即可改写共享代码页：之后所有 `ring3`/`PROC_RUN_USERCODE` 启动的演示进程执行被注入的代码（跨进程代码注入/破坏）；`user_prefix` 字符串同页可改。
- `WRITABLE` 也无必要——演示代码只读即可。

建议：去掉 `PAGE_WRITABLE`（代码页只读）；更彻底的做法是每次 spawn 分配私有物理页拷贝代码，或干脆废弃该演示路径。

#### R3-N7 `fprintf`/`fwrite` 到 REG 文件逐字符 `FS_WRITE`，每次内核整文件 read-modify-write ⬜

`mini-libc/src/stdio.c:522-537` `file_putc_raw` 对 `FS_FILE_REG` 每字符调 `fs_fwrite(&ch,1,1,f)` → `SYS_FS/FS_WRITE` → `sysfs_fwrite` 每次 `fat32_read_file` 整文件 + `fat32_write_file` 整文件回写（`src/os/syscall.c:447-479`）。`fprintf(f, "...")` 输出 N 字符 = N 次全文件 IO，复杂度 O(文件大小×N)；`fputs`/`fputc` 同理。叠加内核 P2-5/P2-11 的全局 fd/整文件语义，向文件写几 KB 即不可用。`fgets`/`fgetc` 反向同样每字节整文件重读。

建议：mini-libc FILE 层加写缓冲（攒批后一次 `fs_write`），读侧加预读缓冲；根治仍需内核流式 FS（见 P2-5）。

#### R3-N8 `vformat`/`vfscanf` 忽略 `ll`/`hh` 长度修饰，`%lld` 参数错位 ⬜

`mini-libc/src/stdio.c:145-146`（输出）与 `:381`（输入）把 `h/hh/l/ll/j/z/t/L` 全部跳过按 `int`/`unsigned` 处理。i386 上 `long long` 占两个栈槽：`printf("%lld %d", ll, x)` 中 `va_arg(ap,int)` 只取低 4 字节且 ap 只前进 4 字节，**后续所有参数整体错位**；`scanf("%lld", &ll)` 只写低 4 字节，高 4 字节残留。`inttypes.h` 注释承认"%lld is not yet parsed"，但 PRId64 宏仍展开为 `"lld"`，用户按标准用法必然踩坑。

建议：解析 `ll`/`l` 并对 64 位参数用 `va_arg(ap, long long)`/`unsigned long long`（需要 out_uint_x 支持 64 位除法——可用 `__udivdi3` 或手写）；或从 inttypes.h 移除 64 位 PRIm 宏直到实现。

### P3

1. `src/os/syscall.c` 各 `SYS_*` 已知 class 的 `default:` 子操作分支一律 `return 0`——拼错 op 号静默"成功"，建议返回 -1/ENOSYS（未知 class 已正确返回 -1）。
2. `mini-libc/src/stdlib.c:204` `strtol` 在 `acc=2^31`（`LONG_MIN` 边界）时 `-(long)acc` 构成 `-(LONG_MIN)` UB（x86 上碰巧正确）；`abs(INT_MIN)`/`labs(LONG_MIN)` 同型 UB。
3. `mini-libc/src/stdio.c:213-221` `snprintf(NULL, 0, ...)` 因 `buf==0` 走 console 分支把内容打印到屏幕，而非只计算返回长度；`snprintf` 其余截断语义正确。
4. `mini-libc/src/stdio.c:480` `vfscanf` 的 `eof_hit` 只在**字面量**分支设置；`scanf("%s")`/`%d` 在输入流首字符即 EOF 时经 `goto done` 返回 0 而非 EOF（POSIX 应为 EOF）。
5. `mini-libc/src/stdio.c:255` `in_getch` 把空行（内核 rc==0）当 EOF 返回 -1：`getchar()` 在用户按一次回车时返回 EOF，是瞬时错误而非流终止。
6. `mini-libc/src/stdlib.c:21-25` `_Exit`/`exit` 恒以 `ecx=0` 调 `PROC_EXIT`，`status` 被丢弃（内核本就不记录，但 ABI 注释"ecx: 退出码"有误导）；`signal()`/`kill()` 的早退分支不设 `errno`。
7. `mini-libc/include/stdint.h`：`intmax_t`/`uintmax_t` 定义为 `int`/`unsigned int`（hosted LP32 通常为 `long long`）；`int_least8_t` 等 least 类型取 `int` 过宽但合法。`inttypes.c` 的 imax\* 与之自洽，仅与 hosted C ABI 有偏差。
8. `mini-libc/src/stdlib.c:51-53` `xalloc` 的 align 归一化 `while (a < align) a <<= 1`：用户伪造 malloc header 的 `align` 字段为非 2 幂大值（如 `0x80000001`）会让 `a` 溢出为 0 后死循环——仅自伤（header 在自己地址空间），但 `hdr_of` 不校验 align 幂次/上界。
9. `src/os/syscall.c:855-867` `sys_call` 是内核态 `int 0x80` 包装：`current_process < 0`（内核/idle 上下文）时 `PROC_EXIT`/`PROC_YIELD` 等静默无效或走默认分支，属未文档化的边界。
10. `src/os/syscall.c` `sysfs_fd.size` 字段在 `FS_READ`/`FS_WRITE` 中被维护但从不参与任何判定（死字段）；`FS_OPEN` 的 mode 未限定 `<= SYSFS_O_RDWR` 之外的位（`mode > 1` 已拒，OK）。

### 复核结论（本轮确认无新问题）

- `syscall.S` 四个 wrapper 的栈参数偏移（+20/+24/+28/+32/+36）、callee-saved 保存顺序与 `int 0x80` ABI 一致；`sys_call`（内核侧 inline asm）与 mini-libc 路径共用同一 `CD 80` 编码，`sys_keyboard_wait_yield` 的 `eip -= 2` 回退对两条路径都成立。
- `sys_gets`/`IO_GETCHAR` 的 yield-restart 契约（返回 `klass` 使 eax 槽重派发原 class+op）、fatal abort 后跳过用户 buf 写入、`i==0` 才 yield 的行中字符保护均与注释一致；P0-1/P0-2 修复无回归。
- `sysfs_fopen/fread/fwrite` 的路径解析、`is_dir`/exists 组合判断、`(uint64_t)offset+size` 上界、`sysfs_fd_index` 边界均正确；`fat32_list_directory(path, 0)` 的 NULL callback 有 `callback != 0` 保护（`src/fs/fat32_dir.c:304`），`FS_CHDIR` 探测安全。
- `ring3_signal_set_handler` 的 `handler > SIG_IGN && handler >= USER_SPACE_END` 边界、`sig_actionable` 的 IGN/DFL 分类、`sig_deliver` 的 CR3 切换写帧、`sigreturn` 的整帧恢复与 mini-libc `signal()` 的 userspace handlers 缓存语义一致；`SIG_IGN=1`/`SIG_DFL=0` 两端取值吻合。
- `crt0.S` 的 argc/argv/envp 帧、`setjmp.S` 的 esp+4/eip 保存与 longjmp 恢复、`qsort` 的小侧递归、`strtol/strtoul` 的 ERANGE/前缀检测、`bsearch`/`memmove` 重叠方向均正确。
- `PROC_LIST` 12 字节记录与 `proc_info_t` 布局、`FS_*` 参数序（a1=path/fd, a2=buf, a3=size）与 `include/os/syscall.h` 及 mini-libc `sys/syscall.h` 两份头文件逐一核对一致。
