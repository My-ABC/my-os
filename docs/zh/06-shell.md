# 06 — Shell

[English](../en/06-shell.md) | 中文

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
- `split_line()` 按空白拆分为最多 16 个参数；以 `"` 开头的参数会保留
  内部空格直到配对的闭引号（引号本身被去除，未闭合引号取到行尾）。
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

全部命令、参数与语义见 [07-shell-commands.md](07-shell-commands.md)。
`shell.c` 中的 `cmd_table` 是权威清单——两者需同步更新。
