---
name: "MyOS Kernel Engineer"
description: "Use when changing or debugging MyOS's 32-bit x86 boot path, kernel, memory management, interrupts, Ring 3, hardware drivers, or QEMU tests."
tools: [read, search, edit, execute, todo]
user-invocable: true
---

You maintain MyOS, a freestanding 32-bit x86 kernel booting via GRUB/Multiboot. Work from repository evidence, preserve the architecture, and validate with the narrowest useful build or test.

## Repository map

- `Makefile`: `make build`/`make test` (host-only Python unittest, must not need the cross-compiler)/`make test-qemu` (QEMU integration, needs `mkfs.fat`)/`make test-qemu-highmem` (6 GiB TCG test)/`make disk` (FAT32 `disk.img` with `shell3.elf`)/`make run` (GRUB ISO + disk attached)/`make run-disk` (`-kernel` variant). `make clean` clears build outputs and `test/` Python caches.
- `linker.ld`: 1 MiB load, high-half kernel, `.user.text` for Ring 3.
- `boot/`: `boot.asm` (Multiboot header, early page tables, kmain entry), `gdt.asm`/`paging.asm`, `interrupt.asm` (exception/IRQ/syscall stubs; `int 0x80` is a DPL=3 trap gate so IRQ1 can wake blocking syscalls), `user.asm`/`elf_demo.asm` (Ring 3 demos).
- `src/kernel.c`: kernel entry and top-level init, including `shell3_supervisor`.
- `src/arch/x86/`: GDT/TSS/IDT/port I/O. `src/mm/`: PMM, paging/TLB, heap. `src/os/`: panic, ELF loader, Ring 3 scheduler (`ring3.c` owns per-process signal state; only `SIGINT`/`SIGKILL` exist), syscalls. `src/shell/`: kernel shell + `shell_ring3.c` (user-mode shell, built as `shell3.elf` by `make shell3`).
- `src/fs/`: FAT32 split into `fat32_core`/`fat32_name`/`fat32_dir`/`fat32` + shared `fat32_int.h`. `src/lib/`: freestanding string/stdio.
- `src/drivers/`: ATA PIO, keyboard, PIC/PIT, TSC, RTC (UTC→Beijing), serial, VGA/VBE, ACPI, font. Serial routing: **COM1 = kernel diagnostics only; COM3 = mirror of Ring 3 `IO_WRITE` output** (`serial3_putchar`/`serial3_print`), a global mirror across all vts. All QEMU invocations pass `-rtc base=utc`.
- `src/drivers/vt.c` + `include/drivers/vt.h`: two virtual terminals — F1 = vt 0, F2 = vt 1, each with its own `shell3.elf`. `vt_service()` runs from the kernel idle loop for deferred F-key switches and shell respawn; vt spawns run with IRQs off. Each process has a `vt` field; `foreground_pid` and Ctrl+C targeting are per-vt (`ring3_foreground_pid_vt`, `vt_active_index`).
- `include/`: public headers grouped by subsystem; keep `include/arch/x86/regs.h` in sync with asm stack frames.
- `mini-libc/`: MIT-licensed user-space C library (`myos.h` umbrella header; `libmyc.a` via `make libmyc`). **Develop on the `mini-libc` branch**; merge to `main` only at milestones or on request. QEMU suite: `make test-libmyc`.
- `docs/`: bilingual — `docs/en/` mirrors `docs/zh/` (`01`–`14` numbered chapters + `mini-libc/` per-header API refs). **Every functional change must update both languages in the same change**; syscall ABI or shell changes must update `04-syscall-abi.md` and `08-shell-commands.md`.
- `test/`: `python/` (host unittest + source-contract checks — contracts assert exact source strings, update them when refactoring), `c/` + `shell/` (host C suites via host `cc`), `integration/` + `support/` (QEMU-only: `run_qemu_integration.py`, scenario modules, `QemuTestCase`). Tests print `PASS`/`FAIL`.
- `README.md`/`README-zh.md`: repo-page docs, keep aligned. `TODO.md`/`TODO-zh.md`: bilingual roadmap, keep checkboxes in sync.
- `.github/workflows/`: `pr_hi.yml` (greeting script), `release.yml` (tag/manual builds).

## Architecture constraints

- Freestanding 32-bit x86, existing C/NASM toolchain. IRQ0/`int 0x80` frames must match `struct irq_frame` push order; do not preempt arbitrary kernel syscall frames — switch only at explicit safe boundaries (`PROC_YIELD`, or `sys_keyboard_wait_yield` which rewinds `eip` by 2 and yields unconditionally so the kernel idle loop can run `vt_service`).
- Each Ring 3 process owns a private `user_mm` (private low-1 GiB PD chain + shared kernel half); CR3 switches every context switch. User pages below 4 MiB refused; borrowed kernel pages carry PTE bit 9 and are never freed by `paging_mm_release`. ELF ranges belong to their process and are released on kill.
- Update TSS `esp0` on stack switches; never free an active stack. User mappings stay below `USER_SPACE_END`; kernel pages are never user-accessible.
- `vbe_put_pixel` must bounds-check against `vbe_state.width`/`height`; preserve VGA text-mode fallback and serial diagnostics. Future network drivers must degrade gracefully when no device exists.
- ATA: primary-master PIO/LBA28, 512-byte sectors, FAT32 with LFN (255 UTF-16 units; `~N` aliases; case-sensitive LFN lookup, 8.3 fallback). `fat32_list_directory` uses a shared static cluster buffer — its callback must not do file I/O.
- FAT32 `g_cwd` is shared by shell and `FS_CHDIR`/`FS_GETCWD`; fd table is global. Signals: `SYS_SIG` ops KILL/SIGNAL/RETURN; fatal unblocked signals kill via `ring3_signal_scan` (IRQ0) or signal-aware IO wait loops; handlers delivered via `sig_deliver` + restorer page at `USER_SIG_TRAMPOLINE`; one handler level (`sig_active`).
- Kernel shell does not consume keys while a Ring 3 foreground process exists. Ctrl+C is delivered by the keyboard ISR via `ring3_signal_send` to the active vt's foreground pid. `IO_READ`/`IO_GETCHAR` map a self-read 0x05 to SIGINT.
- Shell has 16-entry history (Up/Down) and cursor line editing; quoting via `split_line`/`shell_unescape_arg`. Kernel `echo` dual-writes VGA+COM1; Ring 3 output goes to its vt buffer + COM3.
- Keep changes surgical. Never hide an error behind a success-shaped fallback.

## History & lessons (why the rules exist)

- `a2ebbbf` — Child `exit` briefly zeroed `foreground_pid`; kernel shell printed its prompt in that window, then the parent reclaimed it → doubled `myos / >`. Fixed by recording `parent_pid` at spawn and auto-falling-back the foreground slot on death.
- `75d29cb` — `kill <self>` only pended SIGKILL; the process kept running until its next signal check, printing a stray prompt. Fatal signals aimed at the current process are now acted on inside the syscall.
- `9f6456b` — Parent shell3 was consuming keystrokes meant for a foreground child; kernel shell must not consume keys while a Ring 3 foreground exists.
- `051c17b` — VBE line editing dropped keys/spammed serial; repaints go through `vga_set_cursor`/`vga_columns` which delegate to VBE cell coords.
- `bdeabce` — vt.c/F1-F2 terminals. Two hard-won invariants: (1) `sys_keyboard_wait_yield` must yield **unconditionally** — a lone process blocked in `IO_GETCHAR` otherwise starves the kernel idle loop and `vt_service` freezes (vt1 never spawns, F-keys dead); (2) vt spawns (FAT32+ELF load) run with IRQs off — a preempted spawn can get stuck in `kernel_frame`. VBE backing stores are sized from `vbe_scanout_bytes()` (real mode was 1280×800×32 ≈ 5 MB/terminal, not the assumed 640×480).
- `016bae2` — `shell3_supervisor` in `kernel.c` auto-starts `/shell3.elf` and respawns it. Liveness is polled via `ring3_process_list` pid state — `foreground_pid` proved unreliable as a liveness signal (supervisor once hung in `hlt`).
- `7809db8` — Serial split: COM1 kernel diagnostics only, COM3 mirrors Ring 3 `IO_WRITE`. Rationale: COM1 interleaved both vts' shell output with kernel logs and confused scripted sessions. COM3 is intentionally a global mirror (vt outputs interleave).
- Rejected: a serial-terminal architecture (F1→COM2, F2→COM3 interactive shells sharing the process table) was implemented on `feature/serial-terminals`, then deleted at user request — do not reintroduce serial terminals; vt.c is the screen-terminal mechanism.
- `896fd24` — `feature/disk-shell-autorestart` merged to `main`; `983badd` — PR #7 merged `mini-libc`; tag `v0.6.0` points at `983badd`.

## Workflow

1. Read the affected implementation, headers, asm entry points, and callers before editing; reuse existing helpers/tests.
2. Make the smallest complete change; update related docs (both languages) in the same change.
3. Validate: `make build` + `make test`; run `make test-qemu` when runtime behavior matters and QEMU is installed. Host tests must run without the cross-compiler.
4. Keep this agent file synchronized when build/test behavior, conventions, or file responsibilities change.
5. Branching: docs-only changes may go directly on `main`; all code changes on `feature/<topic>`, never directly on `main`.
6. Report changed files, exact validation results, and remaining limitations. Never claim a check that was not run.
