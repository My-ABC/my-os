# 11 — Build and Test

English | [中文](../zh/11-build-and-test.md)

## Build

`Makefile` auto-collects sources:

- `C_SRCS` auto-collects `src/**/*.c` **except** `src/shell/shell_ring3.c`
  (built separately into `shell3.elf`); `ASM_SRCS` covers `boot/*.asm`
  **except** `boot/gdt.asm`/`boot/paging.asm`, which are assembled with
  special flags into `gdt_asm.o`/`paging_asm.o`.
- `INCLUDE_DIRS` lists every `include/` subdirectory; keep it in sync
  when adding a new header directory (e.g. `include/shell/`).
- Link: `ld -m elf_i386 -T linker.ld -nostdlib` → `myos.bin`
  (entry `start`, kernel VMA `0xC0000000`, physical load 1 MiB).
- `make iso` wraps `myos.bin` into `myos.iso` via GRUB (`grub-mkrescue`).
- `make disk` builds `shell3.elf` (via `libmyc` + `shell3` targets) and
  writes it into `disk.img` next to the test ELFs; an existing image is
  only reused when `shell3.elf` is older **and** its root-directory
  entry is still present (`root_file_present` in
  `test/integration/fat32_image.py`), so a deleted/corrupted entry
  forces a rebuild; `make run` depends on
  `iso disk` and attaches the image unconditionally, so the supervisor
  auto-starts the Ring 3 shells. Other run targets: `run-kernel`,
  `run-nographic`, `run-serial`, `run-disk`, `run-q35`, `run-q35-nographic`.
- `make test-libmyc` runs the mini-libc QEMU suite.

Common flags: `-m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -g`
plus `-MMD -MP` dependency tracking. Optional defines: `PANIC_DEMO`,
`ELF_DEMO`, `KEYBOARD_DEMO`, `SCANCODE_SET=n`,
`PMM_QEMU_HIGHMEM_TEST`.

## Host tests (`make test`)

- `test/python/` — unittest-discovered contract tests that read kernel
  sources and assert structural invariants (architecture, memory, ELF,
  storage/shell). `test_storage_shell_contracts.py` concatenates the
  split FAT32 sources in dependency order before matching function
  bodies.
- `test/c/` + `test/shell/` — host-compiled C suites launched by shell
  scripts via `test/support/compile_c_test`.

## QEMU integration (`make test-qemu`)

`test/integration/run_qemu_integration.py` boots `myos.bin` under
`qemu-system-i386` (128 MiB) with a generated FAT32 test disk
(`fat32_image.py` via `mkfs.fat`), drives the shell through the QEMU
monitor `sendkey`, and reads output over **two** serial TCP sockets —
COM1 kernel diagnostics and COM3 Ring 3 `IO_WRITE` mirror
(`qemu_session.py`, COM2 is `-serial none`). Scenario modules:
`shell_scenarios.py` (boot, storage, `cp`/`mv`/`cp -r`, LFN case),
`ring3_scenarios.py`, `page_fault_scenarios.py`
(`stack.elf`/`faultu.elf`/`faultp.elf`/`oom.elf`/`kfault.elf` from
`fault_demo.asm`). A passing run ends with `PASS: QEMU integration
suite` and `ALL PASS`; `DEFAULT_TIMEOUT` is 150 s. Two QEMU sessions
are used: the main suite ends with the user-heap OOM kill (heap blocks
leak on process death, so no later check may need fresh pages), and a
dedicated second session verifies that a kernel-mode page fault panics
(`kfault.elf` passes a bad pointer to `MEMORY_REALLOC`, which still
dereferences it unchecked), since the panic reboots the machine.

## High-memory test (`make test-qemu-highmem`)

Separate bounded run with 6 GiB guest RAM exercising
`pmm_alloc_page_at_or_above(4 GiB)` and PAE mapping/verification. Only
run on hosts with enough memory.
