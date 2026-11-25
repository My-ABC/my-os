# 01 — Boot

English | [中文](../zh/01-boot.md)

## Multiboot header

`boot/boot.asm` starts with a Multiboot header (`0x1BADB002`, flags `0x07`)
so GRUB loads the kernel at physical 1 MiB and jumps to `start` with
`%ebx` = physical address of the Multiboot information structure. The
header also requests a 640x480 video mode.

## Early page tables

Before any C code runs, `boot.asm`:

1. Verifies CPUID PAE support (CPUID leaf 1, EDX bit 6); hangs if absent.
2. Builds static PAE page tables in `.bss`:
   - `early_pae_pdpt` + `early_pae_pd0..pd3` — four 512-entry page
     directories.
   - `early_pae_pt_low` — 16 MiB low identity map (PAE page-directory
     entries map 2 MiB each via `PAE_PAGE_SIZE`; the fill loop actually
     populates page-table entries covering the first 16 MiB).
   - `early_pae_pt_high` — high-half alias: virtual `0xC0000000` maps to
     physical `0x00100000` (kernel load address) for 16 MiB.
3. Sets CR4.PAE (bit 5), loads CR3 with `early_pae_pdpt`, sets CR0.PG
   (bit 31).

This early layout exists only to reach `kmain`; `paging_init()` later
replaces it with the runtime page tables (see [02-memory.md](02-memory.md)).

## Entering kmain

After paging is enabled, `boot.asm` jumps to `.pae_entry`, restores `%ebx`
(the Multiboot info pointer saved on the stack), and calls `kmain`.
Failure paths (`PAE unsupported`, post-`kmain` return) enter `cli; hlt`
loops.

## kmain init sequence

`src/kernel.c:kmain(uint32_t multiboot_info)` performs, in order:

1. `gdt_init()` + `tss_init()` — descriptor tables.
2. `serial_init()` — early debug output on COM1.
3. `pmm_init(multiboot_info)` — bitmap allocator seeded from the Multiboot
   memory map.
4. `paging_init()` + `tss_update_cr3()` — runtime PAE tables; TSS `esp0`
   follows the new CR3.
5. Optional `highmem_qemu_test()` under `PMM_QEMU_HIGHMEM_TEST`.
6. Remaps the Multiboot info page(s) into the low identity map so VBE
   probing can read them later.
7. `vbe_initialize(multiboot_info)` (BIOS VBE info captured pre-boot),
   then `vga_init()`, then `vt_init()` (per-terminal screen backing
   stores — see [15-virtual-terminals.md](15-virtual-terminals.md)).
8. `ata_init()` probe; on success `cmd_disk(1, 0)` mounts FAT32.
9. `idt_init()` — exception/IRQ/syscall gates.
10. Heap self-tests (`kmalloc`/`kfree`/`kcalloc`/`krealloc`) and a
    PMM+mapping smoke test at `0x00800000`.
11. `pic_remap()` (masks IRQ2, keeps IRQ0/1 masked), `pit_init(100 Hz)`,
    `keyboard_init()`, `sti`, `tsc_init()`, `rtc_init()`.
12. Boot-time VGA/serial banner output and RTC time display.
13. Demo paths under `PANIC_DEMO` / `ELF_DEMO` / `KEYBOARD_DEMO`;
    otherwise `shell3_supervisor()` — which auto-starts `/shell3.elf`
    on both virtual terminals when the disk image provides it, falls
    back to the kernel `start_shell()` when no disk is mounted, the
    file is missing, or vt 0's shell3 spawn permanently fails (5
    transient retries then ELF errors latch `spawn_failed`), and keeps
    respawning dead shells via `vt_service()`.
    See [07-shell.md](07-shell.md) and
    [15-virtual-terminals.md](15-virtual-terminals.md).

> Note: `kmain` mixes real init with boot-time self-test logging. The
> serial markers (`MyOS v0.0.1`, `[HEAP] ...`, `[PAGING] ...`) are part of
> the QEMU integration contract in `test/integration/`.
