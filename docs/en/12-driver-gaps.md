# 12 — Driver Ecosystem Inventory and Gaps

English | [中文](../zh/12-driver-gaps.md)

This document inventories the drivers in `src/drivers/`, records their
capability limits, and lists the missing drivers by bus/device category
with priorities and QEMU-testable devices. It is the checklist behind the
"Inventory and enumerate driver-ecosystem gaps" item in `TODO.md`; the
"select and implement incrementally" item stays open because drivers are
adopted one at a time as needs arise.

## Existing drivers

| Driver | Capability | Limits |
|---|---|---|
| ATA PIO (`ata.c`) | Primary-master disk, LBA28, polled PIO read/write of ≤255 sectors per call | No LBA48, no DMA/UDMA, no write-cache flush, no IRQ14, single channel (no slave/secondary), no ATAPI, no error-detail propagation |
| Keyboard (`keyboard.c`) | IRQ1, scancode sets 1–2, 32-byte ring buffer, ASCII + Shift/Caps/Num | Arrow/function/extended keys unmapped, no escape sequences, no UTF-8, Alt/Ctrl tracked but unused, buffer overflow drops keys silently |
| PIC (`pic.c`) | 8259A master/slave remap to IRQ32+, per-line mask, EOI | No spurious-IRQ handling, no priority/IRR inspection, no IRQ sharing model |
| PIT (`pit.c`) | 8253 channel 0 at `PIT_FREQUENCY` (100 Hz), tick counter, drives scheduler + VBE cursor blink | Channel 0 only, no read-back/latency measurement, coarse time source |
| RTC (`rtc.c`) | MC146818 CMOS time, BCD/12–24 h decode, century heuristic, Beijing-time and Unix-timestamp helpers | No IRQ8 alarm/update-ended, century inferred by range guess, no HPET fallback |
| Serial (`serial.c`) | COM1 38400 8N1, polled TX, polled RX helpers (`serial_getchar`/`serial_wait_char`/`serial_gets`) | RX helpers unused by the shell (keyboard is the only console input), no IRQ4, single port, no flow control |
| VGA (`vga.c`) | 80×25 text mode, 16 colors, hardware cursor, scroll | Text mode only; superseded by VBE when a framebuffer exists |
| VBE (`vbe.c`) | Uses the Multiboot/BIOS framebuffer (not VBE BIOS calls), maps it at `0xE0000000`, 8/15/16/24/32 bpp with per-field masks, software 8×8 text rendering, blinking cursor | No mode setting (inherits GRUB's mode), no double buffering, no acceleration, no pointer/input |
| ACPI (`acpi.c`) | RSDP scan (EBDA + BIOS area), RSDT/XSDT walk to FADT, FADT reset register for reboot, PM1 S5 shutdown with a `_S5_` AML byte-pattern hack, triple fallbacks (0xCF9, KBC reset, triple fault) | Only FADT is consumed; no MADT/HPET/MCFG, no general table registry, no length/checksum validation of SDTs, no real AML interpreter |
| Font (`ascii_font.c`) | 8×8 bitmap glyphs for VBE text | Data only |

## Missing drivers by bus/device

### ISA / legacy

| Driver | Why | QEMU test device |
|---|---|---|
| Serial RX interrupt + console input | Headless shells currently cannot receive input; polled helpers exist but are unused | `-serial tcp:...` already used by tests; add `-serial` input path |
| PS/2 mouse (i8042 AUX, IRQ12) | Prerequisite for any pointer-driven UI | Default `-machine pc` PS/2 mouse; `sendkey mouse_move` via monitor |
| i8042 controller hardening | Self-test, AUX enable, command timeout | Same i8042 as keyboard |

### PCI (nothing enumerated yet)

There is no PCI configuration-space access (`0xCF8`/`0xCFC`), so every PCI
device is unreachable. A minimal bus layer (config read/write, BAR
assignment, INTx routing) is the foundation for all of these:

| Driver | Why | QEMU test device |
|---|---|---|
| PCI bus enumeration | Foundation for NIC, storage, USB, GPU | Any `-device` on `-machine pc`/`q35` |
| e1000 or virtio-net NIC | First network driver; TODO lists networking | `-device e1000` / `-netdev user` |
| AHCI SATA | Replaces PIO ATA with DMA + NCQ + hotplug | `-device ich9-ahci` on q35 |
| xHCI/EHCI USB | USB keyboard/mouse/storage | `-device qemu-xhci -device usb-kbd` |
| Bochs/VirtIO GPU | Real mode setting + accel vs. inherited VBE mode | `-device bochs-display` / `-device virtio-gpu` |
| MSI/MSI-X + IOAPIC routing | Required once PCI IRQs exceed ISA range | Comes with PCI layer |

### Storage

| Driver | Why | QEMU test device |
|---|---|---|
| ATA LBA48 + flush + DMA | Correctness/durability on larger disks | Built-in `-drive if=ide` |
| ATA secondary channel / ATAPI | Second disk, CD-ROM | `-drive if=ide,index=1` |
| AHCI (see PCI) | Modern path, supersedes ATA PIO | q35 + `ich9-ahci` |

### Interrupts / timers / power

| Driver | Why | QEMU test device |
|---|---|---|
| ACPI MADT parse | CPU count, IOAPIC location, IRQ overrides — prerequisite for SMP | Always present |
| IOAPIC + local APIC | PCI IRQ routing, SMP prerequisite | q35/`-machine pc` |
| HPET | High-resolution timers (evaluate vs. PIT) | `-machine q35` exposes HPET |
| RTC IRQ8 alarm | Wake/timer events | CMOS emulation |
| AML/DSDT interpreter | Full power management, thermal, battery — large effort, low value for a teaching kernel | N/A |

## Suggested adoption order

1. **Device subsystem skeleton** (`TODO`: registration/probing) — do this
   before new buses so each driver lands in a uniform shape.
2. **PCI enumeration** — unlocks NIC/storage/USB/GPU; highest leverage.
3. **Serial console input** — cheap, immediately useful for headless use.
4. **PS/2 mouse** — pairs with the future GUI work.
5. **ATA hardening** (LBA48, flush, DMA) — incremental, keeps FAT32 path
   honest on real disks.
6. **ACPI MADT + IOAPIC/APIC** — required before SMP (`TODO` SMP section).
7. **NIC (e1000/virtio)** — after PCI + a device layer exist.
8. Later/low priority: HPET, USB HID, AML interpreter, NVMe.

Each item should be adopted only when a concrete feature needs it; this
list records candidates, not commitments.
