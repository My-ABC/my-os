# 07 — Shell

English | [中文](../zh/07-shell.md)

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
- Line editing: `Left`/`Right` move an insertion cursor within the
  line; printable keys insert at the cursor and `Backspace` deletes the
  character before it (previously both only worked at end of line).
  Every edit repaints the line from the recorded prompt origin via
  `vga_set_cursor`/`vga_columns`, so wrapped lines are handled; editing
  keys are ignored while no prompt origin exists (foreground Ring 3
  task running).
- Ctrl+C: the keyboard driver reports `KEYBOARD_KEY_SIGINT` (0x05). The
  shell echoes `^C`, discards the line being edited, sends `SIGINT` to the
  foreground Ring 3 process when one exists, and redraws the prompt on the
  next loop iteration — no Enter is needed. See
  [13-signals.md](13-signals.md).
- `split_line()` tokenizes on whitespace into at most 16 arguments; an
  argument starting with `"` keeps its inner spaces up to the matching
  closing quote (the quotes themselves are stripped), but only when the
  quoted content is non-empty — `""`, unterminated `"`, and `"` anywhere
  else in a word are all literal. `\"` inside a quoted argument does not
  close it. Every argument then goes through
  `shell_unescape_arg()`: `\n` becomes a real newline, `\\` a literal
  backslash, and `\"` a literal `"`; any other `\x` sequence prints an
  `unknown escape` warning and is kept literally.
- Dispatch is an exact `strcmp` against `cmd_table` (name, handler,
  one-line description); unknown input prints `Unknown command`.
- Prompt: `myos <cwd> > ` when FAT32 is mounted, `myos > ` otherwise.

## Path resolution

Disk commands resolve arguments through `fat32_resolve_path()`:

- Absolute paths (`/...`) are used as-is; relative paths are prefixed with
  the shared working directory (`g_cwd` in `src/fs/fat32.c`, changed by
  `cd` and also used by the `FS_GETCWD`/`FS_CHDIR` syscalls).
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
[08-shell-commands.md](08-shell-commands.md). The `cmd_table` in
`shell.c` is the authoritative list — update both together.

## Status: kernel-mode shell + experimental Ring 3 shell

The primary shell runs in kernel mode and calls kernel subsystems
directly (`fat32_*`, `ring3_*`, `vga_*`, `printf`).

An experimental user-mode port lives in `src/shell/shell_ring3.c`
(header `include/shell/shell_ring3.h`), built as `build/shell3.elf`
by `make shell3` and started with `run shell3.elf`. It currently
contains only code that goes through the mini-libc public API — no
direct syscall usage: pure helpers (argument splitting, escape
handling, path utilities, command history) plus stdio/time-backed
commands `time`, `echo`, `hello`, `help`, `exit`. Filesystem,
process, power, signal, raw-key editing, and foreground-arbitration
commands are deferred until mini-libc grows the matching wrappers
(the earlier syscall-based v1 command set is preserved in git
history). Full migration remains tracked in `TODO.md`.
