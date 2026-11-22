# MyOS Project TODO

This document tracks completed work and the project roadmap. When an item is completed, update both this file and [TODO-zh.md](./TODO-zh.md), and keep the English and Chinese READMEs and the kernel agent guide in sync.

## Completed

### Kernel, Memory, and Program Execution

- [x] Established the 32-bit x86 Multiboot/GRUB boot flow, including GDT, IDT, TSS, and basic interrupt handling.
- [x] Implemented kernel paging, PAE page tables, and high-memory mapping. The PMM supports physical addresses below 64 GiB, has host tests for page allocation above 4 GiB, and the optional QEMU `-m 6G` test allocates a real page at `0x100000000`, maps it, verifies writes and reads, then frees and reallocates it.
- [x] Implemented the kernel heap allocator and a basic user heap allocation interface.
- [x] Implemented basic Ring 3 process scheduling and process-management commands; at most two processes can run, and they share an address space.
- [x] Added the basic `IO_WRITE` and `PROC_GETPID` system calls.
- [x] Implemented ELF32 loadable-segment validation/loading and BSS zeroing, plus built-in and disk-based ELF program execution.
- [x] Fixed a possible infinite reboot after PAE changes on the GRUB ISO boot path; build rules now track header dependencies to avoid reusing stale objects after ABI changes.
- [x] Reorganized the syscall ABI into two-level class/sub-operation numbering: `eax` selects one of seven classes (`SYS_IO`/`SYS_TIME`/`SYS_MEMORY`/`SYS_POWER`/`SYS_PROCESS`/`SYS_FS`/`SYS_SIG`), `ebx` the sub-operation, and arguments moved to `ecx`/`edx`/`esi`; added `TIME_TO` for UTC±offset conversion and updated the sigreturn trampoline, all asm demos, contract tests, and bilingual ABI docs.

### ATA, FAT32, and Shell

- [x] Implemented ATA primary-master PIO/LBA28 sector access and startup device detection; the Shell omits the disk-path prompt when no disk is detected.
- [x] Implemented FAT32 mounting and file/directory operations, with automatic mount attempts when an ATA disk is detected.
- [x] Implemented FAT32 ASCII long file names (LFN) and 8.3 aliases, including names such as `help_aaaaa.1234`, with LFN sequence and checksum validation.
- [x] Implemented the `utf16LE_to_ascii` and `ascii_to_utf16LE` conversion interfaces.
- [x] Implemented the Shell's current-path prompt and navigation through existing directories, including `cd ..`; `cd` does not create directories, and `ls` defaults to the current directory while accepting an optional path.
- [x] Implemented `rmdir -f` to recursively remove non-empty directories while protecting the current directory and its parents.
- [x] Implemented `cp`/`cp -r`/`mv` file and directory operations with Linux-style destination semantics.
- [x] Implemented the `run` command to load and execute an ELF32 program from a FAT32 disk.
- [x] Added filesystem syscalls `FS_OPEN`/`CLOSE`/`READ`/`WRITE`/`UNLINK`/`MKDIR`/`RMDIR`/`STAT`/`GETCWD`/`CHDIR` (`SYS_FS` ops 1–10), `PROC_EXIT`/`PROC_YIELD` (`SYS_PROCESS`), `TIME_GET` (`SYS_TIME`), and signal syscalls `SIG_KILL`/`SIG_SIGNAL`/`SIG_RETURN` (`SYS_SIG`); user pointers are validated against registered ranges and paths resolve through the shared `fat32_resolve_path`/`g_cwd`.
- [x] Implemented signal delivery end to end: `SIGINT`/`SIGKILL` only, per-process pending/blocked/handler state, Ctrl+C `KEYBOARD_KEY_SIGINT` → foreground `SIGINT`, fatal-default termination, and user handler invocation via a per-process restorer trampoline at `0x00900000` plus `SIG_RETURN` context restore.
- [x] Implemented shell line editing: 16-entry history (Up/Down), cursor movement (Left/Right), insertion and Backspace editing with full-line repaint, double-quoted arguments, `\n`/`\\`/`\"` escapes with unknown-escape warnings, Ctrl+C `^C` echo + line discard + prompt redraw, and differentiated ELF/`run` error messages (bad image vs. process slots full vs. read failure).
- [x] Made `echo` write to both the screen and COM1 serial so scripted/headless sessions keep serial visibility; removed the `secho` command.
- [x] Added a unittest-style QEMU integration framework (`QemuTestCase` with `assert*` helpers, per-method PASS lines) alongside the existing bounded session orchestration; check results are marked with `✓ PASS`/`✗ FAIL`.
- [x] Fixed RTC 12/24-hour handling: PM flag is extracted before BCD decode, decoded hours >12 fall back to 24-hour values for RTCs with unreliable mode bits, and all QEMU invocations pass `-rtc base=utc` so the guest RTC reads UTC for the UTC→Beijing conversion. `time` accepts `time 12`/`time 24`.
- [x] Guarded `vbe_put_pixel` against out-of-bounds coordinates (serial diagnostic instead of an intermittent kernel panic).

### Drivers, Console, and Power

- [x] Implemented the basic platform drivers: PIC interrupt-controller setup, the PIT IRQ0 timer, PS/2 keyboard scancode decoding (including Ctrl+C `KEYBOARD_KEY_SIGINT` injection), the VGA text-mode console, VBE framebuffer initialization from GRUB multiboot info with bounded pixel drawing, COM1 serial output, and the RTC.
- [x] Implemented ACPI table discovery and FADT parsing, including S5 shutdown through the PM1a control block with graceful fallback when tables or registers are unavailable.

### Project Structure, Tests, and Documentation

- [x] Organized source and header files while keeping `src/kernel.c` as the kernel entry point and grouping code under `arch/x86`, `mm`, `os`, `drivers`, `fs`, and `lib`.
- [x] Organized tests by purpose, separating Python tests, host C tests, Shell launchers, and QEMU integration tests; added the optional `make test-qemu-highmem` test for real high-page allocation and access.
- [x] Added host and QEMU integration tests covering PAE/PMM, high physical-page allocation and access, FAT32 LFN, Shell operations, Ring 3 processes, and disk ELF execution.
- [x] Created the project roadmap and documented its entry points in both READMEs and the kernel agent guide.
- [x] Created the bilingual architecture documentation series under `docs/en` and `docs/zh` (numbered chapters from boot through signals, plus driver-gap analysis and the memory-isolation chapter), with synchronized language links.

## Kernel, User Space, and System Calls

- [x] **Complete ISR14 page-fault handling**
  - [x] Distinguish user- and kernel-mode faults using the error code and interrupted privilege level.
  - [x] Demand-map pages only for registered ELF segments, the user heap, and the user stack.
  - [x] Distinguish unmapped pages from protection violations; terminate the process on illegal user access and panic on illegal kernel faults.
  - [x] Zero user pages before mapping and correctly release resources when allocation or mapping fails.
  - [x] Test valid demand mapping, out-of-range access, protection violations, kernel faults, and out-of-memory behavior. (QEMU covers all five via `stack.elf`/`faultu.elf`/`faultp.elf`/`oom.elf`/`kfault.elf`; the kernel-fault panic runs in a dedicated second session because it reboots the machine.)
  - Entry point: [isr14_handler](./src/arch/x86/trap.c).
- [x] **Implement high-precision timed wait**
  - [x] Calibrate the TSC against the PIT and expose a timestamp/delay helper.
  - [x] Add a `SYS_TIME`/`TIME_SLEEP` syscall taking a microsecond duration.
  - [x] Verify sub-millisecond sleep precision and signal checks with QEMU tests.
- [ ] **Complete the user-space and system-call interfaces**
  - [ ] Define a stable, versioned system-call ABI, argument conventions, and error codes. (Two-level class/sub-op numbering in `eax`/`ebx` with args in `ecx`/`edx`/`esi` is documented in `docs/en/04-syscall-abi.md`; no versioning yet.)
  - [ ] Validate user pointers, lengths, and address ranges; provide safe user/kernel data-copy helpers. (FS syscalls validate pointers; `IO_WRITE`/`IO_READ`/`SYS_MEMORY` still dereference caller pointers directly.)
  - [ ] Complete process creation, exit, wait, signal/event, and resource-reclamation interfaces. (`PROC_EXIT`, `PROC_YIELD`, and the signal set exist; no fork/exec/wait or per-process fd table yet.)
  - [ ] Add system calls for file, directory, console, time, and device access. (`SYS_FS` ops 1–10, `TIME_GET`/`TIME_TO`, and console read/write exist; device access is still missing.)
  - [ ] Add host and QEMU tests for the ABI and invalid-argument handling. (QEMU covers happy paths and signal delivery; invalid-argument coverage is partial.)
- [ ] **Implement a signal mechanism** (only `SIGINT`/`SIGKILL` so far; more numbers, `sigprocmask`-style blocking APIs, and nested delivery remain)
  - [x] Define signal numbers, delivery semantics (poll-based at IRQ0/syscall wait points, not synchronous faults), and per-process pending/blocked signal state.
  - [x] Add system calls to send a signal to a process (`SIG_KILL`) and to register/replace a user-mode handler (`SIG_SIGNAL`); `SIG_RETURN` restores the interrupted context after a handler returns.
  - [x] Ensure delivery cannot corrupt the `irq_frame` contract or an in-flight syscall; terminate the process on unhandled fatal signals. (Delivery saves the whole frame and rebuilds it on the user stack; a delivered-into syscall frame is abandoned rather than restarted.)
  - [x] Cover delivery, blocking, and termination with QEMU tests. (`sigdemo.elf` verifies handler invocation, register restore, and continuation; Ctrl+C termination is covered by the Ring 3 scenario.)
- [x] **Implement per-process page tables for memory isolation**
  - [x] Give each Ring 3 process its own user address space (per-process CR3 on context switch) instead of sharing one page table. (Private PDPT[0]->PD per `struct user_mm`, shared kernel PDPT[3]; CR3 switches in `ring3_timer_switch`/yield/terminate.)
  - [x] Classify mappings as per-process (user heap, stacks, ELF segments) vs. shared (kernel half, low 4 MiB supervisor identity map). (Signal trampoline is per-process; `.user.text`/low identity pages are borrowed, marked with PTE bit 9.)
  - [x] Handle TLB invalidation on CR3 switches, page-fault ownership, and releasing per-process mappings on exit. (CR3 reload flushes non-global TLB entries; ISR14 demand-maps only into the current process's mm; `paging_mm_release` frees all owned user pages.)
  - [x] Verify with QEMU tests that one process cannot observe or corrupt another's memory and that kernel mappings stay inaccessible. (Full `make test-qemu` suite passes; isolation is structural via private page tables.)
- [ ] **Implement a kernel diagnostics and logging subsystem (dmesg / ring buffer)**
  - [ ] Add a fixed-size kernel log ring buffer with severity levels and timestamps.
  - [ ] Route kernel diagnostics through the log and add a `dmesg`-style shell command to dump or review it.
  - [ ] Define wraparound behavior, SMP-ready locking, and log-level filtering.
- [ ] **Complete the Shell and migrate it to Ring 3**
  - [ ] Gradually move command parsing, interaction, and built-ins out of the kernel.
  - [ ] Use system calls to access the console, filesystem, processes, and devices.
  - [ ] Provide basic command-line editing, history, error codes, and executable search. (Editing, 16-entry history, `\n`/`\\`/`\"` escapes, and differentiated ELF errors are done; executable search/PATH lookup is not.)
  - [ ] Ensure the kernel can recover control if the user Shell crashes or exits.

## Inter-Process Communication

- [ ] **Implement pipes and IPC primitives**
  - [ ] Add anonymous pipes (`SYS_PIPE`) backed by bounded kernel buffers with blocking read/write semantics.
  - [ ] Support fd redirection so pipelines like `cmd1 | cmd2` work between user processes (requires per-process fd tables, which do not exist yet).
  - [ ] Extend toward named pipes or additional IPC primitives (shared memory, message queues) as needed.
- [ ] **Implement I/O redirection**
  - [ ] Provide fd duplication/redirect support (`dup2`-style) on top of per-process fd tables.
  - [ ] Support `>`, `>>`, and `<` redirection in the shell once user-side command execution exists.

## Filesystem, Storage, and Caches

- [ ] **Complete filesystem and Shell storage workflows**
  - [ ] Systematically review FAT32 path resolution, directory bounds, error propagation, and corrupt-disk handling.
  - [ ] Complete file-handle and open/close/sequential-read/write interfaces for user programs.
  - [x] Add common operations such as copy, move, and rename, with persistence and regression tests. (`cp` copies files and `cp -r` copies directory trees; `mv` renames/moves files and directories with Linux semantics — existing directory destinations receive the source, existing file destinations are replaced, and directory-into-itself moves are rejected.)
  - [x] Document the supported scope and limits for LFN, short-name aliases, and non-ASCII names. (Shell-created entries always carry LFN preserving case; lookups match LFN case-sensitively and fall back to the 8.3 short name only when no LFN exists. See README.)
- [ ] **Implement a block cache**
  - [ ] Add a cache indexed by device and LBA with a replacement policy.
  - [ ] Define consistency, write-back/sync, invalidation, and error-propagation rules.
  - [ ] Use the cache in FAT32 and validate I/O performance and data consistency.
- [ ] **Implement a page cache**
  - [ ] Associate file-content pages with physical-page management, file offsets, and write-back.
  - [ ] Support cache hits, dirty-page write-back, invalidation, and block-cache coordination.
  - [ ] Reuse the page cache for executable loading and file mappings.

## Hardware, Devices, and ACPI

- [ ] **Inventory and enumerate driver-ecosystem gaps**
  - [x] Review the capabilities and limits of existing ATA, keyboard, PIC/PIT, RTC, VGA/VBE, and ACPI drivers. (See `docs/en/12-driver-gaps.md` / `docs/zh/12-driver-gaps.md`.)
  - [x] List missing drivers by bus/device category, with priorities and test devices. (Same document; includes a suggested adoption order.)
  - [ ] Select and implement drivers incrementally based on actual needs; do not promise blanket compatibility in advance.
- [ ] **Establish a device subsystem**
  - [ ] Design common device, bus, and driver registration/probing interfaces.
  - [ ] Add device-resource management, driver matching, lifecycle management, and error recovery.
  - [ ] Evaluate and implement PCI enumeration to support future network-card and other PCI drivers. (Promoted to the dedicated "Implement PCI bus enumeration" item below.)
- [ ] **Implement PCI bus enumeration**
  - [ ] Add PCI configuration-space access (mechanism 1, ports `0xCF8`/`0xCFC`) with read/write helpers.
  - [ ] Enumerate buses/devices/functions, read vendor/device IDs, and class codes; log the bus map over serial.
  - [ ] Discover and assign BARs (memory and I/O windows) without conflicting with existing ISA devices.
  - [ ] Provide basic INTx interrupt routing so PCI devices can signal the PIC/APIC.
  - [ ] Validate against QEMU `-device` fixtures (e1000, ich9-ahci, qemu-xhci, bochs-display) per `docs/en/12-driver-gaps.md`.
- [ ] **Add GPU support**
  - [ ] Enumerate display-class PCI devices and identify GPU candidates (bochs-display, virtio-gpu, VMware SVGA, QXL).
  - [ ] Extend the framebuffer abstraction to cover GPU-provided scanout surfaces.
  - [ ] Evaluate 2D/3D acceleration paths (bochs 2D, virtio-gpu, or a userspace graphics stack); keep the scope experimental.
- [ ] **Complete ACPI support**
  - [ ] Organize ACPI table discovery, mapping, length checks, and checksum validation into bounded common interfaces.
  - [ ] Parse MADT to provide processor and interrupt-controller information.
  - [ ] Evaluate HPET, MCFG, and related hardware support.
  - [ ] Improve power-state and shutdown/reboot error handling, and evaluate AML/DSDT support.

## Concurrency and Memory

- [ ] **Add an idle task and HLT-based power saving**
  - [ ] Run a dedicated idle loop that executes `HLT` when no runnable task exists instead of busy-spinning in the scheduler.
  - [ ] Ensure IRQ0/PIT and other interrupts wake the CPU reliably between sleeps.
  - [ ] Verify wake behavior and reduced power usage with QEMU tests.
- [ ] **Add SSE support**
  - [ ] Enable SSE in CR4/XCR0 as needed and validate availability (CPUID).
  - [ ] Provide aligned/unaligned load-store and arithmetic helpers for kernel and user code.
  - [ ] Add QEMU tests covering SSE context save/restore across context switches.
- [ ] **Implement SMP / multicore support**
  - [ ] Enumerate processors through MADT and start application processors.
  - [ ] Add per-CPU state, Local APIC timers, and inter-processor interrupts.
  - [ ] Add SMP synchronization primitives for the scheduler, memory manager, devices, and caches.
  - [ ] Support cross-core task scheduling and add multicore QEMU regression tests.
  - [ ] Add Hyper-Threading awareness: enumerate logical processors per core via CPUID leaf 0xB/0x1F and avoid treating SMT siblings as fully independent cores for scheduling decisions.
- [ ] **Implement swap and disk paging**
  - [ ] Define swap-space and page-state/ownership management.
  - [ ] Implement controlled page-out, page-in, and replacement policies.
  - [ ] Handle I/O failures, concurrent faults, dirty pages, and exhausted swap space.
  - [ ] Integrate only after block/page caches and reliable page-fault handling are in place.

## Networking

- [ ] **Establish a network-device layer**
  - [ ] Provide a network-device interface and support at least one network-card driver (e.g. virtio-net, e1000, or RTL8139).
  - [ ] Implement packet RX/TX paths with interrupts or polling plus driver statistics.
- [ ] **Implement link and network protocols**
  - [ ] Implement Ethernet and ARP.
  - [ ] Implement IPv4, routing, and basic ICMP.
  - [ ] Implement ICMP echo request/reply so user programs can implement `ping`.
  - [ ] Implement UDP.
  - [ ] Implement TCP state, retransmission, flow control, and connection management.
- [ ] **Add IPv6 support** (experimental)
  - [ ] Implement ICMPv6, Neighbor Discovery, and stateless autoconfiguration for a single link.
  - [ ] Add IPv6 support on top of the user-space socket interface.
- [ ] **Design the user-space socket interface**
  - [ ] Define the syscall/socket ABI for user programs.
  - [ ] Implement user-space network tools (including `ping`) and validate them with QEMU user-mode networking.
- [ ] **Implement an entropy pool and kernel PRNG**
  - [ ] Collect entropy from available sources (RDTSC jitter, RTC/interrupt timing, device timing).
  - [ ] Probe RDRAND/RDSEED via CPUID and prefer hardware randomness when available, falling back to the entropy pool otherwise.
  - [ ] Maintain a kernel entropy pool feeding a CSPRNG (e.g. ChaCha20-based) for random bytes and integers.
  - [ ] Expose randomness through a syscall or device node for user programs; document seeding and pool-health rules.

## Graphics and GUI

- [ ] **Establish a kernel graphics subsystem**
  - [ ] Abstract framebuffers and display devices; improve graphics-mode and pixel-format management.
  - [ ] Provide a unified screen/region abstraction shared by VGA text mode and VBE modes.
- [ ] **Implement window management and input dispatch**
  - [ ] Implement window management, compositing, input-event dispatch, and focus management.
  - [ ] Define a virtual-canvas or multi-window layout model for graphical output.
- [ ] **Build the user-space GUI layer**
  - [ ] Provide user-space drawing/window interfaces via syscalls or shared-memory buffers.
  - [ ] Build a basic GUI toolkit or widget set and example applications.

## Executables, Security, and User-Space Ecosystem

- [ ] **Complete executable-format support**
  - [ ] Add ELF dynamic-segment, relocation, and dependency handling.
  - [ ] Support shared-library loading, symbol resolution, lifecycle, and address-space layout.
  - [ ] Define static/dynamic program compatibility and add loader tests.
- [ ] **Establish security mechanisms**
  - [ ] Define user, process, and file permission models.
  - [ ] Implement capabilities or equivalent fine-grained authorization.
  - [ ] Provide sandbox boundaries and resource limits for process/device access.
  - [ ] Security-test system calls, path resolution, shared memory, and inter-process interfaces.
- [ ] **Evaluate VT-x virtualization**
  - [ ] Check VMX support via CPUID and enable VMX operation (CR4.VMXE, VMXON region).
  - [ ] Build minimal VMCS setup to run a guest in VMX non-root operation.
  - [ ] Define goals first (e.g. userspace sandboxing vs. a full hypervisor); do not start without a concrete use case.
- [ ] **Define the user-space ABI and toolchain**
  - [ ] Design and stabilize the system-call ABI required by libc.
  - [ ] Establish a basic user-space compile, link, and packaging toolchain.
- [ ] **Implement mini-libc (`mini-libc/`, MIT licensed)**
  - [ ] Syscall wrappers over the class/sub-op ABI (`write`, `read`, `open`/`close`, `malloc` family, `exit`, `yield`, signals).
  - [ ] `string.h`/`stdlib.h` freestanding implementations (memcpy/memset/strlen/strcmp/malloc wrappers).
  - [x] `stdio.h` console/string coverage: `printf`/`sprintf`/`snprintf`/`puts`/`putchar`/`scanf`/`sscanf`/`getchar` with flags, width, precision.
  - [x] `_start` entry (`src/crt0.S`) and ELF32 link recipe so user programs compile against it.
  - [x] Validate with a hello-world ELF32 under QEMU (`examples/hello.c`).
- [ ] **Port musl libc**
  - [ ] Port musl libc and adapt its syscall layer to MyOS.
  - [ ] Validate core libc facilities (stdio, malloc, string, time) with tests.
- [ ] **Port user-space command-line tools**
  - [ ] Port and validate a set of basic command-line tools.
- [ ] **Port glibc**
  - > **Deprecated — not planned for now.** glibc's dynamic-linker, NSS, and locale architecture is far heavier than musl; revisit only if a concrete program requires glibc-only extensions.

## Testing and Quality Assurance

- [ ] **Automate code-quality assurance (static analysis and fuzzing)**
  - [ ] Integrate static analysis for kernel C/assembly (e.g. sparse, cppcheck, or stricter compiler warnings) into `make test`/CI.
  - [ ] Fuzz filesystem parsing (FAT32 directory entries, names, corrupt sectors) with malformed disk images in QEMU.
  - [ ] Fuzz syscall-ABI inputs (bad pointers, lengths, signal numbers) from user-mode test ELFs.
  - [ ] Fuzz the shell line editor (escapes, quotes, history) and the ELF loader with generated inputs.

## 64-bit Mode

> **Deprecated — not planned for now.** This section is kept for reference
> only; the 32-bit PAE kernel remains the target architecture.

- [ ] **Port the kernel to 64-bit long mode**
  - [ ] Replace the 32-bit PAE bootstrap with a long-mode transition (IA-32e, PML4/PDPT/PD/PT), keeping the Multiboot entry path.
  - [ ] Port kernel code to the x86-64 ABI: widen pointers, rewrite inline assembly and stack-frame contracts, and update every `include/` header.
  - [ ] Rework memory management for 64-bit address spaces (4-level paging, canonical addresses, kernel/user split), including the higher-half mapping and physical-memory bitmap.
  - [ ] Port interrupts, syscalls, Ring 3 processes, ELF loading, FAT32, drivers, and the shell to 64-bit; update all QEMU test fixtures.
  - [ ] Document the architecture delta in `docs/en`/`docs/zh` and keep both synchronized.
