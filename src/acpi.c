#include "acpi.h"
#include "io.h"
#include "serial.h"
#include "stddef.h"
#include "stdint.h"
#include "paging.h"

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
    uint8_t reserved1;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
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
    uint32_t flags;
    struct generic_address reset_reg;
    uint8_t reset_value;
    uint8_t reserved3[3];
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
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
    uint16_t pm1a_cnt_blk;        // 45-46 - PM1a 控制块端口 I/O 地址 (修复为uint16_t)
    uint16_t pm1b_cnt_blk;        // 47-48 - PM1b 控制块端口 I/O 地址 (修复为uint16_t)
    uint8_t pm2_cnt_blk;          // 49
    uint8_t pm1a_evt_blk;         // 50
    uint8_t pm1b_evt_blk;         // 51
    uint8_t pm2_evt_blk;          // 52
    uint8_t pm_tmr_blk;           // 53
    uint8_t gpe0_blk;             // 54
    uint8_t gpe1_blk;             // 55
    uint8_t pm1_cnt_len;          // 56
    uint8_t pm1_evt_len;          // 57
    uint8_t pm2_cnt_len;          // 58
    uint8_t pm2_evt_len;          // 59
    uint8_t pm_tmr_len;           // 60
    uint8_t gpe0_blk_len;         // 61
    uint8_t gpe1_blk_len;         // 62
    uint8_t gpe1_base;            // 63
    uint8_t cst_cnt;              // 64
    uint16_t p_lvl2_lat;          // 65-67
    uint16_t p_lvl3_lat;          // 67-69
    uint16_t flush_size;          // 69-71
    uint32_t flush_stride;        // 71-75
    uint8_t duty_offset;          // 75
    uint8_t duty_width;           // 76
    uint8_t day_alrm;             // 77
    uint8_t mon_alrm;             // 78
    uint8_t century;              // 79
    uint16_t iapc_boot_arch;      // 80-82
    uint8_t reserved2;            // 82
    uint32_t flags;               // 83-87
} __attribute__((packed));

#define FADT_RESET_REG_SUP (1u << 10)

#define ACPI_ADDRESS_SPACE_MEMORY 0
#define ACPI_ADDRESS_SPACE_IO     1
#define KERNEL_VIRT_BASE 0xC0000000U
#define KERNEL_PHYS_BASE 0x00100000U

static void *acpi_physical_ptr(uint32_t physical_addr) {
    if (physical_addr < KERNEL_PHYS_BASE + 0x00300000U) {
        return (void *)physical_addr;
    }

    uint32_t virtual_addr = KERNEL_VIRT_BASE + physical_addr - KERNEL_PHYS_BASE;
    paging_map_page(virtual_addr & ~(PAGE_SIZE - 1U),
                    physical_addr & ~(PAGE_SIZE - 1U),
                    PAGE_PRESENT | PAGE_WRITABLE);
    return (void *)(virtual_addr);
}

static struct sdt_header *acpi_map_table(uint32_t physical_addr) {
    struct sdt_header *header = (struct sdt_header *)acpi_physical_ptr(physical_addr);
    uint32_t start = physical_addr & ~(PAGE_SIZE - 1U);
    uint32_t end = physical_addr + header->length;

    for (uint32_t address = start; address < end; address += PAGE_SIZE) {
        acpi_physical_ptr(address);
    }
    return (struct sdt_header *)acpi_physical_ptr(physical_addr);
}

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
        
        // 在1:1 4GB映射下，所有物理地址都应该已经映射
        struct sdt_header* table = acpi_map_table(address);
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
        uint32_t xsdt_addr = (uint32_t)rsdp->xsdt_address;
        // 在1:1 4GB映射下，所有物理地址都应该已经映射
        struct sdt_header* xsdt = acpi_map_table(xsdt_addr);
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
    uint32_t rsdt_addr = rsdp->rsdt_address;
    // 在1:1 4GB映射下，所有物理地址都应该已经映射
    struct sdt_header* rsdt = acpi_map_table(rsdt_addr);
    if (!mem_equal(rsdt->signature, "RSDT", 4)) {
        return NULL;
    }
    return find_fadt_in(rsdt, 4);
}

static int acpi_find_s5(struct fadt *fadt, uint8_t *sleep_type) {
    uint32_t dsdt_addr = fadt->dsdt;
    if (dsdt_addr == 0U && fadt->header.length >= 140U) {
        dsdt_addr = (uint32_t)fadt->x_dsdt;
    }
    if (dsdt_addr == 0U) {
        return 0;
    }

    struct sdt_header *dsdt = acpi_map_table(dsdt_addr);
    uint8_t *data = (uint8_t *)dsdt + sizeof(struct sdt_header);
    uint32_t length = dsdt->length - sizeof(struct sdt_header);

    for (uint32_t i = 0; i + 6U < length; ++i) {
        if (data[i] == 0x08U && data[i + 1U] == '_' && data[i + 2U] == 'S' &&
            data[i + 3U] == '5' && data[i + 4U] == '_') {
            uint32_t package = i + 5U;
            if (data[package] == 0x12U) {
                uint8_t pkg_length_byte = data[package + 1U];
                uint32_t length_bytes = (pkg_length_byte >> 6) & 0x3U;
                uint32_t value = package + 2U;

                if (length_bytes == 0U) {
                    value = package + 3U;
                } else {
                    value = package + 2U + length_bytes;
                }

                // Skip NumElements and decode the first AML integer object.
                value++;
                if (data[value] == 0x0AU) {
                    *sleep_type = data[value + 1U];
                    return 1;
                }
                if (data[value] <= 0x0FU) {
                    *sleep_type = data[value];
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int acpi_reset_via_fadt(void) {
    struct fadt* fadt = find_fadt();
    if (fadt == NULL) {
        serial_print("[ACPI] FADT not found\n");
        return 0;
    }
    // ACPI 1.0 的 FADT (116 字节) 没有 reset register 字段
    uint8_t *fadt_bytes = (uint8_t *)fadt;
    uint32_t fadt_flags = *(uint32_t *)(fadt_bytes + 112U);
    struct generic_address *reset_reg = (struct generic_address *)(fadt_bytes + 116U);
    uint8_t reset_value = fadt_bytes[128U];

    if (fadt->header.length < 129U || !(fadt_flags & FADT_RESET_REG_SUP)) {
        serial_print("[ACPI] reset register not supported by firmware\n");
        return 0;
    }

    uint32_t address = (uint32_t)reset_reg->address;
    serial_print("[ACPI] reset via FADT: space=");
    serial_print_dec(reset_reg->address_space);
    serial_print(" address=");
    serial_print_hex(address);
    serial_print(" value=");
    serial_print_hex(reset_value);
    serial_putchar('\n');

    if (reset_reg->address_space == ACPI_ADDRESS_SPACE_IO) {
        outb((uint16_t)address, reset_value);
        return 1;
    } else if (reset_reg->address_space == ACPI_ADDRESS_SPACE_MEMORY) {
        *(volatile uint8_t*)acpi_physical_ptr(address) = reset_value;
        return 1;
    }

    return 0;
}

void acpi_reboot(void) {
    __asm__ volatile ("cli");

    if (acpi_reset_via_fadt()) {
        serial_print("[ACPI] reset command sent, waiting for reset\n");
        for (volatile uint32_t delay = 0; delay < 1000000U; ++delay) {
            __asm__ volatile ("nop");
        }
    }

    // 走到这里说明 ACPI 复位没生效
    serial_print("[ACPI] reset did not take effect, falling back\n");

    // 兜底 1: PCI 复位控制寄存器
    outb(0xCF9, 0x02);
    for (volatile uint32_t delay = 0; delay < 10000U; ++delay) {
        __asm__ volatile ("nop");
    }
    outb(0xCF9, 0x06);

    // 兜底 2: 键盘控制器脉冲 CPU RESET 线
    for (int i = 0; i < 1000; i++) {
        if (!(inb(0x64) & 0x02)) {
            break;
        }
    }
    outb(0x64, 0xFE);
    for (volatile uint32_t delay = 0; delay < 1000000U; ++delay) {
        __asm__ volatile ("nop");
    }

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

    uint32_t pm1a_cnt = 0;
    uint32_t pm1b_cnt = fadt->pm1b_cnt_blk;
    uint8_t pm1_cnt_len = 0;
    uint8_t s5_type = 0;
    
    // 用于内存空间关机的标志
    int use_memory_space = 0;
    uint32_t memory_address = 0;
    
    pm1a_cnt = fadt->pm1a_cnt_blk;
    pm1_cnt_len = fadt->pm1_cnt_len;
    if (pm1a_cnt == 0U && fadt->header.length >= 176U && fadt->x_pm1a_cnt_blk.address != 0U) {
        if (fadt->x_pm1a_cnt_blk.address_space == ACPI_ADDRESS_SPACE_IO) {
            pm1a_cnt = (uint32_t)fadt->x_pm1a_cnt_blk.address;
        } else if (fadt->x_pm1a_cnt_blk.address_space == ACPI_ADDRESS_SPACE_MEMORY) {
            use_memory_space = 1;
            memory_address = (uint32_t)acpi_physical_ptr((uint32_t)fadt->x_pm1a_cnt_blk.address);
        }
    }

    serial_print("[ACPI] PM1a_cnt_blk=");
    serial_print_hex(pm1a_cnt);
    serial_print(" pm1_cnt_len=");
    serial_print_dec(pm1_cnt_len);
    serial_putchar('\n');

    if (!acpi_find_s5(fadt, &s5_type)) {
        serial_print("[ACPI] S5 not found in DSDT\n");
        return;
    }
    serial_print("[ACPI] S5 SLP_TYP=");
    serial_print_dec(s5_type);
    serial_putchar('\n');

    if (pm1a_cnt == 0 && !use_memory_space) {
        serial_print("[ACPI] PM1a control block address is zero\n");
        serial_print("[ACPI] Try using: qemu-system-i386 -machine q35 -kernel myos.bin -serial stdio\n");
        return;
    }

    // 根据 PM1_CNT_LEN 确定访问大小（如果长度为0，默认为2字节）
    int access_size = (pm1_cnt_len > 0) ? pm1_cnt_len : 2;
    if (access_size != 1 && access_size != 2 && access_size != 4) {
        access_size = 2;
    }
    
    uint32_t current_value = 0;
    
    if (use_memory_space) {
        serial_print("[ACPI] shutdown via PM1a control block: memory address=");
        serial_print_hex(memory_address);
        serial_print(" access_size=");
        serial_print_dec(access_size);
        serial_putchar('\n');
        
        // 读取当前 PM1a 控制寄存器值（内存空间）
        if (access_size == 2) {
            volatile uint16_t* pm1a_cnt_mem = (volatile uint16_t*)memory_address;
            current_value = *pm1a_cnt_mem;
        } else if (access_size == 4) {
            volatile uint32_t* pm1a_cnt_mem = (volatile uint32_t*)memory_address;
            current_value = *pm1a_cnt_mem;
        } else {
            volatile uint8_t* pm1a_cnt_mem = (volatile uint8_t*)memory_address;
            current_value = *pm1a_cnt_mem;
        }

        uint32_t sleep_value = (current_value & ~(0x7U << 10)) |
                               ((uint32_t)s5_type << 10) | (1U << 13);
        serial_print("[ACPI] writing S5 sleep_value=");
        serial_print_hex(sleep_value);
        serial_putchar('\n');

        if (access_size == 2) {
            volatile uint16_t* pm1a_cnt_mem = (volatile uint16_t*)memory_address;
            *pm1a_cnt_mem = (uint16_t)sleep_value;
        } else if (access_size == 4) {
            volatile uint32_t* pm1a_cnt_mem = (volatile uint32_t*)memory_address;
            *pm1a_cnt_mem = sleep_value;
        } else {
            volatile uint8_t* pm1a_cnt_mem = (volatile uint8_t*)memory_address;
            *pm1a_cnt_mem = (uint8_t)sleep_value;
        }
    } else {
        serial_print("[ACPI] shutdown via PM1a control block: port=");
        serial_print_hex(pm1a_cnt);
        serial_print(" access_size=");
        serial_print_dec(access_size);
        serial_putchar('\n');

        // 读取当前 PM1a 控制寄存器值（I/O 空间）
        if (access_size == 2) {
            current_value = inw(pm1a_cnt);
        } else if (access_size == 4) {
            current_value = inl(pm1a_cnt);
        } else {
            current_value = inb(pm1a_cnt);
        }

        uint32_t sleep_value = (current_value & ~(0x7U << 10)) |
                               ((uint32_t)s5_type << 10) | (1U << 13);
        serial_print("[ACPI] writing S5 sleep_value=");
        serial_print_hex(sleep_value);
        serial_putchar('\n');

        if (access_size == 2) {
            outw((uint16_t)pm1a_cnt, (uint16_t)sleep_value);
        } else if (access_size == 4) {
            outl((uint16_t)pm1a_cnt, sleep_value);
        } else {
            outb((uint16_t)pm1a_cnt, (uint8_t)sleep_value);
        }

        if (pm1b_cnt != 0U) {
            if (access_size == 2) {
                outw((uint16_t)pm1b_cnt, (uint16_t)sleep_value);
            } else if (access_size == 4) {
                outl((uint16_t)pm1b_cnt, sleep_value);
            } else {
                outb((uint16_t)pm1b_cnt, (uint8_t)sleep_value);
            }
        }
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
