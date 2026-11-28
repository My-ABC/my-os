# 02 — Memory Management

English | [中文](../zh/02-memory.md)

## Address-space layout

| Range | Use |
|---|---|
| `0x00000000`–`0x000FFFFF` | Real-mode IVT/BDA area; mostly unused after boot |
| `0x00100000`–… | Kernel physical load address (1 MiB) |
| `0x00800000` | Boot-time mapping smoke-test page (transient) |
| `0x01000000`–`0x40000000` | Ring 3 user heap (`USER_HEAP_START`/`USER_HEAP_END`) |
| `0x00810000`, `0x00820000` | Per-process Ring 3 user stack tops (`USER_STACK_TOP_0/1`), 32 KiB each growing down |
| `0x00900000` | Per-process sigreturn trampoline (`USER_SIG_TRAMPOLINE`); reserved, never load test ELFs here |
| ELF image ranges | Demand-mapped per live Ring 3 process |
| `< 0xC0000000` | User range (`USER_SPACE_END`) |
| `0xC0000000`+ | Kernel higher-half mapping (`KERNEL_VIRT_BASE`) |
| `0xD0000000`–`0xE0000000` | Kernel heap window (`KERNEL_HEAP_START`/`KERNEL_HEAP_END`) |
| `0xE0000000`–~`0xE0400000` | VBE scanout mapping (`VBE_FRAMEBUFFER_ADDRESS`, ~4 MiB at 1280×800×32) |
| `0xE8000000`–`0xF0000000` | VT private framebuffer backing stores (`VT_FB_BASE`/`VT_FB_LIMIT`) |
| `0xF0000000` | Temporary page-table access map (`KMAP_BASE`) |

The kernel is linked at virtual `0xC0000000` / physical `0x100000`
(`linker.ld`); `.text/.rodata/.data` carry `AT()` physical addresses.

## PAE paging (`src/mm/paging.c`)

- 4-level tables (PDPT → PD → PT), 4 KiB pages, 64-bit PTEs
  (`PAE_PRESENT`, `PAE_WRITABLE`, `PAE_USER`, `PAE_ACCESSED`,
  `PAE_DIRTY`, `PAE_PAGE_SIZE`, `PAE_NX`).
- Physical ceiling: 36 bits (< 64 GiB), `PMM_PHYSICAL_CEILING`.
- Page-table pages are not permanently mapped; `paging_map_physical()`
  temporarily maps a physical page at `0xF0000000` with interrupts
  disabled, and `paging_unmap_physical()` restores them.
- `paging_map_page()` refuses user mappings (`PAGE_USER`) inside kernel
  address space, refuses to overwrite an already-present PTE, and
  refuses when the PDE is a 2 MiB large page (`PAE_PAGE_SIZE`);
  `paging_unmap_page()` warns on missing entries.
- `paging_init()` builds the runtime tables: kernel high-half mapping,
  low identity coverage needed by the kernel, and leaves user ranges
  unmapped for demand paging.

## PMM (`src/mm/pmm.c`)

Bitmap frame allocator over 4 KiB frames up to the 36-bit physical
ceiling. `pmm_init()` consumes the Multiboot memory map (usable regions
only). `pmm_alloc_page()` / `pmm_free_page()` are the basic ops;
`pmm_alloc_page_at_or_above()` supports the >4 GiB highmem test.

## Kernel heap (`src/mm/heap.c`)

`kmalloc`, `kcalloc`, `krealloc`, `kfree` allocate variable-size blocks
from PMM-backed pages inside the `0xD0000000`–`0xE0000000` window
(`KERNEL_HEAP_START`/`KERNEL_HEAP_END`); allocations outside it are
rejected. This window is exclusive: the VBE scanout
(`0xE0000000`+) and VT framebuffer stores (`0xE8000000`+) sit above it
and are protected by compile-time `#error` guards in `src/drivers/vt.c`.
Used by the shell (`cp -r` entry lists, `run`
image buffer), Ring 3 process structures, and FAT32 copy paths.

## User address space

User memory is demand-mapped, never fully populated:

- **ELF image ranges** — recorded per live process; the loader maps code/
  data pages on demand.
- **User stacks** — two 32 KiB regions whose tops are `0x00810000`
  (`USER_STACK_TOP_0`) and `0x00820000` (`USER_STACK_TOP_1`), growing
  down; released wholesale when a process dies.
- **User heap** — `0x01000000`–`0x40000000`, managed by the `SYS_MEMORY`
  sub-operations in `src/os/syscall.c`.

## ISR14 page-fault handling (`src/arch/x86/trap.c`)

`isr14_handler` classifies each fault:

- **User vs kernel**: error-code bit 2 (U/S) or `cs & 3`.
- **Not-present (`P=0`) inside a registered user range** (live-process
  ELF ranges, user stacks, `USER_HEAP_*`): allocate a physical frame,
  map it `PRESENT|WRITABLE|USER`, zero it, resume.
- **Protection violation (`P=1`) or unregistered user access**: terminate
  the faulting process via `ring3_terminate_current()`.
- **Kernel fault**: panic.

Return contract: `0` = normal `iret`; non-zero = kernel `irq_frame *` to
resume the interrupted context. The `isr14` stub in
`boot/interrupt.asm` switches stacks and replays the IRQ0 exit sequence
when a pointer is returned.

Registered-range checks live in `ring3_user_range_permitted()`
(`src/os/ring3.c`) and `sys_user_heap_contains()` (`src/os/syscall.c`).
