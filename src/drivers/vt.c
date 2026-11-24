#include "vt.h"
#include "vga.h"
#include "vbe.h"
#include "ring3.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include "serial.h"
#include "pit.h"

/* Physical VGA text memory; each inactive vt keeps a private copy in
 * text_cells and the visible vt renders straight into this window. */
#define VT_VGA_MEMORY   ((volatile uint16_t *)0xB8000)
#define VT_VGA_CELLS    (80U * 25U)
#define VT_VGA_BLANK    0x0F20U  /* ' ' | light-gray-on-black << 8 */

/* Offscreen VBE backing stores live above the kernel heap window
 * (0xD0000000-0xE0000000) and below the KMAP slot at 0xF0000000; the
 * VBE scanout map at 0xE0000000 only occupies ~4 MiB. Keeping them
 * here leaves ~740 MiB of headroom for kernel image growth and never
 * overlaps KERNEL_HEAP_START (kmalloc maps pages with
 * paging_map_page, which overwrites PTEs unconditionally).
 * The stride is derived from the real mode size at init
 * (1280x800x32 scanout is 4,096,000 B -> 4 MiB stride). */
#define VT_FB_BASE      0xE8000000U
#define VT_FB_LIMIT     0xF0000000U
#if VT_FB_BASE < 0xE0400000U
#error "VT_FB_BASE overlaps VBE scanout mapping"
#endif
#if VT_FB_BASE >= 0xF0000000U
#error "VT_FB_BASE overlaps KMAP window"
#endif
#define VT_FB_ALIGN     0x100000U
/* Minimum PIT ticks between transient spawn retries (~1 s at 100 Hz). */
#define VT_RETRY_TICKS  100U
/* Transient spawn retries per vt at ~1 Hz before the vt is disabled
 * and its F-key switch is dropped. */
#define VT_MAX_SPAWN_RETRIES 5U

struct vt_state {
    uint32_t shell_pid;      /* tracked shell3 pid, 0 = none yet */
    int spawn_failed;        /* spawner refused permanently; stop retrying */
    int switch_enabled;      /* F-key switching allowed for this vt */
    uint32_t retry_count;    /* transient spawn failures so far */
    int fb_ready;
    uint32_t next_retry_tick;/* earliest pit tick for a transient retry */
    uint16_t text_cells[VT_VGA_CELLS];
    uint8_t *fb;
};

static struct vt_state vts[VT_COUNT];
static uint32_t vt_fb_stride;
static int vt_active;
static int vt_pending = -1;
static int (*vt_spawner)(int vt);

static uint32_t vt_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void vt_irq_restore(uint32_t flags) {
    if (flags & 0x200U) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

int vt_active_index(void) {
    return vt_active;
}

int vt_render_index(void) {
    uint32_t vt = ring3_current_vt();
    return vt < VT_COUNT ? (int)vt : VT_FALLBACK;
}

uint16_t *vt_text_target(int vt) {
    if (vt < 0 || vt >= VT_COUNT) vt = VT_FALLBACK;
    if (vt == vt_active) {
        return (uint16_t *)VT_VGA_MEMORY;
    }
    return vts[vt].text_cells;
}

uint8_t *vt_vbe_target(int vt) {
    if (!vbe_available()) return 0;
    if (vt < 0 || vt >= VT_COUNT) vt = VT_FALLBACK;
    if (vt == vt_active) {
        return vbe_scanout();
    }
    return vts[vt].fb_ready ? vts[vt].fb : 0;
}

void vt_request_switch(int vt) {
    /* vt 0 stays reachable even after giving up: it hosts the ring0
     * kernel shell fallback, so its F-key is never dropped. */
    if (vt >= 0 && vt < VT_COUNT &&
        (vt == VT_FALLBACK || vts[vt].switch_enabled)) {
        vt_pending = vt;
    }
}

static int vt_pid_alive(uint32_t pid) {
    uint32_t pids[RING3_PROCESS_COUNT];
    uint32_t states[RING3_PROCESS_COUNT];
    uint32_t count = ring3_process_list(pids, states, RING3_PROCESS_COUNT);
    for (uint32_t i = 0; i < count; ++i) {
        if (pids[i] == pid &&
            (states[i] == RING3_PROCESS_READY ||
             states[i] == RING3_PROCESS_RUNNING)) {
            return 1;
        }
    }
    return 0;
}

int vt_shell_alive(int vt) {
    if (vt < 0 || vt >= VT_COUNT) return 0;
    return vts[vt].shell_pid != 0U && vt_pid_alive(vts[vt].shell_pid);
}

void vt_set_spawner(int (*spawner)(int vt)) {
    vt_spawner = spawner;
}

/* Latch a permanent spawn failure for `vt`: stop respawning and drop
 * its F-key (vt 0 stays reachable — it hosts the ring0 fallback).
 * When vt 0 itself gives up, queue a switch back to it so the kernel
 * shell takes over the display. */
static void vt_latch_spawn_failed(int vt) {
    vts[vt].spawn_failed = 1;
    vts[vt].switch_enabled = 0;
    if (vt == VT_FALLBACK) {
        vt_pending = VT_FALLBACK;
    }
}

int vt_needs_ring0_fallback(void) {
    return vts[VT_FALLBACK].spawn_failed;
}

int vt_spawn_shell(int vt) {
    uint32_t flags;
    int rc;
    if (vt < 0 || vt >= VT_COUNT || vt_spawner == 0) return -1;
    if (vts[vt].spawn_failed) return -1;

    /* The whole spawn (disk read + ELF load + pid tracking) runs with
     * IRQs off: IRQ0 preemption would park this kernel context in
     * kernel_frame limbo, and it only resumes when the scheduler
     * returns here — which may starve behind a process that never
     * yields (e.g. a shell blocked in IO_GETCHAR). */
    flags = vt_irq_save();
    serial_print("[VT] spawning shell on vt ");
    serial_print_dec((uint32_t)vt);
    serial_putchar('\n');
    rc = vt_spawner(vt);
    if (rc == VT_SPAWN_RETRY) {
        /* Transient failure (disk not mounted yet, file missing,
         * allocation pressure): retry once per second. After
         * VT_MAX_SPAWN_RETRIES the vt gives up: no more respawn
         * attempts and its F-key switch is disabled. vt 0 still has
         * the ring0 kernel shell as fallback. */
        ++vts[vt].retry_count;
        if (vts[vt].retry_count >= VT_MAX_SPAWN_RETRIES) {
            vt_latch_spawn_failed(vt);
            serial_print("[VT] vt ");
            serial_print_dec((uint32_t)vt);
            serial_print(" spawn retries exhausted; vt disabled\n");
        } else {
            vts[vt].next_retry_tick = pit_ticks() + VT_RETRY_TICKS;
        }
        vt_irq_restore(flags);
        return -1;
    }
    if (rc != 0) {
        vt_latch_spawn_failed(vt);
        serial_print("[VT] shell3 spawn failed on vt ");
        serial_print_dec((uint32_t)vt);
        serial_print(" rc=");
        serial_print_dec((uint32_t)(int32_t)rc);
        serial_putchar('\n');
        vt_irq_restore(flags);
        return -1;
    }
    vts[vt].shell_pid = ring3_foreground_pid_vt(vt);
    vts[vt].retry_count = 0U;
    if (vts[vt].shell_pid == 0U) {
        /* elf_run succeeded but no foreground pid was recorded; do not
         * loop forever spawning processes that cannot be tracked. */
        vt_latch_spawn_failed(vt);
        serial_print("[VT] shell3 spawn on vt ");
        serial_print_dec((uint32_t)vt);
        serial_print(" produced no foreground pid\n");
        vt_irq_restore(flags);
        return -1;
    }
    serial_print("[VT] shell3 started on vt ");
    serial_print_dec((uint32_t)vt);
    serial_print(" pid ");
    serial_print_dec(vts[vt].shell_pid);
    serial_putchar('\n');
    vt_irq_restore(flags);
    return 0;
}

static void vt_ensure_shells(void) {
    for (int i = 0; i < VT_COUNT; ++i) {
        if (vts[i].spawn_failed) continue;
        if (vt_shell_alive(i)) continue;
        if (pit_ticks() < vts[i].next_retry_tick) continue;
        vts[i].shell_pid = 0U;
        (void)vt_spawn_shell(i);
    }
}

/* A switch target with no live shell would show a stale or all-blank
 * screen that looks like a hang. Paint a notice into its backing store
 * instead. vt 0 is exempt once it has fallen back to the ring0 kernel
 * shell (spawn_failed latched): that screen belongs to the kernel
 * shell, not to a missing shell3. */
static int vt_needs_placeholder(int vt) {
    return !vt_shell_alive(vt) &&
           !(vt == VT_FALLBACK && vts[vt].spawn_failed);
}

static void vt_paint_text_message(int vt, const char *msg) {
    uint16_t *cells = vts[vt].text_cells;
    uint32_t i;
    for (i = 0; i < VT_VGA_CELLS; ++i) {
        cells[i] = VT_VGA_BLANK;
    }
    for (i = 0; msg[i] != '\0' && i < VT_VGA_CELLS; ++i) {
        cells[i] = (uint16_t)(0x0F00U | (uint8_t)msg[i]);
    }
}

static void vt_paint_placeholder(int vt) {
    if (!vt_needs_placeholder(vt)) return;
    if (vbe_available()) {
        vbe_vt_message(vt, "[ waiting for shell3... ]");
    } else {
        vt_paint_text_message(vt, "[ waiting for shell3... ]");
    }
}

/* Swap the visible display with terminal `target`. Both backing
 * stores must exist; runs with interrupts off because IRQ0's cursor
 * blink reads vt_render_index() mid-copy otherwise. */
static void vt_do_switch(int target) {
    int old = vt_active;
    uint32_t flags;
    if (target == old || target < 0 || target >= VT_COUNT) return;

    /* Refresh the target's backing store before it becomes visible:
     * a vt whose shell3 is not running yet gets a notice instead of a
     * black screen. */
    vt_paint_placeholder(target);

    if (vbe_available()) {
        if (!vts[old].fb_ready || !vts[target].fb_ready) {
            serial_print("[VT] switch refused: VBE backing store missing\n");
            return;
        }
        flags = vt_irq_save();
        memcpy(vts[old].fb, vbe_scanout(), vbe_scanout_bytes());
        memcpy(vbe_scanout(), vts[target].fb, vbe_scanout_bytes());
        vt_active = target;
        vbe_console_switched();
        vt_irq_restore(flags);
    } else {
        flags = vt_irq_save();
        memcpy(vts[old].text_cells, (const void *)VT_VGA_MEMORY,
               VT_VGA_CELLS * sizeof(uint16_t));
        memcpy((void *)VT_VGA_MEMORY, vts[target].text_cells,
               VT_VGA_CELLS * sizeof(uint16_t));
        vt_active = target;
        vga_console_switched();
        vt_irq_restore(flags);
    }
    serial_print("[VT] switched to vt ");
    serial_print_dec((uint32_t)target);
    serial_putchar('\n');
}

void vt_service(void) {
    int target = vt_pending;
    vt_pending = -1;
    if (target >= 0) {
        vt_do_switch(target);
    }
    vt_ensure_shells();
}

static int vt_alloc_fb(int vt) {
    uint32_t size = vbe_scanout_bytes();
    uint32_t pages = (size + PAGE_SIZE - 1U) / PAGE_SIZE;
    uint32_t base = VT_FB_BASE + (uint32_t)vt * vt_fb_stride;
    if (size == 0U || vt_fb_stride == 0U ||
        base + vt_fb_stride > VT_FB_LIMIT) return -1;
    for (uint32_t p = 0; p < pages; ++p) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0U) return -1;
        paging_map_page(base + p * PAGE_SIZE, phys,
                        PAGE_PRESENT | PAGE_WRITABLE);
    }
    vts[vt].fb = (uint8_t *)base;
    vts[vt].fb_ready = 1;
    memset(vts[vt].fb, 0, size);
    return 0;
}

void vt_init(void) {
    memset(vts, 0, sizeof(vts));
    vt_active = VT_FALLBACK;
    vt_pending = -1;
    vt_spawner = 0;
    vt_fb_stride = 0U;
    for (int i = 0; i < VT_COUNT; ++i) {
        vts[i].switch_enabled = 1;
        for (uint32_t c = 0; c < VT_VGA_CELLS; ++c) {
            vts[i].text_cells[c] = (uint16_t)VT_VGA_BLANK;
        }
    }
    if (vbe_available()) {
        uint32_t size = vbe_scanout_bytes();
        vt_fb_stride = (size + VT_FB_ALIGN - 1U) & ~(VT_FB_ALIGN - 1U);
        if (vt_fb_stride == 0U ||
            VT_FB_BASE + (uint32_t)VT_COUNT * vt_fb_stride > VT_FB_LIMIT) {
            serial_print("[VT] VBE backing store window too small\n");
        } else {
            for (int i = 0; i < VT_COUNT; ++i) {
                if (vt_alloc_fb(i) != 0) {
                    serial_print("[VT] VBE backing store alloc failed on vt ");
                    serial_print_dec((uint32_t)i);
                    serial_putchar('\n');
                }
            }
        }
    }
}
