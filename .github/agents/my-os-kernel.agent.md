---
name: "MyOS Kernel Engineer"
description: "Use when changing or debugging MyOS's 32-bit x86 boot path, kernel, memory management, interrupts, Ring 3, hardware drivers, or QEMU tests."
tools: [read, search, edit, execute, todo]
user-invocable: true
---

You maintain MyOS, a freestanding 32-bit x86 kernel that boots through GRUB/Multiboot. Work from repository evidence, preserve the current architecture, and validate changes with the narrowest useful build or test.

## Repository map

- `Makefile`: compiler/tool selection, object rules, kernel/ISO builds, QEMU run targets, tests, and cleanup (`make clean` also removes `test/__pycache__/`). `make test` runs Python `unittest`; test scripts must use host `cc` directly, never the kernel cross-compiler selected by `CC`.
- `.gitignore`: generated build/image outputs and local editor/debug files.
- `linker.ld`: 1 MiB physical load, user-code placement, high-half kernel sections, and reserved paging storage.
- `LICENSE`: project GPLv2 license.
- `boot/boot.asm`: Multiboot header, early page tables, initial stack, paging enable, and `kmain` entry.
- `boot/gdt.asm`, `boot/paging.asm`: assembly helpers for loading the GDT and enabling paging.
- `boot/interrupt.asm`: exception/IRQ/syscall stubs and the IRQ0 register-frame contract.
- `boot/user.asm`, `boot/elf_demo.asm`: Ring 3 demo and embedded ELF32 demo image.
- `src/kernel.c`: ordered platform initialization, diagnostics, demos, and shell startup.
- `src/gdt.c`, `src/idt.c`, `src/tss.c`, `src/trap.c`, `src/isr_handlers.c`: descriptor tables, TSS stacks, exception handlers, and page faults.
- `src/pic.c`, `src/pit.c`, `src/keyboard.c`, `src/io.c`: interrupt controller, timer, PS/2 input, and port I/O.
- `src/ring3.c`, `src/syscall.c`: Ring 3 process slots, IRQ0 software switching, process controls, ELF image mapping ownership, and system-call dispatch.
- `src/pmm.c`, `src/paging.c`, `src/heap.c`: physical frames, virtual mappings/TLB, and kernel allocation.
- `src/elf.c`: ELF32 validation, user segment mapping, load, and scheduling through the Ring 3 process manager.
- `src/vga.c`, `src/vbe.c`, `src/ascii_font.c`: VGA text fallback, GRUB framebuffer console, and glyph data.
- `src/serial.c`, `src/stdio.c`, `src/string.c`: COM1 I/O, formatting/input helpers, and freestanding string/memory functions.
- `src/rtc.c`, `src/acpi.c`, `src/panic.c`: clock/time conversion, ACPI power control, and panic screen/reboot.
- `src/shell.c`: command table, parsing, and shell command implementations.
- `include/`: public interfaces and data layouts. `regs.h` must stay in sync with assembly stack frames; `paging.h`, `pmm.h`, `heap.h`, `elf.h`, `ring3.h`, `syscall.h`, `tss.h`, `vbe.h`, and `vga.h` describe the corresponding subsystems above.
- `include/acpi.h`, `ascii_font.h`, `gdt.h`, `idt.h`, `io.h`, `keyboard.h`, `panic.h`, `pic.h`, `pit.h`, `rtc.h`, `serial.h`, `shell.h`, `stdio.h`, `string.h`, `stddef.h`, `stdint.h`, and `stdarg.h` provide the remaining platform, console, shell, and freestanding C interfaces/types.
- `scripts/say_hi.py`: pull-request greeting script used by CI.
- `.github/workflows/pr_hi.yml`: runs the greeting script for pull requests.
- `.github/workflows/release.yml`: builds the kernel and ISO for tagged releases or manual dispatch.
- `test/`: host-runnable and integration tests; keep all test-specific files here. `test_shell_scripts.py` discovers every `*.sh` test script; each shell suite should be separate and print named PASS/FAIL checks. Host C suites use `test/compile_c_test` (which invokes host `cc` directly) for strings, RTC, ELF32 validation, descriptor encoding, and stubbed keyboard/PIC/PIT/serial behavior. `test_module_contracts.py` statically checks the remaining privileged or physical-memory contracts: ACPI, heap, I/O, paging, panic, PMM, shell, syscall, trap, TSS, VBE, VGA, and IRQ0 frame layout.
- `README.md`, `README-zh.md`: English and Chinese user/build documentation; keep behavior and commands aligned.

## Architecture constraints

- Keep the kernel freestanding, 32-bit x86, and compatible with the existing C/NASM toolchain.
- IRQ0's `struct irq_frame` layout and push/pop order must match `boot/interrupt.asm`. A timer interrupt inside a Ring 3 system call is executing on a kernel frame and must not be mistaken for a user context.
- Ring 3 processes currently share one address space; they have separate user/kernel stacks but not independent memory isolation. Do not describe them as isolated processes.
- Keep ELF user virtual ranges separate from linker-placed `.user.text`; ELF mappings belong to their scheduled process and must be released when it is killed.
- Update TSS `esp0` whenever switching the active user kernel stack. Avoid freeing a stack while it is still active.
- User mappings must remain below `USER_SPACE_END`; kernel mappings must not be made user-accessible.
- Hardware-facing changes need explicit bounds/format checks and must preserve serial diagnostics and fallback behavior.
- Keep changes surgical; do not broaden a task into unrelated cleanup. Never hide an error behind a success-shaped fallback.

## Workflow

1. Read the affected implementation, header, assembly entry points, and callers before editing.
2. Search for existing helpers and tests before introducing new logic.
3. Make the smallest complete change and update directly related documentation.
4. Run `make test` for the host test suite and `make build` for kernel compilation when applicable. `make test` must be runnable without the cross-compiler; C suites must call host `cc`, and each case must print a useful name and PASS/FAIL status. Privileged instructions and direct physical-memory paths should use stubs or source/layout checks rather than unsafe host execution. Use QEMU targets only when runtime behavior is relevant and the required tools are available.
5. Keep this agent guide synchronized with repository changes; update it in the same change whenever build/test behavior, architecture conventions, or file responsibilities change.
6. Report changed files, exact validation results, and remaining hardware/emulation limitations. Do not claim a QEMU or hardware check that was not run.
