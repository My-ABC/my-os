# MyOS Shell Command Reference

English | [中文](../zh/08-shell-commands.md)

This document describes the interactive shell provided by `src/shell/shell.c`.
The shell reads lines of up to 128 characters, splits them into at most 16
whitespace-separated arguments, and dispatches to the command table. Commands
are matched by exact name; there are no flags other than those documented
below.

## Prompt and paths

- When a FAT32 filesystem is mounted the prompt shows the current directory:
  `myos /dir > `. Otherwise it shows `myos > `.
- Disk commands accept absolute paths (`/dir/file`) and paths relative to the
  current directory. `.` and `..` components are resolved; multiple `/`
  characters are collapsed.
- File and directory names are case-sensitive: names written by the shell are
  stored with LFN entries preserving their original case, while legacy
  entries without LFN match through the uppercase 8.3 short name.
- All disk commands fail with `Path is too long` if the resolved path does
  not fit the internal buffers.

## Command table

| Command | Arguments | Description |
|---|---|---|
| `time` | `[12\|24]` | Print the current time (RTC, Beijing time) as `YYYY-MM-DD HH:MM:SS`. Defaults to 24-hour format; `time 12` appends `AM`/`PM` and uses 12-hour format, `time 24` is explicit 24-hour. |
| `echo` | `<text>...` | Print the arguments separated by single spaces to the screen and the COM1 serial port. |
| `reboot` | — | Reboot the machine through ACPI. |
| `shutdown` | — | Power off the machine through ACPI. |
| `help` | — | List all commands with a one-line description. |
| `clear` | — | Clear the VGA text screen. |
| `hello` | `[name...]` | Print `Hello, my-os!`, or `Hello, <name>!` when arguments are given. |
| `ring3` | — | Map and schedule one built-in Ring 3 process in the background. |
| `ps` | — | List Ring 3 processes with PID, state (`READY`/`RUNNING`/`DEAD`) and the foreground marker. |
| `fg` | `<pid>` | Make a process's output visible again (foreground). |
| `bg` | `<pid>` | Hide a process's output (background). |
| `kill` | `<pid>` | Terminate a process by PID. |
| `elf` | — | Load and run the ELF32 demo program built into the kernel image. The demo installs a `SIGINT` handler, prints `Hello from ELF32`, and waits; Ctrl+C delivers the signal, the handler prints `Bye` and exits via `PROC_EXIT`. |
| `disk` | — | Probe the ATA primary master and mount the FAT32 filesystem. |
| `ls` | `[dir]` | List a directory (default: current directory). Directories are shown with a trailing `/` and `<DIR>`; files show their size in bytes. |
| `cat` | `<file>` | Print a file's contents to the screen and serial port. Reads at most 64 KiB. |
| `write` | `<file> <text>...` | Write the given text (arguments joined by spaces, max 1023 bytes) to a file, creating it if needed. |
| `cp` | `[-r] <src> <dst>` | Copy a file. With `-r`, recursively copy a directory tree (depth limit 32). If `dst` is an existing directory, the source is placed inside it as `dst/<basename>`. `cp` overwrites an existing destination file. Refuses to copy a directory into itself or a descendant. |
| `mv` | `<src> <dst>` | Move or rename a file or directory. If `dst` is an existing directory, the source is moved inside it as `dst/<basename>`. An existing destination file is replaced; an existing destination directory is refused. Moving a directory updates its `..` entry and refuses to move a directory into its own subtree. |
| `rm` | `<file>` | Delete a file (files only; use `rmdir` for directories). |
| `cd` | `[dir]` | Change the current directory (default `/`). The directory must exist on the mounted filesystem. |
| `mkdir` | `<dir>` | Create a directory. Fails if the name already exists. |
| `rmdir` | `[-f] <dir>` | Remove an empty directory. With `-f`, recursively delete a non-empty directory tree. Refuses to remove the current directory or one of its parents. |
| `run` | `<elf32-file>` | Read an ELF32 executable from disk and run it as a Ring 3 process. The file is loaded into a 1 MiB kernel buffer first, so files are limited by `FAT32_MAX_FILE_SIZE`. |

## Notes and limitations

- There is no `rm -r`; recursive deletion is `rmdir -f <dir>`.
- `write` overwrites the whole file; there is no append mode.
- `cp -r` enumerates each directory into a heap-allocated list before copying
  because `fat32_list_directory` iterates a shared static buffer.
- Command output goes to both the VGA screen and the serial port for disk and
  process commands, which makes sessions scriptable over the serial socket.
- Typing `help` inside the shell is the authoritative list; this document
  mirrors `cmd_table` in `src/shell/shell.c`.
