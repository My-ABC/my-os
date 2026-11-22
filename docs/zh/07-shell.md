# 07 — Shell

[English](../en/07-shell.md) | 中文

`src/shell/shell.c` 实现交互式 Shell，由 `kmain` 经 `start_shell()` 进
入。

## 输入循环

- 行缓冲 128 字节；`shell_read_line()` 通过 `keyboard_hit()` 轮询 PS/2
  键盘驱动，无按键时以 `sti; hlt` 休眠等待。存在前台 Ring 3 进程时隐藏
  提示符但仍接受输入，保证 `bg`/`kill` 可用。
- 命令历史：保存最近 16 条非空行，连续重复条目不重复记录。`Up`/`Down`
  方向键回看历史（正在编辑的行保存为草稿，回到最新条目之后再按 `Down`
  可恢复）；回显通过 `vga_set_cursor` 重绘，VBE 图形模式同样支持。仅
  在提示符可见时可用历史。
- 行编辑：`Left`/`Right` 在行内移动插入光标；可打印键在光标处插入，
  `Backspace` 删除光标前的字符（此前两者都只能在行尾生效）。每次编辑
  从记录的提示符原点经 `vga_set_cursor`/`vga_columns` 整行重绘，支持
  跨行换行；无提示符原点时（前台 Ring 3 任务运行中）编辑键被忽略。
- Ctrl+C：键盘驱动上报 `KEYBOARD_KEY_SIGINT`（0x05）。Shell 回显 `^C`、
  丢弃正在编辑的行、向前台 Ring 3 进程发送 `SIGINT`（若存在），下一轮循
  环自动重绘提示符——无需再按 Enter。详见
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
  中的 `g_cwd`，由 `cd` 修改，`SYS_GETCWD`/`SYS_CHDIR` 系统调用共用）前
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

## 现状：内核态 Shell

Shell 目前运行在内核态，直接调用内核子系统（`fat32_*`、`ring3_*`、
`vga_*`、`printf`）。迁移到 Ring 3 是 `TODO.md` 中已规划的工作（"将
Shell 完善并迁移到 Ring 3"）；前置条件包括新的进程控制、控制台光标定
位、目录列举、更丰富时间/RTC 数据的系统调用，以及更大的
`RING3_PROCESS_COUNT` 和用于隔离的每进程页表。
