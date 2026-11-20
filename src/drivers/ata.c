#include "ata.h"
#include "io.h"
#include "serial.h"
#include "string.h"

#define ATA_REG_DATA       0x1F0U
#define ATA_REG_ERROR      0x1F1U
#define ATA_REG_SECCOUNT   0x1F2U
#define ATA_REG_LBA0       0x1F3U
#define ATA_REG_LBA1       0x1F4U
#define ATA_REG_LBA2       0x1F5U
#define ATA_REG_HDDEVSEL   0x1F6U
#define ATA_REG_STATUS     0x1F7U
#define ATA_REG_COMMAND    0x1F7U
#define ATA_STATUS_ERR     0x01U
#define ATA_STATUS_DRQ     0x08U
#define ATA_STATUS_BSY     0x80U

#define ATA_CMD_READ_SECTORS  0x20U
#define ATA_CMD_WRITE_SECTORS 0x30U
#define ATA_CMD_IDENTIFY      0xECU

static uint32_t g_sector_count;

static void ata_wait_400ns(void) {
    for (int i = 0; i < 4; ++i) {
        (void)inb(ATA_REG_STATUS);
    }
}

static int ata_wait_bsy_clear(void) {
    for (uint32_t i = 0; i < 100000U; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if ((status & ATA_STATUS_BSY) == 0U) {
            return (status & ATA_STATUS_ERR) == 0U;
        }
    }
    return 0;
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < 100000U; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if ((status & ATA_STATUS_ERR) != 0U) return 0;
        if ((status & ATA_STATUS_BSY) == 0U &&
            (status & ATA_STATUS_DRQ) != 0U) return 1;
    }
    return 0;
}

static void ata_lba28_setup(uint32_t lba, uint32_t count) {
    outb(ATA_REG_HDDEVSEL, (uint8_t)(0xE0U | ((lba >> 24) & 0x0FU)));
    ata_wait_400ns();
    outb(ATA_REG_ERROR, 0);
    outb(ATA_REG_SECCOUNT, (uint8_t)count);
    outb(ATA_REG_LBA0, (uint8_t)lba);
    outb(ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_REG_LBA2, (uint8_t)(lba >> 16));
}

int ata_init(void) {
    g_sector_count = 0U;
    outb(ATA_REG_HDDEVSEL, 0xA0U);
    ata_wait_400ns();
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_REG_STATUS);
    if (status == 0U) {
        return 0;
    }
    if (!ata_wait_bsy_clear()) {
        return 0;
    }
    if (!ata_wait_drq()) {
        return 0;
    }

    uint16_t identify[256];
    for (uint32_t i = 0; i < 256U; ++i) {
        identify[i] = inw(ATA_REG_DATA);
    }

    g_sector_count = (uint32_t)identify[60] |
                     ((uint32_t)identify[61] << 16);
    if (g_sector_count == 0U) {
        g_sector_count = 0U;
        return 0;
    }

    serial_print("[ATA] Primary master detected, sectors=");
    serial_print_dec(g_sector_count);
    serial_putchar('\n');
    return 1;
}

int ata_read_sectors(uint32_t lba, uint32_t count, void *buffer) {
    if (buffer == 0 || count == 0U || count > 255U ||
        g_sector_count != 0U && lba + count < lba) {
        return -1;
    }
    if (g_sector_count != 0U &&
        (lba >= g_sector_count || count > g_sector_count - lba)) {
        return -1;
    }

    ata_lba28_setup(lba, count);
    outb(ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    uint16_t *out = (uint16_t *)buffer;
    for (uint32_t sector = 0; sector < count; ++sector) {
        if (!ata_wait_drq()) return -1;
        for (uint32_t word = 0; word < 256U; ++word) {
            out[sector * 256U + word] = inw(ATA_REG_DATA);
        }
        ata_wait_400ns();
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void *buffer) {
    if (buffer == 0 || count == 0U || count > 255U ||
        g_sector_count != 0U &&
        (lba >= g_sector_count || count > g_sector_count - lba)) {
        return -1;
    }

    ata_lba28_setup(lba, count);
    outb(ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    const uint16_t *input = (const uint16_t *)buffer;
    for (uint32_t sector = 0; sector < count; ++sector) {
        if (!ata_wait_drq()) return -1;
        for (uint32_t word = 0; word < 256U; ++word) {
            outw(ATA_REG_DATA, input[sector * 256U + word]);
        }
        ata_wait_400ns();
    }
    if (!ata_wait_bsy_clear()) return -1;
    return 0;
}

uint32_t ata_sector_count(void) {
    return g_sector_count;
}
