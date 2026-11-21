# 06 — Shell

[English](../en/06-shell.md) | 中文

`src/shell/shell.c` 实现交互式 Shell，由 `kmain` 经 `start_shell()` 进
入。

## 输入循环

- 行缓冲 128 字节；`gets()` 通过 PS/2 键盘驱动读取按键。
- `split_line()` 按空白拆分为最多 16 个参数。
- 分发为对 `cmd_table`（名称、处理函数、一行描述）的 `strcmp` 精确匹
  配；未知输入打印 `Unknown command`。
- 提示符：FAT32 挂载后为 `myos <cwd> > `，否则为 `myos > `。

## 路径解析

磁盘命令的参数经 `disk_resolve_path()` 解析：

- 绝对路径（`/...`）原样使用；相对路径加上 Shell CWD
  （`disk_cwd`，由 `cd` 修改）前缀。
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
