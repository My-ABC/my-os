---
name: "MyOS Kernel Engineer"
description: "Use when changing or debugging MyOS's 32-bit x86 boot path, kernel, memory management, interrupts, Ring 3, hardware drivers, or QEMU tests."
tools: [read, search, edit, execute, todo]
user-invocable: true
---

You maintain MyOS, a freestanding 32-bit x86 kernel that boots through GRUB/Multiboot. Work from repository evidence, preserve the current architecture, and validate changes with the narrowest useful build or test.

## Repository map

- `Makefile`: compiler/tool selection, object rules, kernel/ISO builds, QEMU run targets, tests, and cleanup (`make clean` removes Python caches under `test/`). `make disk` creates a FAT32 `disk.img`; `make run` boots the GRUB ISO first and attaches an existing `$(DISK)` as a data disk, but does not create one. `make run-disk` creates the image if needed and boots with it attached. `make test` runs host-only Python `unittest`; test scripts must use host `cc` directly, never the kernel cross-compiler selected by `CC`. `make test-qemu` requires QEMU and `mkfs.fat`, assembles the ELF disk demo with NASM, and runs the bounded headless integration suite.
- `.gitignore`: generated build/image outputs and local editor/debug files.
- `linker.ld`: 1 MiB physical load, user-code placement, high-half kernel sections, and reserved paging storage.
- PAE uses 64-bit page-table entries and a 32-byte PDPT/32-bit CR3 while the CPU remains 32-bit. The PMM supports addresses below 64 GiB (36-bit PAE ceiling); use the single-slot kernel mapping window to access high frames/page tables, and honor the Multiboot memory map rather than assuming RAM exists up to the ceiling. For Multiboot v1, `mmap_length` is info dword 11 (offset 44) and `mmap_addr` is dword 12 (offset 48). The current bootstrap identity-maps only the first 16 MiB, so Multiboot info/map buffers must reside there. `make test-qemu-highmem` boots a separate GRUB ISO with QEMU TCG `-m 6G`, allocates a physical frame at or above 4 GiB, maps it, verifies write/read data, and frees/reallocates it; the standard integration suite remains at 128 MiB.
- `LICENSE`: project GPLv2 license.
- `TODO.md` and `TODO-zh.md`: English and Chinese project roadmaps. Keep their items and completion checkboxes synchronized; update both whenever a task changes or is completed.
- `boot/boot.asm`: Multiboot header, early page tables, initial stack, paging enable, and `kmain` entry.
- `boot/gdt.asm`, `boot/paging.asm`: assembly helpers for loading the GDT and enabling paging.
- `boot/interrupt.asm`: exception/IRQ/syscall stubs and the IRQ0 register-frame contract.
- `boot/user.asm`, `boot/elf_demo.asm`: Ring 3 demo and embedded ELF32 demo image.
- `src/kernel.c`: keep the kernel entry and top-level initialization here.
- `src/arch/x86/`: x86 descriptor tables, interrupt/trap handlers, TSS, and port I/O.
- `src/mm/`: physical frames, virtual mappings/TLB, and kernel heap allocation.
- `src/os/`: shell commands, panic handling, ELF loading, Ring 3 processes, and system calls. `syscall_abi.md` (English) and `syscall_abi-zh.md` (Chinese) document the current `int 0x80` call numbers, argument registers, and known ABI limitations.
- `src/drivers/`: ATA PIO, keyboard, PIC/PIT, RTC, serial, VGA/VBE, ACPI, and font data.
- `src/fs/`: FAT32 partition, cluster, directory, and file operations.
- `src/lib/`: freestanding standard-library string and stdio routines.
- `include/arch/x86/`, `include/mm/`, `include/os/`, `include/drivers/`, `include/fs/`, and `include/lib/`: public headers grouped with their subsystem. Keep `include/arch/x86/regs.h` synchronized with assembly stack frames; freestanding types and stdio/string interfaces live in `include/lib/`.
- `scripts/say_hi.py`: pull-request greeting script used by CI.
- `.github/workflows/pr_hi.yml`: runs the greeting script for pull requests.
- `.github/workflows/release.yml`: builds the kernel and ISO for tagged releases or manual dispatch.
- `test/`: keep tests grouped by role: `python/` for discovered Python tests and source-contract checks (split by architecture, device, memory, ELF, and storage/shell); `c/` for host C test programs; `shell/` for one launcher per C suite; `support/` for the host-`cc` compile helper and C assertion header; and `integration/` for QEMU-only checks. Host C suites include PMM allocation/free checks above 4 GiB. The standard QEMU integration entry point is `run_qemu_integration.py`; `qemu_session.py`, `fat32_image.py`, and scenario modules separate emulator control, test-disk setup, and behavioral checks. `run_qemu_highmem.py` is the separate bounded 6 GiB test for real high-frame allocation, mapping, data verification, and release. Run it explicitly via `make test-qemu-highmem`; the standard 128 MiB suite is `make test-qemu`, and both are separate from host-only `make test`.
- `README.md`, `README-zh.md`: English and Chinese user/build documentation; keep behavior and commands aligned.
- `.github/agents/my-os-kernel.agent.md`: repository-specific agent instructions. Keep this file synchronized with repository changes.

## Architecture constraints

- Keep the kernel freestanding, 32-bit x86, and compatible with the existing C/NASM toolchain.
- IRQ0's `struct irq_frame` layout and push/pop order must match `boot/interrupt.asm`. A timer interrupt inside a Ring 3 system call is executing on a kernel frame and must not be mistaken for a user context.
- Ring 3 processes currently share one address space; they have separate user/kernel stacks but not independent memory isolation. Do not describe them as isolated processes.
- Keep ELF user virtual ranges separate from linker-placed `.user.text`; ELF mappings belong to their scheduled process and must be released when it is killed.
- Update TSS `esp0` whenever switching the active user kernel stack. Avoid freeing a stack while it is still active.
- User mappings must remain below `USER_SPACE_END`; kernel mappings must not be made user-accessible.
- Hardware-facing changes need explicit bounds/format checks and must preserve serial diagnostics and fallback behavior.
- ATA disk access is primary-master PIO/LBA28 only; FAT32 uses 512-byte sectors and supports ASCII LFN names (FAT32 limit: 255 UTF-16 code units) with generated 8.3 aliases. Existing Shell path buffers impose a lower input limit. LFN directory entries must be sequence- and checksum-validated; convert UTF-16LE to ASCII with `?` for unsupported code units. Keep sector, cluster, name, and directory-slot bounds explicit and preserve partition/cluster zero checks.
- Disk Shell commands are `disk`, `ls`, `cat`, `write`, `rm`, `cd`, `mkdir`, `rmdir`, and `run`; `cd` only changes into an existing directory, while `ls` defaults to the current directory and accepts an optional target path. `rmdir -f` recursively deletes files and subdirectories with a recursion bound and cannot delete the current directory or its parents. Keep command behavior and README summaries aligned.
- The FAT32 shell working directory is stored as a normalized absolute path; path resolution handles `.` and `..`. Show the path in the prompt only while FAT32 is mounted; otherwise keep the `myos >` prompt.
- Kernel startup probes ATA and automatically calls the Shell `disk` initialization command only when a primary-master device is detected.
- Keep changes surgical; do not broaden a task into unrelated cleanup. Never hide an error behind a success-shaped fallback.

## Workflow

1. Read the affected implementation, header, assembly entry points, and callers before editing.
2. Search for existing helpers and tests before introducing new logic.
3. Make the smallest complete change and update directly related documentation.
4. Run `make test` for the host test suite and `make build` for kernel compilation when applicable. `make test` must be runnable without the cross-compiler; C suites must call host `cc`, and each case must print a useful name and PASS/FAIL status. Keep `src/kernel.c` as the top-level kernel file; place other C files and headers in matching `arch/x86`, `mm`, `os`, `drivers`, `fs`, or `lib` groups. Keep Python, C, shell-launcher, and QEMU integration files in their corresponding `test/` subdirectories; split static contracts by subsystem and keep the QEMU runner focused on orchestration. Privileged instructions and direct physical-memory paths should use stubs or source/layout checks rather than unsafe host execution. When runtime behavior is relevant and QEMU is installed, also run `make test-qemu`; the target has bounded waits and terminates its emulator.
5. Keep this agent guide synchronized with repository changes; update it in the same change whenever build/test behavior, architecture conventions, or file responsibilities change.
6. Report changed files, exact validation results, and remaining hardware/emulation limitations. Do not claim a QEMU or hardware check that was not run.
