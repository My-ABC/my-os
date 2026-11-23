#ifndef _VT_H
#define _VT_H

#include "stdint.h"

/* Virtual terminals (ttys): VT_COUNT independent consoles sharing the
 * single physical display. F1 selects vt 0 (the primary terminal that
 * hosts the kernel shell or the auto-started shell3), F2 selects vt 1.
 * Only the active vt receives keyboard input and is visible; each vt
 * keeps its own screen backing store so switching preserves content.
 *
 * Kernel-context output (no current Ring 3 process) is pinned to vt 0,
 * which is also where the kernel shell lives. */
#define VT_COUNT 2
#define VT_FALLBACK 0

/* Currently visible terminal index. */
int vt_active_index(void);

/* Terminal that output from the current context belongs to: the
 * running Ring 3 process's vt, or VT_FALLBACK in kernel context.
 * vga.c/vbe.c resolve their render target through this. */
int vt_render_index(void);

/* Backing-store pointers for terminal `vt`. The active vt returns the
 * physical display addresses; inactive vts return their private
 * buffers. Returns NULL when the backing store is unavailable. */
uint16_t *vt_text_target(int vt);
uint8_t *vt_vbe_target(int vt);

/* Queue a terminal switch (called from the keyboard ISR; the actual
 * memcpy-heavy switch runs later in vt_service). */
void vt_request_switch(int vt);

/* Deferred work: performs a pending switch, then makes sure every vt
 * has a live shell3 process (spawn or respawn through the registered
 * spawner). Safe to call from any kernel context; cheap when idle. */
void vt_service(void);

/* Allocate per-vt backing stores. Call after vga_init/vbe_initialize
 * and pmm_init. */
void vt_init(void);

/* Spawner return codes: 0 = shell scheduled; VT_SPAWN_RETRY = transient
 * failure (disk not mounted, file missing, allocation pressure) — the
 * vt layer retries later without latching; any other nonzero value is
 * a permanent failure and stops respawn attempts for that vt. */
#define VT_SPAWN_RETRY (-1)

/* Register the callback that loads /shell3.elf and schedules it on a
 * vt. Signature: int fn(int vt) -> 0 on success. */
void vt_set_spawner(int (*spawner)(int vt));

/* Try to spawn shell3 on `vt` right now. Returns 0 on success. */
int vt_spawn_shell(int vt);

/* 1 while the shell tracked for `vt` is still alive. */
int vt_shell_alive(int vt);

#endif
