# 15 — Virtual Terminals (ttys)

MyOS provides `VT_COUNT` (= 2) virtual terminals that share the single
physical display. **F1 selects vt 0, F2 selects vt 1**; each terminal
runs its own `shell3.elf` Ring 3 process concurrently. Only the active
terminal is visible and receives keyboard input; every terminal keeps
a private screen backing store, so switching preserves each terminal's
contents.

The implementation lives in `src/drivers/vt.c` /
`include/drivers/vt.h`.

## Public API

| Function | Purpose |
|---|---|
| `vt_active_index()` | Index of the currently visible terminal |
| `vt_render_index()` | Terminal that output from the current context belongs to: the running Ring 3 process's `vt`, or `VT_FALLBACK` (0) in kernel context. `vga.c`/`vbe.c` resolve their render target through this |
| `vt_text_target(vt)` / `vt_vbe_target(vt)` | Backing-store pointer for a terminal — the physical display for the active vt, the private buffer otherwise (`NULL` when unavailable) |
| `vt_request_switch(vt)` | Queue a switch; called from the keyboard ISR, executed later by `vt_service` |
| `vt_service()` | Deferred work: performs a pending switch, then ensures every vt has a live shell3 (spawn or respawn via the registered spawner). Cheap when idle; safe from any kernel context |
| `vt_init()` | Allocate per-vt backing stores; call after `vga_init`/`vbe_initialize` and `pmm_init` |
| `vt_set_spawner(fn)` / `vt_spawn_shell(vt)` / `vt_shell_alive(vt)` | Spawner registration, immediate spawn, and liveness check for a terminal's tracked shell pid |

## Switching flow

1. The keyboard ISR recognizes F1 (scan-set-1 make code `0x3B`) and F2
   (`0x3C`) and calls `vt_request_switch()`. The scancodes never enter
   the per-vt input buffers.
2. `vt_service()` — run from the kernel idle loop inside
   `shell3_supervisor` / `shell_read_line` — picks up the pending
   request and calls `vt_do_switch()`.
3. `vt_do_switch()` copies the outgoing terminal's screen into its
   private store and the incoming terminal's store onto the physical
   display, then calls `vga_console_switched()` /
   `vbe_console_switched()` to resynchronize cursor state. The whole
   copy runs with interrupts off (IRQ0's cursor blink reads
   `vt_render_index()` mid-copy otherwise).

The switch is deferred because the backing-store `memcpy` is far too
heavy for IRQ1 context.

## Screen backing stores

**VGA text mode** — each inactive vt owns a private
`uint16_t text_cells[80*25]` array; the active vt renders directly into
physical `0xB8000`. Switching is two 4 KiB `memcpy`s.

**VBE graphics mode** — each vt owns a private framebuffer in the free
kernel-half window `0xD0000000`–`0xE0000000` (below the scanout map at
`0xE0000000`). Pages are allocated from the PMM and mapped on demand.
The per-terminal stride is derived at init:

```
stride = align(vbe_scanout_bytes(), VT_FB_ALIGN)   /* 1 MiB */
       = align(pitch * height, 1 MiB)
```

For the observed 1280×800×32 mode, `vbe_scanout_bytes()` returns
5120×800 = 4,096,000 bytes, so each terminal gets a **4 MiB** stride —
never hardcode 640×480 sizes.

## Process model

- `struct ring3_process` carries a `vt` field; children inherit the
  parent's vt.
- `foreground_pid` is per-terminal: `foreground_pid[VT_COUNT]`,
  accessed through `ring3_foreground_pid_vt(vt)`.
- `ring3_schedule_mm(..., foreground, vt)` takes the target vt;
  `elf_run_vt(image, size, vt)` loads an ELF onto a specific terminal
  (`elf_run` delegates with the current vt).
- When a process gives up the foreground slot, ownership falls back to
  its `parent_pid` on the same vt when still alive — the foreground
  slot never shows a visible zero window (this is what prevents the
  doubled-prompt race fixed in `a2ebbbf`).
- Ctrl+C is delivered by the keyboard ISR to the *active* vt's
  foreground pid via `ring3_foreground_pid_vt(vt_active_index())`.

## Input routing

Each vt has a private 256-byte keyboard ring buffer
(`keyboard_getchar_vt` / `keyboard_hit_vt`). The ISR enqueues keystrokes
only into the currently visible terminal's buffer, so a hidden
terminal's shell keeps its pending input untouched while switched out.
`IO_READ`/`IO_GETCHAR`/`IO_WRITE` and the process-control ops resolve
the caller's vt through `ring3_current_vt()`.

## Lifecycle and respawn

`kernel.c`'s `shell3_supervisor` registers a spawner and then loops on
`vt_service()`. On boot both terminals get a `shell3.elf` instance; when
a terminal's tracked shell pid dies or disappears, `vt_service`
respawns it on that vt. The kernel shell stays pinned to vt 0 and is
only reachable when no disk is mounted or `shell3.elf` is missing.

Two invariants keep this reliable:

- **`vt_spawn_shell` runs with IRQs off** for the whole
  FAT32-read + ELF-load + pid-tracking sequence. IRQ0 preemption would
  park the kernel context in `kernel_frame` limbo, and it only resumes
  when the scheduler returns — which can starve behind a process that
  never yields.
- **`sys_keyboard_wait_yield` yields unconditionally** when the keyboard
  buffer is empty (it rewinds the saved `eip` by 2 so `int 0x80`
  restarts). A lone process blocked in `IO_GETCHAR` would otherwise
  starve the kernel idle loop and freeze `vt_service` entirely.

## Serial output

Ring 3 `IO_WRITE` renders to the process's own vt buffer and mirrors
the string to **COM3** (`serial3_print`) — a single global mirror, so
output from vt 0 and vt 1 interleaves there. COM1 carries kernel-side
output only (diagnostics plus kernel-shell commands).

## Limitations

- `VT_COUNT` is fixed at 2; adding terminals means extending the vt
  array, per-vt foreground slots, and the F-key map together.
- COM3 is a global mirror, not a per-terminal serial stream.
- Each vt keeps exactly one screen of state — no scrollback buffer.
- The kernel shell exists only on vt 0.
