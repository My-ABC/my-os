# 15 — 虚拟终端（tty）

MyOS 提供 `VT_COUNT`（= 2）个共享同一物理屏幕的虚拟终端。**F1 选择
vt 0，F2 选择 vt 1**；每个终端并发运行各自的 `shell3.elf` Ring 3
进程。只有当前激活的终端可见并接收键盘输入；每个终端都保留私有的
屏幕备份，因此切换时各自内容都得以保留。

实现在 `src/drivers/vt.c` / `include/drivers/vt.h`。

## 公开 API

| 函数 | 用途 |
|---|---|
| `vt_active_index()` | 当前可见终端的索引 |
| `vt_render_index()` | 当前上下文输出所属的终端：运行中 Ring 3 进程的 `vt`，内核上下文为 `VT_FALLBACK`（0）。`vga.c`/`vbe.c` 经此解析渲染目标 |
| `vt_text_target(vt)` / `vt_vbe_target(vt)` | 终端的备份存储指针——激活终端返回物理显示地址，否则返回私有缓冲（不可用时返回 `NULL`） |
| `vt_request_switch(vt)` | 排队一次切换；键盘 ISR 中调用，由 `vt_service` 延迟执行 |
| `vt_service()` | 延迟工作：执行挂起的切换，然后确保每个 vt 都有存活的 shell3（经注册的 spawner 拉起或重启）。空闲时代价很低，可在任意内核上下文调用 |
| `vt_init()` | 分配各 vt 备份存储；在 `vga_init`/`vbe_initialize` 和 `pmm_init` 之后调用 |
| `vt_set_spawner(fn)` / `vt_spawn_shell(vt)` / `vt_shell_alive(vt)` | spawner 注册、立即拉起、终端受跟踪 shell pid 的存活检查 |

## 切换流程

1. 键盘 ISR 识别 F1（扫描码集 1 make code `0x3B`）和 F2（`0x3C`），
   调用 `vt_request_switch()`。这些扫描码不会进入各 vt 的输入缓冲。
2. `vt_service()`——在 `shell3_supervisor` / `shell_read_line` 的
   内核空闲循环中运行——取出挂起的请求并调用 `vt_do_switch()`。
3. `vt_do_switch()` 把当前终端的屏幕拷入其私有存储，再把目标终端的
   存储拷上物理屏幕，然后调用 `vga_console_switched()` /
   `vbe_console_switched()` 重新同步光标状态。整个拷贝过程关中断
   执行（否则 IRQ0 的光标闪烁会在拷贝中途读到 `vt_render_index()`）。

之所以延迟执行，是因为备份存储的 `memcpy` 量太大，绝不能放进
IRQ1 上下文。

## 屏幕备份存储

**VGA 文本模式**——每个非激活 vt 拥有私有的
`uint16_t text_cells[80*25]` 数组；激活 vt 直接渲染到物理
`0xB8000`。切换就是两次 4 KiB 的 `memcpy`。

**VBE 图形模式**——每个 vt 在内核高半区空闲窗口
`0xD0000000`–`0xE0000000`（扫描输出映射 `0xE0000000` 之下）拥有
私有帧缓冲。页按需从 PMM 分配并映射。每终端 stride 在初始化时
推导：

```
stride = align(vbe_scanout_bytes(), VT_FB_ALIGN)   /* 1 MiB */
       = align(pitch * height, 1 MiB)
```

实测 1280×800×32 模式下 `vbe_scanout_bytes()` 返回
5120×800 = 4,096,000 字节，因此每个终端获得 **4 MiB** stride——
切勿硬编码 640×480 的尺寸。

## 进程模型

- `struct ring3_process` 带有 `vt` 字段；子进程继承父进程的 vt。
- `foreground_pid` 按终端分槽：`foreground_pid[VT_COUNT]`，经
  `ring3_foreground_pid_vt(vt)` 访问。
- `ring3_schedule_mm(..., foreground, vt)` 接受目标 vt；
  `elf_run_vt(image, size, vt)` 把 ELF 加载到指定终端
  （`elf_run` 用当前 vt 委托调用）。
- 进程释放前台槽位时，若同 vt 上的 `parent_pid` 仍存活则回退给它
  ——前台槽位不会出现可见的零窗口（这正是修复 `a2ebbbf` 双提示符
  竞态的机制）。
- Ctrl+C 由键盘 ISR 投递给*当前激活* vt 的前台 pid，即
  `ring3_foreground_pid_vt(vt_active_index())`。

## 输入路由

每个 vt 有独立的 256 字节键盘环形缓冲
（`keyboard_getchar_vt` / `keyboard_hit_vt`）。ISR 只把按键入队到
当前可见终端的缓冲，因此隐藏终端的 shell 在切走期间其待处理输入
保持不动。`IO_READ`/`IO_GETCHAR`/`IO_WRITE` 及进程控制操作通过
`ring3_current_vt()` 解析调用者的 vt。

## 生命周期与重启

`kernel.c` 的 `shell3_supervisor` 注册 spawner 后循环调用
`vt_service()`。开机时两个终端各得到一个 `shell3.elf` 实例；当某
终端受跟踪的 shell pid 死亡或消失时，`vt_service` 在该 vt 上重新
拉起。内核 shell 固定在 vt 0，仅当未挂载磁盘或 `shell3.elf` 缺失
时才可达。

两条不变量保证其可靠：

- **`vt_spawn_shell` 全程关中断**执行 FAT32 读盘 + ELF 加载 + pid
  跟踪。IRQ0 抢占会把该内核上下文困在 `kernel_frame` 里，只有调度器
  回到这里才恢复——而一个永不让出的进程（如阻塞在 `IO_GETCHAR`
  的 shell）可能让它无限期饿死。
- **`sys_keyboard_wait_yield` 在键盘缓冲为空时无条件让出**（回退
  保存的 `eip` 2 字节使 `int 0x80` 重启）。否则单个阻塞在
  `IO_GETCHAR` 的进程会饿死内核空闲循环，`vt_service` 完全冻结。

## 串口输出

Ring 3 `IO_WRITE` 渲染到进程所属 vt 的缓冲，并把字符串镜像到
**COM3**（`serial3_print`）——单一全局镜像，vt 0 与 vt 1 的输出
会在其上交错。COM1 只承载内核侧输出（诊断信息及内核 shell 命令）。

## 限制

- `VT_COUNT` 固定为 2；增加终端需要同步扩展 vt 数组、每终端前台
  槽位和 F 键映射。
- COM3 是全局镜像，不是按终端分离的串口流。
- 每个 vt 只保留一屏状态——没有滚动回溯缓冲。
- 内核 shell 只存在于 vt 0。
