# 06 — Shell

English | [中文](../zh/06-shell.md)

`src/shell/shell.c` implements the interactive shell entered by `kmain`
via `start_shell()`.

## Input loop

- Line buffer: 128 bytes; `shell_read_line()` polls the PS/2 keyboard
  driver with `keyboard_hit()` and sleeps via `sti; hlt` while waiting.
  While a foreground Ring 3 process exists the prompt is hidden but input
  is still accepted so `bg`/`kill` keep working.
- Command history: the last 16 non-blank lines are kept in a static
  table; consecutive duplicates are skipped. `Up`/`Down` recall entries
  (the in-progress line is preserved as a draft and restored when
  stepping back past the newest entry). Recall repaints the input through
  `vga_set_cursor`, which also works in VBE mode; history is only
  available while the prompt is visible.
- `split_line()` tokenizes on whitespace into at most 16 arguments.
- Dispatch is an exact `strcmp` against `cmd_table` (name, handler,
  one-line description); unknown input prints `Unknown command`.
- Prompt: `myos <cwd> > ` when FAT32 is mounted, `myos > ` otherwise.

## Path resolution

Disk commands resolve arguments through `fat32_resolve_path()`:

- Absolute paths (`/...`) are used as-is; relative paths are prefixed with
  the shared working directory (`g_cwd` in `src/fs/fat32.c`, changed by
  `cd` and also used by the `SYS_GETCWD`/`SYS_CHDIR` syscalls).
- `.` components are dropped, `..` pops one component, repeated `/` is
  collapsed, and the result is normalized to a canonical absolute path.
- Names are case-sensitive (LFN-aware FAT32 lookup).

`disk_resolve_destination()` implements the shared `cp`/`mv` rule: when
the destination is an existing directory, the effective target becomes
`dst/<source-basename>`.

## Output conventions

- Disk and process commands dual-write status to VGA `printf` and COM1
  `serial_print*`, which keeps scripted QEMU sessions deterministic.
- Failures print `Unable to ...` / `Usage: ...` on VGA only.

## Command table

Every command, its arguments, and semantics are documented in
[07-shell-commands.md](07-shell-commands.md). The `cmd_table` in
`shell.c` is the authoritative list — update both together.
