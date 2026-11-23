# 07 — Shell

[English](../en/07-shell.md) | 中文

`src/shell/shell.c` 实现交互式 Shell，由 `kmain` 经 `start_shell()` 进
入。

## 输入循环

- 行缓冲 128 字节；`shell_read_line()` 通过 `keyboard_hit()` 轮询 PS/2
  键盘驱动，无按键时以 `sti; hlt` 休眠等待。存在前台 Ring 3 进程时隐藏
  提示符并完全停止消费按键（`sti; hlt` 循环）——输入留在缓冲区，留给
  子进程或子进程退出后的 Shell。
- 命令历史：保存最近 16 条非空行，连续重复条目不重复记录。`Up`/`Down`
  方向键回看历史（正在编辑的行保存为草稿，回到最新条目之后再按 `Down`
  可恢复）；回显通过 `vga_set_cursor` 重绘，VBE 图形模式同样支持。仅
  在提示符可见时可用历史。
- 行编辑：`Left`/`Right` 在行内移动插入光标；可打印键在光标处插入，
  `Backspace` 删除光标前的字符（此前两者都只能在行尾生效）。每次编辑
  从记录的提示符原点经 `vga_set_cursor`/`vga_columns` 整行重绘，支持
  跨行换行；无提示符原点时（前台 Ring 3 任务运行中）编辑键被忽略。
- Ctrl+C：键盘驱动上报 `KEYBOARD_KEY_SIGINT`（0x05）。键盘 ISR 本身
  会对前台 Ring 3 进程调用 `ring3_signal_send()`，因此即使 Shell 处于
  halt 状态、无人读取缓冲字节，信号也能送达。Shell 消费到该字节时回显
  `^C`、丢弃正在编辑的行、向仍存活的前台进程转发 `SIGINT`，下一轮循环
  自动重绘提示符——无需再按 Enter。详见
  [13-signals.md](13-signals.md)。
- `split_line()` 按空白拆分为最多 16 个参数；以 `"` 开头的参数会保留
  内部空格直到配对的闭引号（引号本身被去除），但要求引号内容非空——
  `""`、未闭合的 `"` 以及单词中间的 `"` 都按字面量处理；引号内的 `\"`
  不会闭合引号。之后每个参数
  再经 `shell_unescape_arg()` 处理：`\n` 变为真实换行，`\\` 变为字面
  反斜杠，`\"` 变为字面双引号；其余 `\x` 序列打印 `unknown escape`
  警告并保持原样。
- 分发为对 `cmd_table`（名称、处理函数、一行描述）的 `strcmp` 精确匹
  配；未知输入打印 `Unknown command`。
- 提示符：FAT32 挂载后为 `myos <cwd> > `，否则为 `myos > `。

## 路径解析

磁盘命令的参数经 `fat32_resolve_path()` 解析：

- 绝对路径（`/...`）原样使用；相对路径加上共享工作目录（`src/fs/fat32.c`
  中的 `g_cwd`，由 `cd` 修改，`FS_GETCWD`/`FS_CHDIR` 系统调用共用）前
  缀。
- `.` 组件丢弃，`..` 回退一层，连续 `/` 合并，结果归一化为规范绝对
  路径。
- 名字区分大小写（FAT32 查找感知 LFN）。

`disk_resolve_destination()` 实现 `cp`/`mv` 共用规则：目标是已存在目
录时，有效目标变为 `dst/<源文件名>`。

## 输出约定

- 磁盘和进程类命令把状态同时写到 VGA `printf` 和 COM1
  `serial_print*`，保证 QEMU 脚本化会话的确定性。
- 失败打印 `Unable to ...` / `Usage: ...`，仅走 VGA。

## 命令表

全部命令、参数与语义见 [08-shell-commands.md](08-shell-commands.md)。
`shell.c` 中的 `cmd_table` 是权威清单——两者需同步更新。

## 现状：内核态 Shell + 实验性 Ring 3 Shell

主 Shell 运行在内核态，直接调用内核子系统（`fat32_*`、`ring3_*`、
`vga_*`、`printf`）。

实验性用户态移植位于 `src/shell/shell_ring3.c`（头文件
`include/shell/shell_ring3.h`），由 `make shell3` 构建为
`build/shell3.elf`，用 `run shell3.elf` 启动。当挂载的磁盘根目录
带有 `shell3.elf` 时——`make disk` 生成的 `disk.img`——内核监督器
（`kernel.c` 中的 `shell3_supervisor`）会将其置于前台自动启动，
并在每次退出或被杀后自动重启，因此 `make run` 开机即进入 Ring 3
Shell，永远看不到内核 Shell。仅当未挂载磁盘或 `shell3.elf` 缺失/
无法加载时才会进入内核 Shell。最终版通过
mini-libc 公开 API 实现了内核 Shell 的完整命令集：全部
`fs.h` 文件命令（`ls`、`cat`、`write`、`cp [-r]`、`mv`、
`rm [-f]`、`cd`、`mkdir`、`rmdir`）、进程控制（`ps`、`fg`、
`bg`、`kill`、`run`、`elf`、`ring3`，分别经
`proc_run_demo`/`proc_run_usercode`）、`reboot`/`shutdown`
（`power.h`）、`clear`、`time [12|24]`、`echo`、`hello`、
`help`、`exit`——`disk` 仍为内核专属。行编辑（方向键、
16 条历史召回、行内光标）在用户态基于 `console.h` 原语实现；
Ctrl+C 以投递的 `SIGINT` 形式到达。当前台有其他进程时编辑器完全
停止读键并让出 CPU（`sched_yield`），直到前台槽位变化——所有
Ring 3 进程共享内核键盘缓冲，继续读键会与子进程平分按键。缓冲
的按键留给前台进程，子进程退出后成为 Shell 的 type-ahead，与
内核 Shell 不消费按键直接 halt 的策略一致。已在 QEMU 下
验证（`make test-qemu` 不受影响；手动 `run shell3.elf` 会话
覆盖全部命令）。
