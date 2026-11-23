# MyOS 代码审计报告（P0–P3）

审计范围：`src/` 全部内核代码（arch/drivers/fs/mm/lib/os/shell），mini-libc 按约定不在本次范围。行号基于 `feature/vt-f1-f2` 分支当前 HEAD。

严重度定义：**P0** 必坏 / **P1** 可能坏 / **P2** 隐患 / **P3** 风格。
状态：✅ = 本轮已修复并推送；⬜ = 未修复（文档已记录或待后续 PR）。

---

## 一、syscall / Ring 3 边界

| # | 位置 | 问题 | 级别 | 状态 |
|---|------|------|------|------|
| 1 | `src/os/syscall.c` IO_WRITE | 用户字符串指针未经校验直接交给 `vga_print`/`serial3_print`，坏指针/缺 NUL → 内核态缺页 → panic | P1 | ✅ 已修：`sys_copy_user_string` 拷入 kmalloc 缓冲，上限 `IO_WRITE_MAX`=4096，失败返回 -1 |
| 2 | `src/os/syscall.c` IO_READ | `sys_gets` 直接向 `a1` 写入，无指针/size 校验 | P1 | ✅ 已修：`size<=0` 或 `sys_user_buffer_ok` 失败返回 -1 |
| 3 | `src/os/syscall.c` 校验判据 | 初版用 `ring3_user_range_permitted` 判定，漏掉未注册 image range 的进程（内嵌 `user_entry` demo）且放行"许可但未映射"页 | P1 | ✅ 已修（`ac88eac`）：改为 `paging_mm_get_phys` 判定"页已映射" |
| 4 | `src/os/syscall.c` `user_realloc` | `ptr-sizeof(user_block)` 无范围校验直接解引用 → 内核 panic | P1 | ⬜ 保留为 panic 演示路径（`kfault.elf` 专用）；正式修法是加 `sys_user_buffer_ok` 式校验，属 `MEMORY_*` 校验补齐的一部分 |
| 5 | `src/os/syscall.c` IO_READ/IO_GETCHAR | 无前台检查，同 vt 后台进程可抢前台按键（IO_WRITE 有检查，此处不对称） | P2 | ⬜ 不修，已写入 `docs/{en,zh}/04-syscall-abi.md` Limitations |
| 6 | `src/os/syscall.c` `sys_gets` | 不过滤 `KEYBOARD_KEY_UP/DOWN/LEFT/RIGHT`（0x01–0x04），方向键原样入缓冲并回显 | P3 | ⬜ 不修，已写入 Limitations |
| 7 | `src/os/syscall.c` `sysfs` fd 表 | 全局 fd 表、进程退出不回收、`FS_READ/WRITE` 整文件读改写 | P2 | ⬜ 已知限制，文档已记录 |

## 二、虚拟终端（vt）

| # | 位置 | 问题 | 级别 | 状态 |
|---|------|------|------|------|
| 8 | `src/drivers/vt.c` `spawn_failed` | 任何 spawner 非零返回都永久锁存 → 无盘启动后 `disk` 命令挂载也无法自动拉起 shell3 | P1 | ✅ 已修（`6a1c454`）：`VT_SPAWN_RETRY` 可重试（~1Hz PIT 节流），ELF 错误才锁存 |
| 9 | `src/drivers/vt.c` `vt_do_switch` | 任一侧 `fb_ready==0` 拒绝切换 → 单侧分配失败则 F1/F2 永久失效且屏幕无提示 | P2 | ⬜ 不修（降级方案更糟），已写入 15 章 Limitations |
| 10 | `src/drivers/vt.c` `vt_alloc_fb` | 中途分配失败泄漏已映射页（窗口永不回收） | P2 | ⬜ 已写入 15 章 Limitations |
| 11 | `src/drivers/vt.c` `vt_pending` | ISR 写/`vt_service` 读非原子 | P3 | ⬜ 单核窗口极窄，已写入 Limitations |
| 12 | `src/drivers/vt.c` `vt_spawn_shell` | spawner 全程关中断执行 FAT32 读盘+ELF 加载（设计权衡，长时间关中断） | P3 | ⬜ 有意为之（防止 spawn 被抢占困在 kernel_frame） |

## 三、串口 / 键盘 / 显示

| # | 位置 | 问题 | 级别 | 状态 |
|---|------|------|------|------|
| 13 | `src/drivers/serial.c` TX 等待 | `serial_putchar`/`serial3_putchar` 死等 LSR bit5，无超时 → 无 COM3 硬件时永久挂死 | P1 | ✅ 已修（`43e2159`）：~1M 次轮询上限，COM3 超时打一次性警告 |
| 14 | `src/drivers/serial.c` | `serial_wait_char`/`serial_gets` 无超时且无内核调用方 | P3 | ✅ 已删（`4a19fdb`），测试同步更新 |
| 15 | `src/drivers/keyboard.c` | F1/F2 只认 scan set 1 make code；set 2/E0 前缀下 F 键失效 | P3 | ⬜ QEMU/常规键盘均为 set 1，低风险 |
| 16 | `src/drivers/vga.c` `vga_update_cursor` | `visible` 形参未使用 | P3 | ✅ 已清理（`4a19fdb`） |
| 17 | `src/drivers/vga.c`/`vbe.c` | 非活跃 vt 的 `IO_CLEAR`/光标操作作用于私有缓冲（正确），但 `vga_clear` 类操作无前台检查，后台进程可清屏 | P3 | ⬜ 影响限于自己 vt 的缓冲，危害低 |

## 四、进程 / 调度 / ELF

| # | 位置 | 问题 | 级别 | 状态 |
|---|------|------|------|------|
| 18 | `src/os/ring3.c` `ring3_schedule` | 无 mm 版本硬编码 `VT_FALLBACK`，vt1 发起的 demo 落到 vt0 | P2 | ✅ 已修（`ade260a`）：传 `ring3_current_vt()` |
| 19 | `src/os/ring3.c` `ring3_kill` | 对 current 进程只标 DEAD+释放前台，资源回收依赖下一调度 tick | P3 | ⬜ 时序窗口极小，行为正确 |
| 20 | `src/os/elf.c` `elf_run` | `image_start/end=0` 的调用方（内嵌 demo）不注册用户范围，demand-map/保护检查对其失效 | P2 | ⬜ 与 #3 相关；`elf_run` 正常路径已注册范围，仅 `ring3` 命令路径受影响 |
| 21 | `src/os/ring3.c` `foreground_pid` | `ring3_foreground_pid_vt()` 无中断锁读写（单核下窗口小） | P3 | ⬜ 与 #11 同类，未单独处理 |

## 五、内存管理

| # | 位置 | 问题 | 级别 | 状态 |
|---|------|------|------|------|
| 22 | `src/mm/paging.c` `paging_mm_unmap` | 只在 PT 清空时回收 PT 页；PDE 若为 1GiB 大页直接返回（正确），但 `paging_flush_tlb(0U)` 只刷一个地址，全 PT 失效靠逐条 TLB shootdown 缺失 | P2 | ⬜ 单核无 shootdown 需求；`flush_tlb(0)` 是否覆盖全表取决于实现，建议核对 |
| 23 | `src/mm/heap.c` | `heap_find_free` 首-fit、`heap_split` 未见合并边界检查（`remaining==0` 才返回，相邻块合并依赖 `heap_merge_*` 调用点） | P2 | ⬜ 需专项审 heap 块合并/分裂不变量，本次未深入 |
| 24 | `src/mm/pmm.c` | PMM 上限 36-bit/64GiB 已文档化；bitmap 分配未在本次重读 | P3 | ⬜ 历史代码，测试覆盖较好 |

## 六、文件系统 / 驱动 / 其他

| # | 位置 | 问题 | 级别 | 状态 |
|---|------|------|------|------|
| 25 | `src/fs/fat32_dir.c` | 目录回调禁止文件 I/O（共享静态 cluster 缓冲）——约束已遵守但属易碎设计 | P2 | ⬜ 架构约束，文档已载 |
| 26 | `src/fs/fat32*.c` | `FAT32_MAX_FILE_SIZE`=1MiB 上限；超限文件静默截断还是报错取决于各调用点 | P2 | ⬜ `fat32_read_file` 有 size 检查，写路径需逐点核对 |
| 27 | `src/lib/stdio.c` `gets` | 内核侧 `gets(buf,size)`：`size<=0` 时 `i < size-1` 立即假 → `buf[0]='\0'`，但 `buf` 指针本身无校验（内核内部调用，风险低） | P3 | ⬜ 内部函数，调用方均传真实缓冲 |
| 28 | `src/drivers/acpi.c` | 553 行 ACPI 解析，未在本次逐行审计 | P2 | ⬜ 建议单独审计（表解析边界/校验和） |
| 29 | `src/shell/shell_ring3.c` | 1025 行行编辑/历史实现，未在本次逐行审计 | P2 | ⬜ 建议单独审计（缓冲边界、历史索引） |
| 30 | `src/arch/x86/*.S`/`isr_handlers.c` | 汇编中断帧/TSS/trap gate 合约依赖手工核对 | P2 | ⬜ QEMU 集成测试覆盖主要路径 |

## 七、本轮测试基建修复（非 bug 但阻塞验证）

| # | 位置 | 问题 | 状态 |
|---|------|------|------|
| T1 | `test/integration/qemu_session.py` | COM3 迁移后 harness 只读 COM1 → Ring 3 输出断言全挂 | ✅ 三次修复：加 COM3 后端 → `wait_for`/`wait_for_regex` 走统一读取 → COM1/COM3 分缓冲防行内交错污染 |
| T2 | `test/integration/fault_demo.asm` + `page_fault_scenarios.py` | `kfault.elf` 依赖"IO_WRITE 坏指针→panic"前提，#1 修复后前提失效 | ✅ 改用 `MEMORY_REALLOC` 坏指针（`user_realloc` 仍无校验解引用） |

## 验证状态

- `make build` ✓、`make test` 45 项 OK（每个修复 commit 后均跑过）
- `make test-qemu`：主套件全 ✓ PASS；第二阶段 kfault panic 场景在修复 T1/T2 后重跑中（本报告写入时上一轮被中断，结论待确认）
- mini-libc：按约定未审计
