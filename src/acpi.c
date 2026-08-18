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
    uint8_t reserved[68];  // 到 flags 之前的字段, 这里用不到
    uint32_t flags;        // 偏移 112
    struct generic_address reset_reg;  // 偏移 116
    uint8_t reset_value;               // 偏移 128
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
