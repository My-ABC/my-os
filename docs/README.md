# MyOS Documentation

Design notes, ABI references, and command references for the kernel.
English documents are the default; `-zh` suffixed files are the Chinese
translations and link back to their English counterpart.

| Document | Description |
|---|---|
| [syscall_abi.md](syscall_abi.md) / [syscall_abi-zh.md](syscall_abi-zh.md) | `int 0x80` system-call ABI: call numbers, argument registers, sub-operations, and known limitations. |
| [shell_command.md](shell_command.md) / [shell_command-zh.md](shell_command-zh.md) | Interactive shell command reference: every command, arguments, path semantics, and limitations. |

Project-level documents stay at the repository root: `README.md` /
`README-zh.md` for an overview and `TODO.md` / `TODO-zh.md` for the roadmap.
