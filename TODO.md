# MyOS Project TODO

This document tracks completed work and the project roadmap. When an item is completed, update both this file and [TODO-zh.md](./TODO-zh.md), and keep the English and Chinese READMEs and the kernel agent guide in sync.

## Completed

### Kernel, Memory, and Program Execution

- [x] Established the 32-bit x86 Multiboot/GRUB boot flow, including GDT, IDT, TSS, and basic interrupt handling.
- [x] Implemented kernel paging, PAE page tables, and high-memory mapping. The PMM supports physical addresses below 64 GiB, has host tests for page allocation above 4 GiB, and the optional QEMU `-m 6G` test allocates a real page at `0x100000000`, maps it, verifies writes and reads, then frees and reallocates it.
- [x] Implemented the kernel heap allocator and a basic user heap allocation interface.
- [x] Implemented basic Ring 3 process scheduling and process-management commands; at most two processes can run, and they share an address space.
- [x] Added the basic `SYS_WRITE` and `SYS_GETPID` system calls.
- [x] Implemented ELF32 loadable-segment validation/loading and BSS zeroing, plus built-in and disk-based ELF program execution.
- [x] Fixed a possible infinite reboot after PAE changes on the GRUB ISO boot path; build rules now track header dependencies to avoid reusing stale objects after ABI changes.

### ATA, FAT32, and Shell

- [x] Implemented ATA primary-master PIO/LBA28 sector access and startup device detection; the Shell omits the disk-path prompt when no disk is detected.
- [x] Implemented FAT32 mounting and file/directory operations, with automatic mount attempts when an ATA disk is detected.
- [x] Implemented FAT32 ASCII long file names (LFN) and 8.3 aliases, including names such as `help_aaaaa.1234`, with LFN sequence and checksum validation.
- [x] Implemented the `utf16LE_to_ascii` and `ascii_to_utf16LE` conversion interfaces.
- [x] Implemented the Shell's current-path prompt and navigation through existing directories, including `cd ..`; `cd` does not create directories, and `ls` defaults to the current directory while accepting an optional path.
- [x] Implemented `rmdir -f` to recursively remove non-empty directories while protecting the current directory and its parents.
- [x] Implemented the `run` command to load and execute an ELF32 program from a FAT32 disk.

### Project Structure, Tests, and Documentation

- [x] Organized source and header files while keeping `src/kernel.c` as the kernel entry point and grouping code under `arch/x86`, `mm`, `os`, `drivers`, `fs`, and `lib`.
- [x] Organized tests by purpose, separating Python tests, host C tests, Shell launchers, and QEMU integration tests; added the optional `make test-qemu-highmem` test for real high-page allocation and access.
- [x] Added host and QEMU integration tests covering PAE/PMM, high physical-page allocation and access, FAT32 LFN, Shell operations, Ring 3 processes, and disk ELF execution.
- [x] Created the project roadmap and documented its entry points in both READMEs and the kernel agent guide.

## Kernel, User Space, and System Calls

- [x] **Complete ISR14 page-fault handling**
  - [x] Distinguish user- and kernel-mode faults using the error code and interrupted privilege level.
  - [x] Demand-map pages only for registered ELF segments, the user heap, and the user stack.
  - [x] Distinguish unmapped pages from protection violations; terminate the process on illegal user access and panic on illegal kernel faults.
  - [x] Zero user pages before mapping and correctly release resources when allocation or mapping fails.
  - [x] Test valid demand mapping, out-of-range access, protection violations, kernel faults, and out-of-memory behavior. (QEMU covers all five via `stack.elf`/`faultu.elf`/`faultp.elf`/`oom.elf`/`kfault.elf`; the kernel-fault panic runs in a dedicated second session because it reboots the machine.)
  - Entry point: [isr14_handler](./src/arch/x86/trap.c).
- [ ] **Complete the user-space and system-call interfaces**
  - [ ] Define a stable, versioned system-call ABI, argument conventions, and error codes.
  - [ ] Validate user pointers, lengths, and address ranges; provide safe user/kernel data-copy helpers.
  - [ ] Complete process creation, exit, wait, signal/event, and resource-reclamation interfaces.
  - [ ] Add system calls for file, directory, console, time, and device access.
  - [ ] Add host and QEMU tests for the ABI and invalid-argument handling.
- [ ] **Complete the Shell and migrate it to Ring 3**
  - [ ] Gradually move command parsing, interaction, and built-ins out of the kernel.
  - [ ] Use system calls to access the console, filesystem, processes, and devices.
  - [ ] Provide basic command-line editing, history, error codes, and executable search.
  - [ ] Ensure the kernel can recover control if the user Shell crashes or exits.

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
  - [x] Review the capabilities and limits of existing ATA, keyboard, PIC/PIT, RTC, VGA/VBE, and ACPI drivers. (See `docs/en/11-driver-gaps.md` / `docs/zh/11-driver-gaps.md`.)
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
  - [ ] Validate against QEMU `-device` fixtures (e1000, ich9-ahci, qemu-xhci, bochs-display) per `docs/en/11-driver-gaps.md`.
- [ ] **Complete ACPI support**
  - [ ] Organize ACPI table discovery, mapping, length checks, and checksum validation into bounded common interfaces.
  - [ ] Parse MADT to provide processor and interrupt-controller information.
  - [ ] Evaluate HPET, MCFG, and related hardware support.
  - [ ] Improve power-state and shutdown/reboot error handling, and evaluate AML/DSDT support.

## Concurrency and Memory

- [ ] **Implement SMP / multicore support**
  - [ ] Enumerate processors through MADT and start application processors.
  - [ ] Add per-CPU state, Local APIC timers, and inter-processor interrupts.
  - [ ] Add SMP synchronization primitives for the scheduler, memory manager, devices, and caches.
  - [ ] Support cross-core task scheduling and add multicore QEMU regression tests.
- [ ] **Implement swap and disk paging**
  - [ ] Define swap-space and page-state/ownership management.
  - [ ] Implement controlled page-out, page-in, and replacement policies.
  - [ ] Handle I/O failures, concurrent faults, dirty pages, and exhausted swap space.
  - [ ] Integrate only after block/page caches and reliable page-fault handling are in place.

## Networking and Graphics

- [ ] **Implement a network stack**
  - [ ] Provide a network-device interface and support at least one network-card driver.
  - [ ] Implement Ethernet and ARP.
  - [ ] Implement IPv4, routing, and basic ICMP.
  - [ ] Implement UDP and design a user-space socket interface.
  - [ ] Implement TCP state, retransmission, flow control, and connection management.
- [ ] **Establish a graphics subsystem**
  - [ ] Abstract framebuffers and display devices; improve graphics-mode and pixel-format management.
  - [ ] Implement window management, compositing, input-event dispatch, and focus management.
  - [ ] Provide user-space drawing/window interfaces and build a basic GUI.

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
- [ ] **Port a standard user-space environment**
  - [ ] Design and stabilize the system-call ABI required by libc.
  - [ ] Port musl libc.
  - [ ] Establish a basic user-space compile, link, and packaging toolchain.
  - [ ] Port and validate a set of basic command-line tools.

## 64-bit Mode

> **Deprecated — not planned for now.** This section is kept for reference
> only; the 32-bit PAE kernel remains the target architecture.

- [ ] **Port the kernel to 64-bit long mode**
  - [ ] Replace the 32-bit PAE bootstrap with a long-mode transition (IA-32e, PML4/PDPT/PD/PT), keeping the Multiboot entry path.
  - [ ] Port kernel code to the x86-64 ABI: widen pointers, rewrite inline assembly and stack-frame contracts, and update every `include/` header.
  - [ ] Rework memory management for 64-bit address spaces (4-level paging, canonical addresses, kernel/user split), including the higher-half mapping and physical-memory bitmap.
  - [ ] Port interrupts, syscalls, Ring 3 processes, ELF loading, FAT32, drivers, and the shell to 64-bit; update all QEMU test fixtures.
  - [ ] Document the architecture delta in `docs/en`/`docs/zh` and keep both synchronized.
