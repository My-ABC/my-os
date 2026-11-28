#include "stdint.h"
#include "stdio.h"
#include "vga.h"
#include "io.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "tsc.h"
#include "keyboard.h"
#include "serial.h"
#include "rtc.h"
#include "shell.h"
#include "tss.h"
#include "syscall.h"
#include "ring3.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "vbe.h"
#include "ata.h"
#include "elf.h"
#include "fat32.h"
#include "vt.h"

#ifdef PMM_QEMU_HIGHMEM_TEST
static void highmem_qemu_test(void) {
    const uint64_t minimum_address = 0x100000000ULL;
    const uint32_t virtual_address = 0xE0000000U;
    const uint32_t expected[4] = {
        0x13579BDFU, 0x2468ACE0U, 0xA5C35A3CU, 0x5A3CC3A5U
    };
    uint64_t physical = pmm_alloc_page_at_or_above(minimum_address);
    if (physical < minimum_address ||
        (physical & (PAGE_SIZE - 1U)) != 0U) {
        serial_print("[HIGHMEM] FAIL: no usable page at or above 4 GiB\n");
        goto halt;
    }

    paging_map_page(virtual_address, physical, PAGE_PRESENT | PAGE_WRITABLE);
    if (paging_get_page_physical(virtual_address) != physical) {
        serial_print("[HIGHMEM] FAIL: PAE mapping did not resolve to allocated frame\n");
        paging_unmap_page(virtual_address);
        pmm_free_page(physical);
        goto halt;
    }

    volatile uint32_t *mapped = (volatile uint32_t *)(uintptr_t)virtual_address;
    for (uint32_t i = 0U; i < 4U; ++i) mapped[i] = expected[i];
    for (uint32_t i = 0U; i < 4U; ++i) {
        if (mapped[i] != expected[i]) {
            serial_print("[HIGHMEM] FAIL: mapped page pattern mismatch\n");
            paging_unmap_page(virtual_address);
            pmm_free_page(physical);
            goto halt;
        }
    }

    paging_unmap_page(virtual_address);
    pmm_free_page(physical);
    uint64_t reused = pmm_alloc_page_at_or_above(minimum_address);
    if (reused != physical) {
        serial_print("[HIGHMEM] FAIL: freed high page was not reusable\n");
        if (reused != 0U) pmm_free_page(reused);
        goto halt;
    }
    pmm_free_page(reused);

    serial_print("[HIGHMEM] PASS: physical=");
    serial_print_hex((uint32_t)(physical >> 32));
    serial_putchar(':');
    serial_print_hex((uint32_t)physical);
    serial_print(" mapped, pattern verified, unmapped, and freed\n");
halt:
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}
#endif

/* Boot supervisor: when the mounted FAT32 disk carries /shell3.elf,
 * run it on vt 0 and let the vt layer keep a shell3 alive on every
 * virtual terminal (F1/F2 switch between them; each exit or kill is
 * respawned). The kernel shell is only reached when no disk is
 * mounted or the file is missing/unloadable. */
static uint8_t *shell3_image;

static int shell3_spawn_on_vt(int vt) {
    uint32_t size = 0U;
    if (shell3_image == 0) {
        shell3_image = (uint8_t *)kmalloc(FAT32_MAX_FILE_SIZE);
        if (shell3_image == 0) return VT_SPAWN_RETRY;
    }
    if (fat32_read_file("/shell3.elf", shell3_image, FAT32_MAX_FILE_SIZE,
                        &size) != 0) {
        return VT_SPAWN_RETRY;
    }
    int rc = elf_run_vt(shell3_image, size, vt);
    /* -7 (user-mm slots exhausted) and -8 (process-table schedule
     * failure) are both transient resource exhaustion; every other
     * nonzero is an ELF load/parse error and latches permanently. */
    return (rc == -7 || rc == -8) ? VT_SPAWN_RETRY : rc;
}

static void shell3_supervisor(void) {
    if (!fat32_mounted()) {
        start_shell();
        return;
    }
    vt_set_spawner(shell3_spawn_on_vt);
    if (vt_spawn_shell(VT_FALLBACK) != 0) {
        start_shell();
        return;
    }
    serial_print("[SHELL] Auto-started /shell3.elf (Ring 3 shell)\n");
    for (;;) {
        vt_service();
        if (vt_needs_ring0_fallback()) {
            serial_print("[SHELL] vt0 gave up; falling back to ring0 shell\n");
            break;
        }
        __asm__ volatile ("hlt");
    }
    /* The kernel shell's own read loop keeps polling vt_service, so
     * vt1 switching/respawn keeps working while it runs. */
    start_shell();
}

void kmain(uint32_t multiboot_info) {
    // 初始化 GDT（必须最先进行）
    gdt_init();
    tss_init();
    
    // 初始化串口（用于早期调试输出）
    serial_init();
    serial_printf("MyOS v0.0.1\n");

    // 初始化物理内存管理（在分页前确保可用内存位图已建立）
    pmm_init(multiboot_info);
    
    // 初始化分页（在GDT之后，VGA之前）
    paging_init();
    tss_update_cr3(paging_get_page_directory());

#ifdef PMM_QEMU_HIGHMEM_TEST
    highmem_qemu_test();
#endif

    // Keep the Multiboot information block available to the later VBE probe.
    // The runtime low identity map intentionally stops at 4 MiB so user ELF
    // virtual ranges above it remain available for demand mappings.
    if (multiboot_info != 0U) {
        uint32_t info_page = multiboot_info & ~(PAGE_SIZE - 1U);
        uint64_t info_end = (uint64_t)multiboot_info + 128U;
        for (uint64_t page = info_page; page < info_end; page += PAGE_SIZE) {
            paging_map_page((uint32_t)page, page, PAGE_PRESENT | PAGE_WRITABLE);
        }
    }

    // 使用启动阶段 BIOS VBE 调用返回的帧缓冲信息。
    vbe_initialize(multiboot_info);
    
    // 初始化 VGA
    vga_init();

    // 虚拟终端：为每个 tty 分配屏幕备份 (VBE 模式下含离屏帧缓冲)
    vt_init();

    serial_print("[DISK] Probing ATA primary master\n");
    if (ata_init()) {
        cmd_disk(1, 0);
    } else {
        serial_print("[DISK] ATA primary master not found\n");
    }
    
    printf("MyOS v0.2\n");
    printf("==========\n\n");
    
    // 测试日志输出
    vga_print_info("System initialized successfully");
    vga_print_success("GDT initialized");
    vga_print_success("Paging enabled");
    
    vga_print_info("Loading IDT...");
    idt_init();
    vga_print_success("IDT loaded");

    char *heap_test = (char *)kmalloc(100);
    if (heap_test != 0) {
        heap_test[0] = 'O';
        heap_test[1] = 'K';
        heap_test[2] = '\0';
        serial_print("[HEAP] kmalloc write: ");
        serial_print(heap_test);
        serial_print("\n");
        kfree(heap_test);
        serial_print("[HEAP] kfree completed\n");
    } else {
        serial_print("[HEAP] kmalloc failed\n");
    }

    char *heap_reuse = (char *)kmalloc(100);
    if (heap_reuse != 0) {
        serial_print("[HEAP] free-list reuse succeeded\n");
        kfree(heap_reuse);
    }

    char *heap_resize = (char *)kcalloc(8, 16);
    if (heap_resize != 0) {
        heap_resize = (char *)krealloc(heap_resize, 8192);
        if (heap_resize != 0) {
            serial_print("[HEAP] calloc/realloc succeeded\n");
            kfree(heap_resize);
        }
    }

    uint64_t test_phys = pmm_alloc_page();
    paging_map_page(0x00800000U, test_phys,
                    PAGE_PRESENT | PAGE_WRITABLE);
    uint32_t *test_ptr = (uint32_t *)0x00800000;
    *test_ptr = 0x12345678;
    serial_print("Write succeeded!\n");
    serial_printf("ptr value: %d\n", *test_ptr);
    paging_unmap_page(0x00800000U);
    pmm_free_page(test_phys);

    vga_print_info("Remapping PIC...");
    pic_remap();
    pic_set_mask(14);
    vga_print_success("PIC remapped");

    vga_print_info("Initializing PIT at 100Hz...");
    pit_init(PIT_FREQUENCY);
    pic_clear_mask(0);
    vga_print_success("PIT initialized");

    vga_print_info("Initializing keyboard (IRQ1)...");
    keyboard_init();

#ifdef SCANCODE_SET
    keyboard_set_scancode_set(SCANCODE_SET);
    serial_printf("Scancode set: %d\n", SCANCODE_SET);
#endif
    
    pic_clear_mask(1);
    vga_print_success("Keyboard initialized");

    vga_print_info("Enabling interrupts...");
    __asm__ volatile ("sti");
    vga_print_success("Interrupts enabled");

    vga_print_info("Calibrating TSC...");
    tsc_init();
    vga_print_success("TSC calibrated");

    vga_print_info("Initializing RTC...");
    rtc_init();
    vga_print_success("RTC initialized");

    vga_print_info("Kernel loaded at 0x100000");
    vga_print_warning("Low memory detected");
    vga_print_error("Network card not found");
    vga_print_success("VGA driver loaded");
    vga_print("\n");
    
    // 测试数字
    printf("Numbers: %d (dec), %x (hex)\n", 42, 0xDEADBEEF);

    // 显示RTC时间
    vga_print("Current time (UTC): ");
    rtc_print_datetime();
    
    // 显示北京时间
    vga_print("Beijing time (UTC+8): ");
    {
        rtc_time_t time;
        rtc_read_time(&time);
        rtc_to_beijing_time(&time);
        
        // 打印北京时间
        printf("%04d-%02d-%02d %02d:%02d:%02d\n", 
               time.year, time.month, time.day, time.hour, time.minute, time.second);
        
        // 同样输出到串口
        serial_printf("Beijing time (UTC+8): %04d-%02d-%02d %02d:%02d:%02d\n", 
                      time.year, time.month, time.day, time.hour, time.minute, time.second);
    }
    // 显示Unix时间戳
    rtc_print_unix_timestamp();

#ifdef PANIC_DEMO
    vga_print("Triggering INT3 (panic demo)\n");
    __asm__ volatile ("int $0x03");
#endif
#ifdef ELF_DEMO
    vga_print("Loading built-in ELF demo\n");
    cmd_elf(0, 0);
#endif
#ifdef KEYBOARD_DEMO
    printf("Timer ticking, output on COM1\n");
    printf("Press 'b' for a blue screen, any other key to halt\n");
    serial_printf("Press 'b' for a blue screen, any other key to halt\n");

    char key = keyboard_wait_key();
    printf("Key pressed: %c\n", key);
    serial_printf("Key pressed: %c\n", key);

    if (key == 'b' || key == 'B') {
        __asm__ volatile ("int $0x03");
    }

    serial_printf("Halted\n");
    printf("Halted\n");
    while (1);
#else
    shell3_supervisor();
#endif
}
