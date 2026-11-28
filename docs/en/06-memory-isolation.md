English | [中文](../zh/06-memory-isolation.md)

# 06 — Memory Isolation (per-process page tables)

Each Ring 3 process owns a private address space through `struct user_mm`
(`src/mm/paging.c`, `USER_MM_MAX = 8` slots).

## Layout

- **PDPT[0] -> private page directory** covering the low 1 GiB of user
  space. Page tables are allocated on demand via the KMAP window, so
  table edits never depend on the currently loaded CR3.
- **PDPT[3] -> shared kernel page directory**, identical to the kernel's
  own entry, so the kernel half — including the KMAP window — is the
  same in every address space and kernel code may run on any CR3.
- **Private copy of the supervisor-only low 4 MiB identity map** in the
  user PD. It keeps the `.boot.text` interrupt/syscall stubs and the
  initial low kernel stack reachable after any CR3 switch. Because the
  kernel stack at `0x11B000` lives in this range, `paging_switch_cr3`
  can be called from ordinary C without the return path faulting.

## Rules

- CR3 is switched on every context switch: `ring3_timer_switch`
  (preemption both ways), `PROC_YIELD`, and both termination paths
  (`PROC_EXIT`, fatal signals, `ring3_kill`). CR3 reload flushes all
  non-global TLB entries.
- ISR14 demand paging only maps into the *running* process's mm
  (`ring3_current_mm()`); `ring3_user_range_permitted()` therefore only
  consults the current process's image/stack ranges.
- The signal restorer trampoline is installed per process at schedule
  time; signal delivery temporarily switches CR3 to write the handler
  frame onto the target's private user stack.
- `paging_mm_release()` walks the private page tables and frees every
  owned user page — stack, ELF image, user heap, trampoline — plus the
  page-table pages themselves. This also reclaims user heap pages that
  the old shared-table teardown leaked.
- Kernel pages borrowed into a user space (the low identity map, the
  linker-placed `.user.text` code page used by the `ring3` builtin)
  carry PTE bit 9 (`PAGE_BORROWED`) and are never freed by
  `paging_mm_release`.
- User mappings below 4 MiB and above 1 GiB are refused, keeping the
  low identity region and the unused PDPT[1..2] range kernel-only.
- `paging_mm_map` refuses to overwrite an already-present PTE — remap
  requires `paging_mm_unmap` first, so a stray double-map cannot leak
  the old physical page or silently change permissions.

## Verification

The full `make test-qemu` suite passes: Ring 3 scheduling and
foreground/background switching, SIGINT handler delivery with
`SIG_RETURN`, ELF load/run, stack demand-mapping, illegal-access
termination, and OOM reclamation. Isolation is structural — a process
cannot observe another's pages because they are simply absent from its
page tables.
