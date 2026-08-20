#include "acpi.h"
#include "io.h"
#include "serial.h"
#include "stddef.h"
#include "stdint.h"

struct rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    // ACPI 2.0+ 扩展部分, revision >= 2 时有效
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

// ACPI 通用地址结构
struct generic_address {
    uint8_t address_space;  // 0 = 内存, 1 = 端口 I/O
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

struct fadt {
    struct sdt_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t pm1a_cnt_blk;    // PM1a 控制块端口 I/O 地址
    uint8_t pm1b_cnt_blk;    // PM1b 控制块端口 I/O 地址
    uint8_t pm2_cnt_blk;
    uint8_t pm1a_evt_blk;
    uint8_t pm1b_evt_blk;
    uint8_t pm2_evt_blk;
    uint8_t pm_tmr_blk;
    uint8_t gpe0_blk;
    uint8_t gpe1_blk;
    uint8_t pm1_cnt_len;
    uint8_t pm1_evt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm2_evt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint32_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;        // 偏移 112
    struct generic_address reset_reg;  // 偏移 116
    uint8_t reset_value;               // 偏移 128
    uint8_t reserved3[3];
    uint8_t x_firmware_ctrl;
    uint8_t x_dsdt;
    struct generic_address x_pm1a_cnt_blk;
    struct generic_address x_pm1b_cnt_blk;
    struct generic_address x_pm2_cnt_blk;
    struct generic_address x_pm1a_evt_blk;
    struct generic_address x_pm1b_evt_blk;
    struct generic_address x_pm2_evt_blk;
    struct generic_address x_pm_tmr_blk;
    struct generic_address x_gpe0_blk;
    struct generic_address x_gpe1_blk;
} __attribute__((packed));

// ACPI 1.0 FADT 结构体（简化版，用于读取旧版 FADT）
struct fadt_legacy {
    struct sdt_header header;     // 0-36
    uint32_t firmware_ctrl;       // 36-40
    uint32_t dsdt;                // 40-44
    uint8_t int_model;            // 44
    uint8_t pm1a_cnt_blk;         // 45 - PM1a 控制块端口 I/O 地址
    uint8_t pm1b_cnt_blk;         // 46 - PM1b 控制块端口 I/O 地址
    uint8_t pm2_cnt_blk;          // 47
    uint8_t pm1a_evt_blk;         // 48
    uint8_t pm1b_evt_blk;         // 49
    uint8_t pm2_evt_blk;          // 50
    uint8_t pm_tmr_blk;           // 51
    uint8_t gpe0_blk;             // 52
    uint8_t gpe1_blk;             // 53
    uint8_t pm1_cnt_len;          // 54
    uint8_t pm1_evt_len;          // 55
    uint8_t pm2_cnt_len;          // 56
    uint8_t pm2_evt_len;          // 57
    uint8_t pm_tmr_len;           // 58
    uint8_t gpe0_blk_len;         // 59
    uint8_t gpe1_blk_len;         // 60
    uint8_t gpe1_base;            // 61
    uint8_t cst_cnt;              // 62
    uint16_t p_lvl2_lat;          // 63-65
    uint16_t p_lvl3_lat;          // 65-67
    uint16_t flush_size;          // 67-69
    uint32_t flush_stride;        // 69-73
    uint8_t duty_offset;          // 73
    uint8_t duty_width;           // 74
    uint8_t day_alrm;             // 75
    uint8_t mon_alrm;             // 76
    uint8_t century;              // 77
    uint16_t iapc_boot_arch;      // 78-80
    uint8_t reserved2;            // 80
    uint32_t flags;               // 81-85
} __attribute__((packed));

#define FADT_RESET_REG_SUP (1u << 10)

#define ACPI_ADDRESS_SPACE_MEMORY 0
#define ACPI_ADDRESS_SPACE_IO     1

static int mem_equal(const char* a, const char* b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int checksum_ok(const uint8_t* data, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum == 0;
}

static struct rsdp* find_rsdp_in(uint32_t start, uint32_t end) {
    // RSDP 按 16 字节对齐
    for (uint32_t addr = start; addr < end; addr += 16) {
        struct rsdp* candidate = (struct rsdp*)addr;
        // 前 20 字节是 ACPI 1.0 部分, 校验和只覆盖这一段
        if (mem_equal(candidate->signature, "RSD PTR ", 8) &&
            checksum_ok((const uint8_t*)candidate, 20)) {
            return candidate;
        }
    }
    return NULL;
}

static struct rsdp* find_rsdp(void) {
    // 先搜 EBDA 的头 1KB, 再搜 BIOS 只读区
    uint32_t ebda = (uint32_t)(*(volatile uint16_t*)0x40E) << 4;
    if (ebda >= 0x400 && ebda < 0xA0000) {
        struct rsdp* found = find_rsdp_in(ebda, ebda + 1024);
        if (found != NULL) {
            return found;
        }
    }
    return find_rsdp_in(0xE0000, 0x100000);
}

// 在 RSDT (4 字节指针) 或 XSDT (8 字节指针) 中查找 FADT
static struct fadt* find_fadt_in(struct sdt_header* root, int pointer_size) {
    uint32_t entries = (root->length - sizeof(struct sdt_header)) / pointer_size;
    uint8_t* tables = (uint8_t*)root + sizeof(struct sdt_header);
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t address = pointer_size == 8
            ? (uint32_t)*(uint64_t*)(tables + i * 8)
            : *(uint32_t*)(tables + i * 4);
        struct sdt_header* table = (struct sdt_header*)address;
        if (mem_equal(table->signature, "FACP", 4)) {
            return (struct fadt*)table;
        }
    }
    return NULL;
}

static struct fadt* find_fadt(void) {
    struct rsdp* rsdp = find_rsdp();
    if (rsdp == NULL) {
        return NULL;
    }

    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        struct sdt_header* xsdt = (struct sdt_header*)(uint32_t)rsdp->xsdt_address;
        if (mem_equal(xsdt->signature, "XSDT", 4)) {
            struct fadt* fadt = find_fadt_in(xsdt, 8);
            if (fadt != NULL) {
                return fadt;
            }
        }
    }

    if (rsdp->rsdt_address == 0) {
        return NULL;
    }
    struct sdt_header* rsdt = (struct sdt_header*)rsdp->rsdt_address;
    if (!mem_equal(rsdt->signature, "RSDT", 4)) {
        return NULL;
    }
    return find_fadt_in(rsdt, 4);
}

static void acpi_reset_via_fadt(void) {
    struct fadt* fadt = find_fadt();
    if (fadt == NULL) {
        serial_print("[ACPI] FADT not found\n");
        return;
    }
    // ACPI 1.0 的 FADT (116 字节) 没有 reset register 字段
    if (fadt->header.length < sizeof(struct fadt) ||
        !(fadt->flags & FADT_RESET_REG_SUP)) {
        serial_print("[ACPI] reset register not supported by firmware\n");
        return;
    }

    uint32_t address = (uint32_t)fadt->reset_reg.address;
    serial_print("[ACPI] reset via FADT: space=");
    serial_print_dec(fadt->reset_reg.address_space);
    serial_print(" address=");
    serial_print_hex(address);
    serial_print(" value=");
    serial_print_hex(fadt->reset_value);
    serial_putchar('\n');

    if (fadt->reset_reg.address_space == ACPI_ADDRESS_SPACE_IO) {
        outb((uint16_t)address, fadt->reset_value);
    } else if (fadt->reset_reg.address_space == ACPI_ADDRESS_SPACE_MEMORY) {
        *(volatile uint8_t*)address = fadt->reset_value;
    }
}

void acpi_reboot(void) {
    __asm__ volatile ("cli");

    acpi_reset_via_fadt();

    // 走到这里说明 ACPI 复位没生效
    serial_print("[ACPI] reset did not take effect, falling back\n");

    // 兜底 1: PCI 复位控制寄存器
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    // 兜底 2: 键盘控制器脉冲 CPU RESET 线
    for (int i = 0; i < 1000; i++) {
        if (!(inb(0x64) & 0x02)) {
            break;
        }
    }
    outb(0x64, 0xFE);

    // 兜底 3: 加载空 IDT 后触发三重错误
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = {0, 0};
    __asm__ volatile ("lidt %0" :: "m" (null_idt));
    __asm__ volatile ("int $0x03");

    while (1) {
        __asm__ volatile ("hlt");
    }
}

void acpi_shutdown(void) {
    __asm__ volatile ("cli");

    struct fadt* fadt = find_fadt();
    if (fadt == NULL) {
        serial_print("[ACPI] FADT not found, shutdown not supported\n");
        return;
    }

    serial_print("[ACPI] FADT found, length=");
    serial_print_dec(fadt->header.length);
    serial_putchar('\n');

    uint16_t pm1a_cnt = 0;
    
    // 用于内存空间关机的标志
    int use_memory_space = 0;
    uint32_t memory_address = 0;
    
    // 根据 FADT 长度决定使用哪个结构体
    if (fadt->header.length <= 116) {
        // ACPI 1.0 简化版 FADT
        struct fadt_legacy* legacy_fadt = (struct fadt_legacy*)fadt;
        pm1a_cnt = legacy_fadt->pm1a_cnt_blk;
        serial_print("[ACPI] legacy PM1a_cnt_blk=");
        serial_print_hex(pm1a_cnt);
        serial_putchar('\n');
    } else {
        // ACPI 2.0+ 完整版 FADT
        pm1a_cnt = fadt->pm1a_cnt_blk;
        serial_print("[ACPI] PM1a_cnt_blk=");
        serial_print_hex(pm1a_cnt);
        serial_putchar('\n');

        // 如果传统地址为 0，尝试使用扩展地址结构
        if (pm1a_cnt == 0 && fadt->header.length >= 176) {
            if (fadt->x_pm1a_cnt_blk.address != 0) {
                if (fadt->x_pm1a_cnt_blk.address_space == ACPI_ADDRESS_SPACE_IO) {
                    pm1a_cnt = (uint16_t)fadt->x_pm1a_cnt_blk.address;
                    serial_print("[ACPI] using extended PM1a control block (I/O space)\n");
                } else if (fadt->x_pm1a_cnt_blk.address_space == ACPI_ADDRESS_SPACE_MEMORY) {
                    // 支持内存空间的 ACPI 地址
                    use_memory_space = 1;
                    memory_address = (uint32_t)fadt->x_pm1a_cnt_blk.address;
                    serial_print("[ACPI] using extended PM1a control block (memory space)\n");
                }
            }
        }
    }

    if (pm1a_cnt == 0 && !use_memory_space) {
        serial_print("[ACPI] PM1a control block address is zero\n");
        serial_print("[ACPI] Try using: qemu-system-i386 -machine q35 -kernel myos.bin -serial stdio\n");
        return;
    }

    uint16_t current_value = 0;
    
    if (use_memory_space) {
        serial_print("[ACPI] shutdown via PM1a control block: memory address=");
        serial_print_hex(memory_address);
        serial_putchar('\n');
        
        // 读取当前 PM1a 控制寄存器值（内存空间）
        volatile uint16_t* pm1a_cnt_mem = (volatile uint16_t*)memory_address;
        current_value = *pm1a_cnt_mem;

        // S5 关机: 设置 SLP_TYP 为 S5 (0x7) 并设置 SLP_EN 位 (bit 13)
        // SLP_TYP 位 10-12, SLP_EN 位 13
        uint16_t sleep_value = (current_value & ~(0x7 << 10)) | (0x7 << 10) | (1 << 13);
        
        // 写入 PM1a 控制寄存器（内存空间）
        *pm1a_cnt_mem = sleep_value;
    } else {
        serial_print("[ACPI] shutdown via PM1a control block: port=");
        serial_print_hex(pm1a_cnt);
        serial_putchar('\n');

        // 读取当前 PM1a 控制寄存器值（I/O 空间）
        current_value = inw(pm1a_cnt);

        // S5 关机: 设置 SLP_TYP 为 S5 (0x7) 并设置 SLP_EN 位 (bit 13)
        // SLP_TYP 位 10-12, SLP_EN 位 13
        uint16_t sleep_value = (current_value & ~(0x7 << 10)) | (0x7 << 10) | (1 << 13);
        
        // 写入 PM1a 控制寄存器（I/O 空间）
        outw(pm1a_cnt, sleep_value);
    }

    // 短暂延迟，让关机生效
    for (int i = 0; i < 100000; i++) {
        __asm__ volatile ("nop");
    }

    // 如果关机成功，代码不会执行到这里
    serial_print("[ACPI] ACPI shutdown failed, trying fallback methods\n");

    // 备用方案: 尝试使用常见的 ACPI I/O 端口
    serial_print("[ACPI] trying common ACPI I/O ports\n");
    
    // 尝试端口 0xB004
    outw(0xB004, 0x2000);
    for (int i = 0; i < 100000; i++) {
        __asm__ volatile ("nop");
    }
    
    // 尝试端口 0x604
    outw(0x604, 0x2000);
    for (int i = 0; i < 100000; i++) {
        __asm__ volatile ("nop");
    }
    
    // 尝试端口 0x4004
    outw(0x4004, 0x2000);
    for (int i = 0; i < 100000; i++) {
        __asm__ volatile ("nop");
    }
    
    // 备用方案: 尝试通过 PCI 来关机
    serial_print("[ACPI] trying PCI power management\n");
    outw(0xCF9, 0x0E);  // 尝试关机而不是重启
    
    // 最后的备用方案: 停机
    serial_print("[ACPI] all shutdown methods failed, halting\n");
    
    while (1) {
        __asm__ volatile ("hlt");
    }
}
