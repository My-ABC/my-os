# MyOS Documentation

Design notes, ABI references, and command references for the kernel.
English documents are the default; `-zh` suffixed files are the Chinese
translations and link back to their English counterpart.

| Document | Description |
|---|---|
| [README-zh.md](README-zh.md) | Chinese translation of the root `README.md` project overview (a copy also lives at the repo root next to `README.md`). |
| [syscall_abi.md](syscall_abi.md) / [syscall_abi-zh.md](syscall_abi-zh.md) | `int 0x80` system-call ABI: call numbers, argument registers, sub-operations, and known limitations. |
| [shell_command.md](shell_command.md) / [shell_command-zh.md](shell_command-zh.md) | Interactive shell command reference: every command, arguments, path semantics, and limitations. |

`README.md` (English overview) and `TODO.md` / `TODO-zh.md` (the bilingual
roadmap) stay at the repository root so GitHub renders the README on the
repo page and the roadmap stays easy to find.
