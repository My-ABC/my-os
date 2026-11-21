# 10 — Build and Test

English | [中文](../zh/10-build-and-test.md)

## Build

`Makefile` auto-collects sources:

- `C_SRCS = $(shell find src -type f -name '*.c')` — new `.c` files under
  `src/` are picked up automatically; objects land in `build/` mirroring
  the source path.
- `ASM_SRCS` covers `boot/*.asm` (NASM); `boot/gdt.asm` also produces
  `gdt_asm.o`.
- `INCLUDE_DIRS` lists every `include/` subdirectory; keep it in sync
  when adding a new header directory (e.g. `include/shell/`).
- Link: `ld -m elf_i386 -T linker.ld -nostdlib` → `myos.bin`
  (entry `start`, kernel VMA `0xC0000000`, physical load 1 MiB).
- `make iso` wraps `myos.bin` into `myos.iso` via GRUB (`grub-mkrescue`).

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
monitor `sendkey`, and reads output over the serial TCP socket
(`qemu_session.py`). Scenario modules: `shell_scenarios.py` (boot,
storage, `cp`/`mv`/`cp -r`, LFN case), `ring3_scenarios.py`,
`page_fault_scenarios.py` (`stack.elf`/`faultu.elf`/`faultp.elf` from
`fault_demo.asm`). A passing run ends with `PASS: QEMU integration
suite` and `ALL PASS`; `DEFAULT_TIMEOUT` is 150 s.

## High-memory test (`make test-qemu-highmem`)

Separate bounded run with 6 GiB guest RAM exercising
`pmm_alloc_page_at_or_above(4 GiB)` and PAE mapping/verification. Only
run on hosts with enough memory.
